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
#include "processing/engines/chaos_probability.h"
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
uint8_t probSource[kNumGateRules] = {0, 0, 0, 0};
float probDepth = 0.5f;
probability::Roller roller;
uint32_t lastSampleTimer = 0;
float pendingSeconds = 0.0f;
uint8_t lastGates = 0;
uint8_t prevGateMask = 0; ///< gateMask as of the last apply(), to tell which gates were just claimed
uint8_t extCV = 0;        ///< channels claimed by another generator
uint8_t extGate = 0;
bool engineRequested = false;
uint32_t riseCount[kNumGateRules] = {};
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
			riseCount[g]++;
		}
		if ((changed & (1 << g)) && (gateMask & (1 << g))) {
			cvEngine.sendChaosGate(g, gates & (1 << g));
		}
	}
}

bool anyProb() {
	for (uint8_t src : probSource) {
		if (src != 0) {
			return true;
		}
	}
	return false;
}

bool engineWanted() {
	return cvMask || gateMask || engineRequested || anyProb();
}

/// Hands the masks to the CV engine. Starts the chaos engine running if this is the first thing to want it.
void apply(bool wasRunning) {
	cvMask &= ~extCV;
	gateMask &= ~extGate;
	cvEngine.setChaosOwnedCV(cvMask | extCV);
	cvEngine.setChaosOwnedGates(gateMask | extGate);
	// gates the CV engine refused (run/clock) aren't ours
	gateMask = cvEngine.getChaosOwnedGates() & ~extGate;

	if (!wasRunning && engineWanted()) {
		chaosEngine.reset();
		roller.reset();
		lastSampleTimer = AudioEngine::audioSampleTimer;
		pendingSeconds = 0.0f;
		chaosEngine.step(kStepSeconds, currentBpm());
		lastGates = 0;
	}
	// a newly owned gate must be sent its current level
	lastGates &= prevGateMask;
	prevGateMask = gateMask;
	if (engineWanted()) {
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

bool inUse() {
	return engineWanted();
}

void setProbSource(int32_t gate, uint8_t source) {
	if (gate < 0 || gate >= kNumGateRules || source > kNumSloths) {
		return;
	}
	bool was = engineWanted();
	probSource[gate] = source;
	apply(was);
}

uint8_t getProbSource(int32_t gate) {
	return (gate < 0 || gate >= kNumGateRules) ? 0 : probSource[gate];
}

void setProbDepth(float depth) {
	probDepth = depth < 0.0f ? 0.0f : (depth > 1.0f ? 1.0f : depth);
}

float getProbDepth() {
	return probDepth;
}

bool allowNoteOn(int32_t gate) {
	if (gate < 0 || gate >= kNumGateRules || probSource[gate] == 0) {
		return true;
	}
	float x = chaosEngine.getNormalised(probSource[gate] - 1, 0);
	return roller.roll(probability::passProbability(x, probDepth));
}

void setExternalOwnership(uint8_t cvBits, uint8_t gateBits) {
	bool was = engineWanted();
	extCV = cvBits & ((1 << kNumCVOutputs) - 1);
	extGate = gateBits & ((1 << kNumGateRules) - 1);
	apply(was);
}

void requestEngine(bool wanted) {
	bool was = engineWanted();
	engineRequested = wanted;
	apply(was);
}

uint32_t gateRiseCount(int32_t gate) {
	return (gate >= 0 && gate < kNumGateRules) ? riseCount[gate] : 0;
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
	bool was = engineWanted();
	gateMask = chaos ? (gateMask | (1 << gate)) : (gateMask & ~(1 << gate));
	apply(was);
}

void setCVChaos(int32_t cv, bool chaos) {
	if (cv < 0 || cv >= kNumCVOutputs) {
		return;
	}
	bool was = engineWanted();
	cvMask = chaos ? (cvMask | (1 << cv)) : (cvMask & ~(1 << cv));
	apply(was);
}

void setAll(bool chaos) {
	bool was = engineWanted();
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
	for (int32_t g = 0; g < kNumGateRules; g++) {
		s.probSource[g] = probSource[g];
	}
	s.probDepth = probDepth;
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
	roller.reset();
	lastSampleTimer = AudioEngine::audioSampleTimer;
	pendingSeconds = 0.0f;
	for (int32_t g = 0; g < kNumGateRules; g++) {
		probSource[g] = s.probSource[g] > kNumSloths ? 0 : s.probSource[g];
	}
	setProbDepth(s.probDepth);
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
	if (!engineWanted()) {
		return;
	}
	chaosEngine.reset();
	roller.reset();
	pendingSeconds = 0.0f;
	lastSampleTimer = AudioEngine::audioSampleTimer;
}

void tick() {
	if (!engineWanted()) {
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
