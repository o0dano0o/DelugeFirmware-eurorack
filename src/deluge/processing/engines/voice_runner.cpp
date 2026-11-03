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

void releaseChannel(int32_t channel) {
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
	tracker.setLevelThreshold(kThresholds[settings.sensitivity]);
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
	releaseChannel(settings.channel);
	settings.channel = wanted;
	if (wanted >= 0) {
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

bool recentlyVoiced() {
	return voicedCountdown > 0;
}

} // namespace deluge::voice::runner
