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

#include "processing/engines/rungler_runner.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"

namespace deluge::rungler::runner {

using deluge::owners::Owner;

namespace {
constexpr float kStepSeconds = 0.002f;
constexpr float kSamplesPerSecond = 44100.0f;
constexpr int32_t kMaxStepsPerTick = 25; ///< if the task is starved, drop time rather than hog the CPU
constexpr float kFallbackBpm = 120.0f;

RunglerEngine runglerEngine;
uint8_t cvMask = 0;
uint8_t gateMask = 0;
uint8_t lastGates = 0;
uint8_t prevGateMask = 0; ///< gateMask as of the last apply(), to tell which gates were just claimed
uint32_t lastSampleTimer = 0;
float pendingSeconds = 0.0f;
uint32_t lastRise[kNumGates] = {};
bool everRose[kNumGates] = {};
uint32_t lastClockTimer = 0;
uint32_t lastClockCount = 0;

float currentBpm() {
	if (currentSong == nullptr) {
		return kFallbackBpm;
	}
	return playbackHandler.calculateBPMForDisplay();
}

void writeOutputs() {
	for (uint8_t ch = 0; ch < kNumCV; ch++) {
		if (cvMask & (1 << ch)) {
			cvEngine.sendOwnedVoltage(Owner::RUNGLER, ch, runglerEngine.getCVCode(ch));
		}
	}
	uint8_t gates = runglerEngine.getGateLevels();
	uint8_t changed = gates ^ lastGates;
	lastGates = gates;
	for (uint8_t g = 0; g < kNumGates; g++) {
		if ((changed & gates) & (1 << g)) {
			lastRise[g] = AudioEngine::audioSampleTimer;
			everRose[g] = true;
		}
		if ((changed & (1 << g)) && (gateMask & (1 << g))) {
			cvEngine.sendOwnedGate(Owner::RUNGLER, g, gates & (1 << g));
		}
	}
	if (runglerEngine.getShiftCount() != lastClockCount) {
		lastClockCount = runglerEngine.getShiftCount();
		lastClockTimer = AudioEngine::audioSampleTimer;
	}
}

/// Called by the CV engine when another generator takes outputs from Rungler
void onOutputsLost(uint8_t cvLost, uint8_t gateLost) {
	cvMask &= ~cvLost;
	gateMask &= ~gateLost;
	lastGates &= gateMask;
	prevGateMask = gateMask;
}

/// Hands the masks to the CV engine
void apply(bool wasActive) {
	cvEngine.setLossHandler(Owner::RUNGLER, &onOutputsLost);
	// Rungler never takes an output from another generator; it only gets the ones that are free or already its own
	cvMask &= ~(cvEngine.getOwnedCV() & ~cvEngine.getOwnedCV(Owner::RUNGLER));
	gateMask &= ~(cvEngine.getOwnedGates() & ~cvEngine.getOwnedGates(Owner::RUNGLER));
	cvEngine.claimOutputs(Owner::RUNGLER, cvMask, gateMask);
	// gates the CV engine refused (run/clock) aren't ours
	gateMask = cvEngine.getOwnedGates(Owner::RUNGLER);
	cvMask = cvEngine.getOwnedCV(Owner::RUNGLER);

	if (!wasActive && (cvMask || gateMask)) {
		lastSampleTimer = AudioEngine::audioSampleTimer;
		pendingSeconds = 0.0f;
		lastGates = 0;
	}
	// a newly owned gate must be sent its current level
	lastGates &= prevGateMask;
	prevGateMask = gateMask;
	if (cvMask || gateMask) {
		writeOutputs();
	}
}
} // namespace

Settings getSettings() {
	Settings s;
	s.params = runglerEngine.params();
	s.cvMask = cvMask;
	s.gateMask = gateMask;
	return s;
}

void loadSettings(const Settings& s) {
	bool was = isActive();
	cvMask = 0;
	gateMask = 0;
	apply(was);
	runglerEngine.setParams(s.params);
	runglerEngine.reset();
	pendingSeconds = 0.0f;
	lastSampleTimer = AudioEngine::audioSampleTimer;
	cvMask = s.cvMask & ((1 << kNumCV) - 1);
	gateMask = s.gateMask & ((1 << kNumGates) - 1);
	apply(false);
}

void resetToDefaults() {
	loadSettings(Settings{});
}

bool isActive() {
	return cvMask || gateMask;
}

bool isCVRungler(int32_t cv) {
	return cv >= 0 && cv < kNumCV && (cvMask & (1 << cv));
}

bool isGateRungler(int32_t gate) {
	return gate >= 0 && gate < kNumGates && (gateMask & (1 << gate));
}

void setCVRungler(int32_t cv, bool on) {
	if (cv < 0 || cv >= kNumCV) {
		return;
	}
	bool was = isActive();
	cvMask = on ? (cvMask | (1 << cv)) : (cvMask & ~(1 << cv));
	apply(was);
}

void setGateRungler(int32_t gate, bool on) {
	if (gate < 0 || gate >= kNumGates) {
		return;
	}
	bool was = isActive();
	gateMask = on ? (gateMask | (1 << gate)) : (gateMask & ~(1 << gate));
	apply(was);
}

void setAll(bool on) {
	bool was = isActive();
	cvMask = on ? ((1 << kNumCV) - 1) : 0;
	gateMask = on ? ((1 << kNumGates) - 1) : 0;
	apply(was);
}

RunglerEngine& engine() {
	return runglerEngine;
}

void setTempoSync(bool sync) {
	runglerEngine.setTimeMode(sync ? TimeMode::TEMPO : TimeMode::FREE);
}

bool isTempoSync() {
	return runglerEngine.params().timeMode == TimeMode::TEMPO;
}

bool gateRecentlyHigh(int32_t gate, int32_t holdMs) {
	if (gate < 0 || gate >= kNumGates) {
		return false;
	}
	if (runglerEngine.getGateLevels() & (1 << gate)) {
		return true;
	}
	return everRose[gate] && (AudioEngine::audioSampleTimer - lastRise[gate]) < static_cast<uint32_t>(holdMs * 44);
}

uint32_t getStepCount() {
	return runglerEngine.getShiftCount();
}

bool stepRecently(int32_t holdMs) {
	return runglerEngine.getShiftCount() > 0
	       && (AudioEngine::audioSampleTimer - lastClockTimer) < static_cast<uint32_t>(holdMs * 44);
}

void playbackBegun() {
	if (!isActive()) {
		return;
	}
	runglerEngine.reset();
	lastClockCount = 0;
	pendingSeconds = 0.0f;
	lastSampleTimer = AudioEngine::audioSampleTimer;
}

void tick() {
	if (!isActive()) {
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
			runglerEngine.step(kStepSeconds, bpm);
			steps++;
		}
	}
	if (steps > 0) {
		writeOutputs();
	}
}

} // namespace deluge::rungler::runner
