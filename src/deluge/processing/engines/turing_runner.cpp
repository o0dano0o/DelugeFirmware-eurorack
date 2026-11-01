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

#include "processing/engines/turing_runner.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/chaos_runner.h"
#include "processing/engines/cv_engine.h"
#include <algorithm>
#include <cmath>

namespace deluge::turing::runner {

namespace {

constexpr float kSamplesPerMs = 44.1f;
constexpr int32_t kBaseNote = 36;          ///< note code at the root (C2) before the channel's own transpose
constexpr int32_t kMinGateSamples = 5 * 44; ///< 5 ms
constexpr int32_t kMaxGateSamples = 500 * 44;
constexpr int32_t kDefaultGateSamples = 10 * 44;

/// Division table, in sixteenth notes
constexpr int32_t kDivisionSixteenths[kNumDivisions] = {1, 2, 3, 4, 6, 8, 16, 32, 64};
char const* const kDivisionNames[kNumDivisions] = {"1/16", "1/8", "1/8.", "1/4", "1/4.", "1/2", "1bar", "2bar", "4bar"};

TuringEngine turingEngine;
Settings settings;
uint32_t rngState = 0x2545F491u;

int64_t lastSongStep = -1;
uint32_t lastChaosRiseCount = 0;
uint32_t stepCount = 0;
uint32_t lastStepTimer = 0;
bool gateHigh = false;
uint32_t gateOffAt = 0;
bool haveLastStep = false;
uint8_t history[kHistoryLength] = {};
uint32_t historyCount = 0;

float nextRandomFromRng() {
	rngState ^= rngState << 13;
	rngState ^= rngState >> 17;
	rngState ^= rngState << 5;
	return static_cast<float>(rngState >> 8) * (1.0f / 16777216.0f);
}

/// A number in [0, 1) from the chaos state. The fractional part of a large multiple of a chaotic value is spread
/// evenly, and the value is different at every clock because the sloths keep moving.
float nextRandomFromChaos() {
	auto& chaos = deluge::chaos::runner::engine();
	float v = chaos.getNormalised(0, 0) * 997.0f + chaos.getNormalised(1, 2) * 331.0f
	          + chaos.getNormalised(2, 0) * 113.0f + chaos.getNormalised(0, 2) * 53.0f;
	v = std::fabs(v);
	return v - std::floor(v);
}

void applyEngineSettings() {
	turingEngine.setLength(settings.length);
	turingEngine.setFlipProbability(settings.flipProbability);
	if (!settings.hasRegister) {
		turingEngine.randomise(AudioEngine::audioSampleTimer * 2654435761u + 1u);
		settings.hasRegister = true;
	}
	settings.shiftRegister = turingEngine.getRegister();
}

bool needsChaos() {
	return settings.channel >= 0
	       && (settings.clock != ClockSource::SONG || settings.random == RandomSource::CHAOS);
}

void updateChaosRequest() {
	deluge::chaos::runner::requestEngine(needsChaos());
}

void releaseGate() {
	if (gateHigh && settings.channel >= 0) {
		cvEngine.sendChaosGate(settings.channel, false);
	}
	gateHigh = false;
}

void outputCurrentNote() {
	if (settings.channel < 0 || currentSong == nullptr) {
		return;
	}
	uint16_t scaleBits = currentSong->key.modeNotes.toBits();
	int32_t root = ((currentSong->key.rootNote % 12) + 12) % 12;
	int32_t offset = TuringEngine::noteOffsetFromValue(turingEngine.getValue(), scaleBits, settings.octaves);
	int32_t note = kBaseNote + root + offset;
	int32_t voltage = cvEngine.calculateVoltage(note, settings.channel);
	voltage = std::max<int32_t>(0, std::min<int32_t>(voltage, 65535));
	cvEngine.sendChaosVoltage(settings.channel, static_cast<uint16_t>(voltage));
}

void doStep() {
	float rnd = (settings.random == RandomSource::CHAOS) ? nextRandomFromChaos() : nextRandomFromRng();
	turingEngine.clock(rnd);
	settings.shiftRegister = turingEngine.getRegister();
	stepCount++;
	history[historyCount % kHistoryLength] = turingEngine.getValue();
	historyCount++;

	uint32_t now = AudioEngine::audioSampleTimer;
	// gate length: half the time since the last step, so it scales with the clock, within sensible limits
	int32_t gateSamples = kDefaultGateSamples;
	if (haveLastStep) {
		gateSamples = static_cast<int32_t>((now - lastStepTimer) / 2);
		gateSamples = std::max(kMinGateSamples, std::min(gateSamples, kMaxGateSamples));
	}
	haveLastStep = true;
	lastStepTimer = now;

	outputCurrentNote();
	cvEngine.sendChaosGate(settings.channel, true);
	gateHigh = true;
	gateOffAt = now + gateSamples;
}

void claimChannel() {
	if (settings.channel >= 0) {
		deluge::chaos::runner::setExternalOwnership(1 << settings.channel, 1 << settings.channel);
	}
	else {
		deluge::chaos::runner::setExternalOwnership(0, 0);
	}
}

} // namespace

char const* divisionName(int32_t division) {
	return kDivisionNames[std::max<int32_t>(0, std::min<int32_t>(division, kNumDivisions - 1))];
}

TuringEngine& engine() {
	return turingEngine;
}

Settings getSettings() {
	settings.shiftRegister = turingEngine.getRegister();
	return settings;
}

void loadSettings(const Settings& s) {
	setChannel(-1);
	settings = s;
	settings.channel = -1;
	settings.length = static_cast<uint8_t>(std::max<int32_t>(kMinLength, std::min<int32_t>(s.length, kMaxLength)));
	settings.octaves = static_cast<uint8_t>(std::max<int32_t>(kMinOctaves, std::min<int32_t>(s.octaves, kMaxOctaves)));
	settings.division = static_cast<uint8_t>(std::min<int32_t>(s.division, kNumDivisions - 1));
	applyEngineSettings();
	if (s.hasRegister) {
		turingEngine.setRegister(s.shiftRegister);
		settings.shiftRegister = turingEngine.getRegister();
	}
	setChannel(s.channel);
}

void resetToDefaults() {
	Settings defaults;
	loadSettings(defaults);
}

void setChannel(int32_t channel) {
	int8_t wanted = (channel == 0 || channel == 1) ? static_cast<int8_t>(channel) : static_cast<int8_t>(-1);
	if (wanted == settings.channel) {
		return;
	}
	releaseGate();
	settings.channel = wanted;
	claimChannel();
	updateChaosRequest();
	if (wanted >= 0) {
		applyEngineSettings();
		lastSongStep = -1;
		lastChaosRiseCount = deluge::chaos::runner::gateRiseCount(
		    settings.clock == ClockSource::CHAOS_LOBE ? 1 : 2);
		haveLastStep = false;
		outputCurrentNote();
	}
}

int32_t getChannel() {
	return settings.channel;
}

bool isActive() {
	return settings.channel >= 0;
}

void setClockSource(ClockSource source) {
	settings.clock = source;
	lastSongStep = -1;
	lastChaosRiseCount = deluge::chaos::runner::gateRiseCount(source == ClockSource::CHAOS_LOBE ? 1 : 2);
	updateChaosRequest();
}

ClockSource getClockSource() {
	return settings.clock;
}

void setDivision(int32_t division) {
	settings.division = static_cast<uint8_t>(std::max<int32_t>(0, std::min<int32_t>(division, kNumDivisions - 1)));
}

int32_t getDivision() {
	return settings.division;
}

void setRandomSource(RandomSource source) {
	settings.random = source;
	updateChaosRequest();
}

RandomSource getRandomSource() {
	return settings.random;
}

void setFlipProbability(float probability) {
	turingEngine.setFlipProbability(probability);
	settings.flipProbability = turingEngine.getFlipProbability();
}

void setLength(int32_t length) {
	turingEngine.setLength(length);
	settings.length = static_cast<uint8_t>(turingEngine.getLength());
	settings.shiftRegister = turingEngine.getRegister();
}

void setOctaves(int32_t octaves) {
	settings.octaves = static_cast<uint8_t>(std::max<int32_t>(kMinOctaves, std::min<int32_t>(octaves, kMaxOctaves)));
	outputCurrentNote();
}

int32_t getOctaves() {
	return settings.octaves;
}

void randomiseRegister() {
	turingEngine.randomise(AudioEngine::audioSampleTimer * 2654435761u + stepCount * 40503u + 1u);
	settings.shiftRegister = turingEngine.getRegister();
	settings.hasRegister = true;
	outputCurrentNote();
}

uint8_t getHistoryValue(int32_t age) {
	if (age < 0 || age >= kHistoryLength || static_cast<uint32_t>(age) >= historyCount) {
		return 0;
	}
	return history[(historyCount - 1 - age) % kHistoryLength];
}

uint32_t getStepCount() {
	return stepCount;
}

bool stepRecently(int32_t holdMs) {
	return stepCount != 0 && (AudioEngine::audioSampleTimer - lastStepTimer) < static_cast<uint32_t>(holdMs * kSamplesPerMs);
}

void playbackBegun() {
	lastSongStep = -1;
}

void tick() {
	if (settings.channel < 0) {
		return;
	}

	// end of the gate
	if (gateHigh && static_cast<int32_t>(AudioEngine::audioSampleTimer - gateOffAt) >= 0) {
		releaseGate();
	}

	switch (settings.clock) {
	case ClockSource::SONG: {
		if (currentSong == nullptr || !playbackHandler.isEitherClockActive()) {
			break;
		}
		int64_t ticks = playbackHandler.getCurrentInternalTickCount();
		int64_t stepTicks = std::max<int64_t>(
		    1, static_cast<int64_t>(currentSong->getQuarterNoteLength()) * kDivisionSixteenths[settings.division] / 4);
		int64_t index = (ticks >= 0) ? ticks / stepTicks : -1;
		if (index != lastSongStep && index >= 0) {
			lastSongStep = index;
			doStep();
		}
		break;
	}
	case ClockSource::CHAOS_LOBE:
	case ClockSource::CHAOS_LOOP: {
		int32_t rule = (settings.clock == ClockSource::CHAOS_LOBE) ? 1 : 2;
		uint32_t count = deluge::chaos::runner::gateRiseCount(rule);
		if (count != lastChaosRiseCount) {
			lastChaosRiseCount = count;
			doStep();
		}
		break;
	}
	default:
		break;
	}
}

} // namespace deluge::turing::runner
