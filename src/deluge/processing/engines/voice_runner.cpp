/*
 * Copyright © 2026 The Deluge Firmware contributors
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "processing/engines/voice_runner.h"

#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/chaos_runner.h"
#include "processing/engines/cv_engine.h"
#include "processing/engines/pitch_tracker.h"
#include "processing/engines/turing_runner.h"

#include <algorithm>
#include <cmath>

namespace deluge::voice::runner {

namespace {

constexpr int32_t kRingSize = 8192; ///< input frames kept between scheduler runs (power of two), ~190 ms
constexpr int32_t kMaxBlocksPerTick = 64;

// level thresholds (RMS, 0 to 1) for each sensitivity step
constexpr float kThresholds[kNumSensitivities] = {0.06f, 0.03f, 0.015f, 0.008f, 0.004f, 0.002f};

Settings settings;
PitchTracker tracker;
NoteFollower follower;

float ring[kRingSize];
volatile uint32_t writePos = 0;
uint32_t readPos = 0;

bool gateSent = false;
int32_t lastNote = -1;
uint32_t voicedCountdown = 0;
float level = 0.0f;

constexpr int32_t kEstimatesPerSlot = 21; ///< about 245 ms
int32_t history[kHistoryLength];
int32_t estimatesInSlot = 0;
uint32_t historyChanges = 0;

void clearHistory() {
	for (int32_t& n : history) {
		n = -1;
	}
	estimatesInSlot = 0;
	historyChanges++;
}

void updateHistory(bool gate, int32_t note) {
	int32_t& current = history[kHistoryLength - 1];
	if (gate && current != note) {
		current = note;
		historyChanges++;
	}
	if (++estimatesInSlot >= kEstimatesPerSlot) {
		estimatesInSlot = 0;
		for (int32_t i = 0; i + 1 < kHistoryLength; i++) {
			history[i] = history[i + 1];
		}
		history[kHistoryLength - 1] = -1;
		historyChanges++;
	}
}

PhraseLoop phrase;
bool loopGateHigh = false;
uint32_t lastTriggerRises = 0;
int8_t lastChannel = 0; ///< the channel the voice tracker was last on, so a phrase can switch it back on
bool recordedGate = false;
int32_t recordedNote = -1;

void releaseChannel(int32_t channel) {
	phrase.stop();
	if (phrase.isRecording()) {
		phrase.clear();
	}
	loopGateHigh = false;
	if (channel >= 0) {
		CVEngine& cv = cvEngine;
		cv.sendChaosGate(channel, false);
	}
	gateSent = false;
}

void claim() {
	if (settings.channel >= 0) {
		uint8_t bit = 1 << settings.channel;
		deluge::chaos::runner::setExternalOwnership(1, bit, bit);
	}
	else {
		deluge::chaos::runner::setExternalOwnership(1, 0, 0);
	}
}

/// Voltage code for a (possibly fractional) note, by interpolating between neighbouring semitones
int32_t codeForNote(float note, uint8_t channel) {
	float floorNote = std::floor(note);
	int32_t base = static_cast<int32_t>(floorNote);
	int32_t c0 = cvEngine.calculateVoltage(base, channel);
	int32_t c1 = cvEngine.calculateVoltage(base + 1, channel);
	float frac = note - floorNote;
	int32_t code = c0 + static_cast<int32_t>(frac * static_cast<float>(c1 - c0));
	return std::max<int32_t>(0, std::min<int32_t>(code, deluge::chaos::kDacMaxCode));
}

bool songClockRunning() {
	return currentSong != nullptr && playbackHandler.isEitherClockActive();
}

/// The phrase's clock: sixteenth-note steps of the song, or audio time in units of 64 samples (about 1.5 ms)
uint32_t loopNow() {
	if (settings.loopTiming == LoopTiming::SONG) {
		if (currentSong == nullptr) {
			return 0;
		}
		int64_t stepTicks = std::max<int64_t>(1, static_cast<int64_t>(currentSong->getQuarterNoteLength()) / 4);
		int64_t ticks = std::max<int64_t>(0, playbackHandler.getCurrentInternalTickCount());
		return static_cast<uint32_t>((ticks + stepTicks / 2) / stepTicks); // nearest step
	}
	return AudioEngine::audioSampleTimer >> 6;
}

void outputNote(float note) {
	if (settings.channel < 0) {
		return;
	}
	note += 12.0f * settings.octaveShift;
	cvEngine.sendChaosVoltage(settings.channel, static_cast<uint16_t>(codeForNote(note, settings.channel)));
}

void loopGate(bool high) {
	if (settings.channel >= 0) {
		cvEngine.sendChaosGate(settings.channel, high);
	}
	loopGateHigh = high;
}

void emitLoop(bool gate, uint8_t note) {
	if (gate) {
		outputNote(static_cast<float>(note));
	}
	loopGate(gate);
}

/// Hands the output over to the phrase (or back to the live voice): closes whatever gate is open
void beginPhrasePlayback() {
	if (gateSent) {
		cvEngine.sendChaosGate(settings.channel, false);
		gateSent = false;
	}
	phrase.start(loopNow(), settings.loopOneShot);
}

void endPhrasePlayback() {
	phrase.stop();
	if (loopGateHigh) {
		loopGate(false);
	}
	gateSent = false; // the live voice picks the gate up again if it is singing
}

void outputPitch() {
	if (settings.channel < 0) {
		return;
	}
	float note = settings.quantise ? static_cast<float>(follower.note()) : follower.pitch();
	note += 12.0f * settings.octaveShift;
	cvEngine.sendChaosVoltage(settings.channel, static_cast<uint16_t>(codeForNote(note, settings.channel)));
}

} // namespace

const Settings& getSettings() {
	return settings;
}

void loadSettings(const Settings& s) {
	setChannel(-1);
	settings = s;
	settings.channel = -1;
	settings.octaveShift = static_cast<int8_t>(std::max<int32_t>(-2, std::min<int32_t>(s.octaveShift, 2)));
	settings.sensitivity = static_cast<uint8_t>(std::min<int32_t>(s.sensitivity, kNumSensitivities - 1));
	if (settings.loopTrigger < -1 || settings.loopTrigger > 3) {
		settings.loopTrigger = -1;
	}
	tracker.setLevelThreshold(kThresholds[settings.sensitivity]);
	setChannel(s.channel);
}

void resetToDefaults() {
	phrase.clear();
	Settings defaults;
	loadSettings(defaults);
}

void setChannel(int32_t channel) {
	int8_t wanted = (channel == 0 || channel == 1) ? static_cast<int8_t>(channel) : static_cast<int8_t>(-1);
	if (wanted == settings.channel) {
		return;
	}
	releaseChannel(settings.channel);
	settings.channel = wanted;
	if (wanted >= 0) {
		lastChannel = wanted;
		// the Turing machine can't share the channel
		if (deluge::turing::runner::getChannel() == wanted) {
			deluge::turing::runner::setChannel(-1);
		}
		tracker.reset();
		follower.reset();
		tracker.setLevelThreshold(kThresholds[settings.sensitivity]);
		readPos = writePos;
		lastNote = -1;
		voicedCountdown = 0;
		level = 0.0f;
		clearHistory();
	}
	claim();
}

int32_t getChannel() {
	return settings.channel;
}

bool isActive() {
	return settings.channel >= 0;
}

void setQuantise(bool quantise) {
	settings.quantise = quantise;
}

void setOctaveShift(int32_t octaves) {
	settings.octaveShift = static_cast<int8_t>(std::max<int32_t>(-2, std::min<int32_t>(octaves, 2)));
}

void setSensitivity(int32_t sensitivity) {
	settings.sensitivity = static_cast<uint8_t>(std::max<int32_t>(0, std::min<int32_t>(sensitivity, kNumSensitivities - 1)));
	tracker.setLevelThreshold(kThresholds[settings.sensitivity]);
}

void feedInput(const int32_t* interleavedStereo, int32_t numFrames) {
	if (settings.channel < 0) {
		return;
	}
	uint32_t w = writePos;
	for (int32_t i = 0; i < numFrames; i++) {
		// Q31 to -1..1
		// both input channels are added, so it doesn't matter which one the mic or line input arrives on
		ring[w & (kRingSize - 1)] = (static_cast<float>(interleavedStereo[i * 2] >> 1)
		                             + static_cast<float>(interleavedStereo[i * 2 + 1] >> 1))
		                            * (1.0f / 2147483648.0f);
		w++;
	}
	writePos = w;
}

void tick() {
	if (settings.channel < 0) {
		return;
	}

	// restart the phrase on triggers from another gate
	if (settings.loopTrigger >= 0 && settings.loopTrigger != settings.channel && phrase.hasPhrase()) {
		uint32_t rises = cvEngine.gateRiseCount(static_cast<uint8_t>(settings.loopTrigger));
		if (rises != lastTriggerRises) {
			lastTriggerRises = rises;
			triggerLoop();
		}
	}
	// the phrase drives the output while it plays
	if (phrase.isPlaying() && (settings.loopTiming == LoopTiming::FREE || songClockRunning())) {
		if (!phrase.advance(loopNow(), emitLoop)) {
			endPhrasePlayback();
		}
	}

	uint32_t w = writePos;
	uint32_t available = w - readPos;
	if (available > static_cast<uint32_t>(kRingSize)) {
		// we fell behind and the oldest audio was overwritten: skip ahead
		readPos = w - kRingSize / 2;
		available = kRingSize / 2;
	}

	float block[128];
	int32_t blocks = 0;
	while (available > 0 && blocks < kMaxBlocksPerTick) {
		uint32_t n = std::min<uint32_t>(available, 128);
		for (uint32_t i = 0; i < n; i++) {
			block[i] = ring[(readPos + i) & (kRingSize - 1)];
		}
		readPos += n;
		available -= n;
		blocks++;

		int32_t produced = tracker.feed(block, static_cast<int32_t>(n));
		// at most one estimate per 128 decimated samples (512 input frames), so a block holds at most one
		for (int32_t k = 0; k < produced; k++) {
			const PitchResult& r = tracker.latest();
			level = r.level;
			if (r.voiced) {
				voicedCountdown = 40; // about 460 ms of display hold
			}
			else if (voicedCountdown > 0) {
				voicedCountdown--;
			}
			bool newNote = follower.update(r);
			updateHistory(follower.gate(), follower.note());
			if (phrase.isRecording()) {
				bool g = follower.gate();
				int32_t n = follower.note();
				if (g != recordedGate || (g && n != recordedNote)) {
					phrase.record(loopNow(), g, static_cast<uint8_t>(n));
					recordedGate = g;
					recordedNote = n;
				}
			}
			if (phrase.isPlaying()) {
				continue; // the recorded phrase has the output
			}
			if (follower.gate() != gateSent) {
				if (follower.gate()) {
					outputPitch();
				}
				cvEngine.sendChaosGate(settings.channel, follower.gate());
				gateSent = follower.gate();
				if (follower.gate()) {
					lastNote = follower.note();
				}
			}
			else if (follower.gate()) {
				// a changed note re-triggers: pull the gate low briefly via a one-estimate gap is not possible without
				// the scheduler, so just move the pitch (legato)
				if (newNote) {
					lastNote = follower.note();
				}
				outputPitch();
			}
		}
	}
}

bool gateOn() {
	return gateSent;
}

int32_t currentNote() {
	return lastNote;
}

float currentLevel() {
	return level;
}

RecordResult toggleRecord() {
	if (settings.channel < 0) {
		return RecordResult::NOT_ACTIVE;
	}
	if (phrase.isRecording()) {
		// song-clock phrases are whole steps, at least one
		bool ok = phrase.endRecording(loopNow(), 1);
		if (!ok) {
			return RecordResult::NOTHING_SUNG;
		}
		beginPhrasePlayback();
		return RecordResult::PLAYING;
	}
	if (settings.loopTiming == LoopTiming::SONG && !songClockRunning()) {
		return RecordResult::NEEDS_PLAYBACK;
	}
	endPhrasePlayback();
	phrase.beginRecording(loopNow());
	recordedGate = false;
	recordedNote = -1;
	if (follower.gate()) {
		// already singing as the recording starts
		recordedGate = true;
		recordedNote = follower.note();
		phrase.record(loopNow(), true, static_cast<uint8_t>(recordedNote));
	}
	return RecordResult::STARTED;
}

void triggerLoop() {
	if (!phrase.hasPhrase() || phrase.isRecording()) {
		return;
	}
	if (settings.channel < 0) {
		// the voice tracker was switched off (leaving a phrase behind): switch it back on where it was
		setChannel(lastChannel);
		if (settings.channel < 0) {
			return;
		}
	}
	beginPhrasePlayback();
}

void stopLoop() {
	endPhrasePlayback();
}

void clearLoop() {
	endPhrasePlayback();
	phrase.clear();
}

void setLoopTiming(LoopTiming timing) {
	if (timing == settings.loopTiming) {
		return;
	}
	// the stored phrase is in the old unit, so it can't be played in the new one
	endPhrasePlayback();
	phrase.clear();
	settings.loopTiming = timing;
}

void setLoopOneShot(bool oneShot) {
	settings.loopOneShot = oneShot;
}

void setLoopTrigger(int32_t gate) {
	settings.loopTrigger = (gate >= 0 && gate < 4) ? static_cast<int8_t>(gate) : static_cast<int8_t>(-1);
	if (settings.loopTrigger >= 0) {
		lastTriggerRises = cvEngine.gateRiseCount(static_cast<uint8_t>(settings.loopTrigger));
	}
}

LoopState loopState() {
	if (phrase.isRecording()) {
		return LoopState::RECORDING;
	}
	if (phrase.isPlaying()) {
		return LoopState::PLAYING;
	}
	return phrase.hasPhrase() ? LoopState::READY : LoopState::NONE;
}

float loopPosition() {
	if (!phrase.hasPhrase() || !phrase.isPlaying()) {
		return 0.0f;
	}
	return static_cast<float>(phrase.position(loopNow())) / static_cast<float>(phrase.length());
}

int32_t loopNoteAt(float fraction) {
	if (!phrase.hasPhrase()) {
		return -1;
	}
	return phrase.noteAt(static_cast<uint32_t>(fraction * static_cast<float>(phrase.length())));
}

const PhraseLoop& loopPhrase() {
	return phrase;
}

void loadLoop(const PhraseEvent* events, int32_t count, uint32_t length) {
	phrase.load(events, count, length);
}

void playbackBegun() {
	if (phrase.isPlaying() && settings.loopTiming == LoopTiming::SONG) {
		phrase.start(loopNow(), settings.loopOneShot);
	}
}

int32_t historyNote(int32_t age) {
	if (settings.channel < 0 || age < 0 || age >= kHistoryLength) {
		return -1;
	}
	return history[kHistoryLength - 1 - age];
}

uint32_t historyVersion() {
	return historyChanges;
}

bool recentlyVoiced() {
	return voicedCountdown > 0;
}

} // namespace deluge::voice::runner
