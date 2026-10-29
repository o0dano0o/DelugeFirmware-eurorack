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

#include "processing/engines/chaos_runner.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"

namespace deluge::chaos::runner {

namespace {
constexpr float kStepSeconds = 0.002f;
constexpr float kSamplesPerSecond = 44100.0f;
constexpr int32_t kMaxStepsPerTick = 25; ///< if the task is starved, drop time rather than hog the CPU
constexpr float kFallbackBpm = 120.0f;

// Default periods: 12 / 30 / 90 seconds, or the same at 90 BPM expressed in beats
constexpr float kFreePeriods[kNumSloths] = {12.0f, 30.0f, 90.0f};
constexpr float kTempoPeriods[kNumSloths] = {18.0f, 45.0f, 135.0f};

ChaosEngine chaosEngine;
Mode mode = Mode::OFF;
bool tempoSync = false;
uint32_t lastSampleTimer = 0;
float pendingSeconds = 0.0f;
uint8_t lastGates = 0;

float currentBpm() {
	if (currentSong == nullptr) {
		return kFallbackBpm;
	}
	return playbackHandler.calculateBPMForDisplay();
}

void writeOutputs() {
	for (uint8_t ch = 0; ch < kNumCVOutputs; ch++) {
		cvEngine.sendChaosVoltage(ch, chaosEngine.getCVCode(ch));
	}
	if (mode == Mode::CV_AND_GATES) {
		uint8_t gates = chaosEngine.getGateLevels();
		uint8_t changed = gates ^ lastGates;
		lastGates = gates;
		for (uint8_t g = 0; g < kNumGateRules; g++) {
			if (changed & (1 << g)) {
				cvEngine.sendChaosGate(g, gates & (1 << g));
			}
		}
	}
}
} // namespace

ChaosEngine& engine() {
	return chaosEngine;
}

Mode getMode() {
	return mode;
}

bool isTempoSync() {
	return tempoSync;
}

void setTempoSync(bool sync) {
	if (sync == tempoSync) {
		return;
	}
	tempoSync = sync;
	chaosEngine.setTimeMode(sync ? TimeMode::TEMPO : TimeMode::FREE);
	for (int32_t i = 0; i < kNumSloths; i++) {
		chaosEngine.setPeriod(i, sync ? kTempoPeriods[i] : kFreePeriods[i]);
	}
}

void setMode(Mode newMode) {
	if (newMode == mode) {
		return;
	}
	bool wasOff = (mode == Mode::OFF);
	mode = newMode;

	if (newMode == Mode::OFF) {
		cvEngine.setChaosOwnedGates(0);
		cvEngine.setChaosOwnedCV(0);
		return;
	}

	if (wasOff) {
		chaosEngine.reset();
		lastSampleTimer = AudioEngine::audioSampleTimer;
		pendingSeconds = 0.0f;
		cvEngine.setChaosOwnedCV(0b11);
		chaosEngine.step(kStepSeconds, currentBpm());
	}

	if (newMode == Mode::CV_AND_GATES) {
		cvEngine.setChaosOwnedGates(0b1111);
		// start from a known state: every owned gate low, then bring up whichever rules are high
		lastGates = 0;
	}
	else {
		cvEngine.setChaosOwnedGates(0);
	}
	writeOutputs();
}

void playbackBegun() {
	if (mode == Mode::OFF) {
		return;
	}
	chaosEngine.reset();
	pendingSeconds = 0.0f;
	lastSampleTimer = AudioEngine::audioSampleTimer;
}

void tick() {
	if (mode == Mode::OFF) {
		return;
	}
	uint32_t now = AudioEngine::audioSampleTimer;
	uint32_t elapsed = now - lastSampleTimer;
	lastSampleTimer = now;
	pendingSeconds += static_cast<float>(elapsed) / kSamplesPerSecond;

	float bpm = currentBpm();
	int32_t steps = 0;
	while (pendingSeconds >= kStepSeconds) {
		pendingSeconds -= kStepSeconds;
		if (steps < kMaxStepsPerTick) {
			chaosEngine.step(kStepSeconds, bpm);
			steps++;
		}
	}
	if (steps > 0) {
		writeOutputs();
	}
}

} // namespace deluge::chaos::runner
