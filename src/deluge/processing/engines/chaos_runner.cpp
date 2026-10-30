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
uint8_t gateMask = 0; ///< bit g set: gate g is driven by chaos rule g
uint8_t cvMask = 0;   ///< bit c set: CV c is driven by chaos
bool tempoSync = false;
uint32_t lastSampleTimer = 0;
float pendingSeconds = 0.0f;
uint8_t lastGates = 0;
uint32_t lastRise[kNumGateRules] = {};
bool everRose[kNumGateRules] = {};

float currentBpm() {
	if (currentSong == nullptr) {
		return kFallbackBpm;
	}
	return playbackHandler.calculateBPMForDisplay();
}

void writeOutputs() {
	for (uint8_t ch = 0; ch < kNumCVOutputs; ch++) {
		if (cvMask & (1 << ch)) {
			cvEngine.sendChaosVoltage(ch, chaosEngine.getCVCode(ch));
		}
	}
	uint8_t gates = chaosEngine.getGateLevels();
	uint8_t changed = gates ^ lastGates;
	lastGates = gates;
	for (uint8_t g = 0; g < kNumGateRules; g++) {
		if ((changed & gates) & (1 << g)) {
			lastRise[g] = AudioEngine::audioSampleTimer;
			everRose[g] = true;
		}
		if ((changed & (1 << g)) && (gateMask & (1 << g))) {
			cvEngine.sendChaosGate(g, gates & (1 << g));
		}
	}
}

/// Hands the masks to the CV engine. Starts the chaos running if this is the first output to use it.
void apply(bool wasActive) {
	uint8_t oldGates = cvEngine.getChaosOwnedGates();
	cvEngine.setChaosOwnedCV(cvMask);
	cvEngine.setChaosOwnedGates(gateMask);
	// gates the CV engine refused (run/clock) aren't ours
	gateMask = cvEngine.getChaosOwnedGates();

	if (!wasActive && (cvMask || gateMask)) {
		chaosEngine.reset();
		lastSampleTimer = AudioEngine::audioSampleTimer;
		pendingSeconds = 0.0f;
		chaosEngine.step(kStepSeconds, currentBpm());
		lastGates = 0;
	}
	// a newly owned gate must be sent its current level
	lastGates &= oldGates;
	if (cvMask || gateMask) {
		writeOutputs();
	}
}
} // namespace

bool gateRecentlyHigh(int32_t gate, int32_t holdMs) {
	if (gate < 0 || gate >= kNumGateRules) {
		return false;
	}
	if (chaosEngine.getGateLevels() & (1 << gate)) {
		return true;
	}
	return everRose[gate] && (AudioEngine::audioSampleTimer - lastRise[gate]) < static_cast<uint32_t>(holdMs * 44);
}

float getBpm() {
	return currentBpm();
}

ChaosEngine& engine() {
	return chaosEngine;
}

bool isActive() {
	return cvMask || gateMask;
}

bool isGateChaos(int32_t gate) {
	return gateMask & (1 << gate);
}

bool isCVChaos(int32_t cv) {
	return cvMask & (1 << cv);
}

void setGateChaos(int32_t gate, bool chaos) {
	if (gate < 0 || gate >= kNumGateRules) {
		return;
	}
	bool was = isActive();
	gateMask = chaos ? (gateMask | (1 << gate)) : (gateMask & ~(1 << gate));
	apply(was);
}

void setCVChaos(int32_t cv, bool chaos) {
	if (cv < 0 || cv >= kNumCVOutputs) {
		return;
	}
	bool was = isActive();
	cvMask = chaos ? (cvMask | (1 << cv)) : (cvMask & ~(1 << cv));
	apply(was);
}

void setAll(bool chaos) {
	bool was = isActive();
	gateMask = chaos ? 0b1111 : 0;
	cvMask = chaos ? 0b11 : 0;
	apply(was);
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

Settings getSettings() {
	Settings s;
	s.model = chaosEngine.getModel();
	s.tempoSync = tempoSync;
	for (int32_t i = 0; i < kNumSloths; i++) {
		s.periods[i] = chaosEngine.getPeriod(i);
	}
	s.coupling = chaosEngine.getCoupling();
	s.gateMask = gateMask;
	s.cvMask = cvMask;
	return s;
}

void loadSettings(const Settings& s) {
	setAll(false);
	chaosEngine.setModel(s.model);
	setTempoSync(s.tempoSync);
	for (int32_t i = 0; i < kNumSloths; i++) {
		chaosEngine.setPeriod(i, s.periods[i]);
	}
	chaosEngine.setCoupling(s.coupling);
	chaosEngine.reset();
	for (int32_t g = 0; g < kNumGateRules; g++) {
		if (s.gateMask & (1 << g)) {
			setGateChaos(g, true);
		}
	}
	for (int32_t c = 0; c < kNumCVOutputs; c++) {
		if (s.cvMask & (1 << c)) {
			setCVChaos(c, true);
		}
	}
}

void resetToDefaults() {
	loadSettings(Settings{});
}

void playbackBegun() {
	if (!isActive()) {
		return;
	}
	chaosEngine.reset();
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
			chaosEngine.step(kStepSeconds, bpm);
			steps++;
		}
	}
	if (steps > 0) {
		writeOutputs();
	}
}

} // namespace deluge::chaos::runner
