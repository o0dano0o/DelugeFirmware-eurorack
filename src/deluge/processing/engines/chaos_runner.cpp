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
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"

namespace deluge::chaos::runner {

namespace {
constexpr float kStepSeconds = 0.002f;
constexpr float kSamplesPerSecond = 44100.0f;
constexpr int32_t kMaxStepsPerTick = 25; ///< if the task is starved, drop time rather than hog the CPU
constexpr float kDefaultBpm = 120.0f;    ///< only matters in TEMPO mode, which isn't hooked up yet

ChaosEngine chaosEngine;
bool enabled = false;
uint32_t lastSampleTimer = 0;
float pendingSeconds = 0.0f;
} // namespace

ChaosEngine& engine() {
	return chaosEngine;
}

bool isEnabled() {
	return enabled;
}

void setEnabled(bool on) {
	if (on == enabled) {
		return;
	}
	enabled = on;
	if (on) {
		chaosEngine.reset();
		lastSampleTimer = AudioEngine::audioSampleTimer;
		pendingSeconds = 0.0f;
		cvEngine.setChaosOwnedCV(0b11);
		// give the outputs a value straight away rather than waiting for the next tick
		chaosEngine.step(kStepSeconds, kDefaultBpm);
		for (uint8_t ch = 0; ch < kNumCVOutputs; ch++) {
			cvEngine.sendChaosVoltage(ch, chaosEngine.getCVCode(ch));
		}
	}
	else {
		cvEngine.setChaosOwnedCV(0);
	}
}

void tick() {
	if (!enabled) {
		return;
	}
	uint32_t now = AudioEngine::audioSampleTimer;
	uint32_t elapsed = now - lastSampleTimer;
	lastSampleTimer = now;
	pendingSeconds += static_cast<float>(elapsed) / kSamplesPerSecond;

	int32_t steps = 0;
	while (pendingSeconds >= kStepSeconds) {
		pendingSeconds -= kStepSeconds;
		if (steps < kMaxStepsPerTick) {
			chaosEngine.step(kStepSeconds, kDefaultBpm);
			steps++;
		}
	}
	if (steps > 0) {
		for (uint8_t ch = 0; ch < kNumCVOutputs; ch++) {
			cvEngine.sendChaosVoltage(ch, chaosEngine.getCVCode(ch));
		}
	}
}

} // namespace deluge::chaos::runner
