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

#include "processing/engines/marbles_engine.h"
#include <cmath>

namespace deluge::marbles {

namespace {
constexpr uint32_t kSeed = 0x2545F491u;
constexpr float kPulseSeconds = 0.010f;
constexpr int32_t kMaxClocksPerStep = 16;
constexpr float kVoltCode = 6552.0f;
constexpr float kMaxCode = 65520.0f;

constexpr float kStepsPerBeat[kNumRates] = {0.25f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f};
char const* const kRateNames[kNumRates] = {"1 bar", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};

float clamp(float v, float lo, float hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

float stepsPerBeat(int32_t rateIndex) {
	return kStepsPerBeat[rateIndex < 0 ? 0 : (rateIndex >= kNumRates ? kNumRates - 1 : rateIndex)];
}

char const* rateName(int32_t rateIndex) {
	return kRateNames[rateIndex < 0 ? 0 : (rateIndex >= kNumRates ? kNumRates - 1 : rateIndex)];
}

uint32_t MarblesEngine::nextRandom() {
	// xorshift32
	rng_ ^= rng_ << 13;
	rng_ ^= rng_ >> 17;
	rng_ ^= rng_ << 5;
	return rng_;
}

float MarblesEngine::random01() {
	// 24 bits so the result is always below 1
	return static_cast<float>(nextRandom() >> 8) * (1.0f / 16777216.0f);
}

void MarblesEngine::reset() {
	rng_ = kSeed;
	randomise();
	for (float& t : gateTime_) {
		t = 0.0f;
	}
	clockCount_ = 0;
	timeToNext_ = 0.0f;
	for (int32_t cv = 0; cv < kNumCV; cv++) {
		target_[cv] = shape(loop_[cv][0]);
		out_[cv] = target_[cv];
	}
}

void MarblesEngine::randomise() {
	for (int32_t cv = 0; cv < kNumCV; cv++) {
		for (int32_t i = 0; i < kMaxLength; i++) {
			loop_[cv][i] = random01();
		}
	}
	for (int32_t i = 0; i < kMaxLength; i++) {
		choice_[i] = random01();
	}
}

void MarblesEngine::setParams(const Params& p) {
	params_ = p;
	setDejaVu(p.dejaVu);
	for (int32_t cv = 0; cv < kNumCV; cv++) {
		setLength(cv, p.length[cv]);
	}
	setSpread(p.spread);
	setBias(p.bias);
	setSteps(p.steps);
	setRange(p.rangeVolts);
	setJitter(p.jitter);
	setBernoulli(p.bernoulli);
	setGateDivision(p.gateDivision);
	setTimeMode(p.timeMode);
	setFreePeriod(p.freePeriod);
	setRate(p.rate);
}

void MarblesEngine::setDejaVu(float v) {
	params_.dejaVu = clamp(v, 0.0f, 1.0f);
}

void MarblesEngine::setLength(int32_t cv, int32_t length) {
	if (cv < 0 || cv >= kNumCV) {
		return;
	}
	params_.length[cv] = static_cast<uint8_t>(length < 1 ? 1 : (length > kMaxLength ? kMaxLength : length));
}

void MarblesEngine::setSpread(float v) {
	params_.spread = clamp(v, 0.0f, 1.0f);
}

void MarblesEngine::setBias(float v) {
	params_.bias = clamp(v, 0.0f, 1.0f);
}

void MarblesEngine::setSteps(float v) {
	params_.steps = clamp(v, 0.0f, 1.0f);
}

void MarblesEngine::setRange(float volts) {
	params_.rangeVolts = clamp(volts, 1.0f, 10.0f);
}

void MarblesEngine::setJitter(float v) {
	params_.jitter = clamp(v, 0.0f, 1.0f);
}

void MarblesEngine::setBernoulli(float v) {
	params_.bernoulli = clamp(v, 0.0f, 1.0f);
}

void MarblesEngine::setGateDivision(int32_t n) {
	params_.gateDivision = static_cast<uint8_t>(n < 1 ? 1 : (n > 16 ? 16 : n));
}

void MarblesEngine::setTimeMode(TimeMode m) {
	params_.timeMode = m;
}

void MarblesEngine::setFreePeriod(float seconds) {
	params_.freePeriod = clamp(seconds, 0.02f, 20.0f);
}

void MarblesEngine::setRate(int32_t index) {
	params_.rate = static_cast<uint8_t>(index < 0 ? 0 : (index >= kNumRates ? kNumRates - 1 : index));
}

float MarblesEngine::shape(float u) const {
	// A stored number u (0 to 1) becomes a value. Spread blends a bell-shaped spread of values (most near the middle)
	// into an even one, bias leans the values low or high, and with spread 0 everything sits at the bias.
	float t = 2.0f * u - 1.0f;
	float bell = 0.5f + 0.5f * t * std::fabs(t);
	float s = clamp(bell + (u - bell) * params_.spread, 0.0f, 1.0f);
	float exponent = std::exp2(4.0f * (0.5f - params_.bias));
	float skewed = std::pow(s, exponent);
	return clamp(params_.bias + (skewed - params_.bias) * params_.spread, 0.0f, 1.0f);
}

float MarblesEngine::unshape(float value) const {
	value = clamp(value, 0.0f, 1.0f);
	if (params_.spread < 0.001f) {
		return value; // every number gives the same value, so nothing to match
	}
	float lo = 0.0f;
	float hi = 1.0f;
	for (int32_t i = 0; i < 24; i++) {
		float mid = 0.5f * (lo + hi);
		if (shape(mid) < value) {
			lo = mid;
		}
		else {
			hi = mid;
		}
	}
	return 0.5f * (lo + hi);
}

float MarblesEngine::getLoopValue(int32_t cv, int32_t index) const {
	if (cv < 0 || cv >= kNumCV || index < 0 || index >= kMaxLength) {
		return 0.0f;
	}
	return shape(loop_[cv][index]);
}

void MarblesEngine::setLoopValue(int32_t cv, int32_t index, float value) {
	if (cv < 0 || cv >= kNumCV || index < 0 || index >= kMaxLength) {
		return;
	}
	loop_[cv][index] = unshape(value);
}

int32_t MarblesEngine::getPosition(int32_t cv) const {
	if (cv < 0 || cv >= kNumCV || clockCount_ == 0) {
		return 0;
	}
	return static_cast<int32_t>((clockCount_ - 1) % params_.length[cv]);
}

float MarblesEngine::getVolts(int32_t cv) const {
	return out_[cv] * params_.rangeVolts;
}

uint16_t MarblesEngine::getCVCode(int32_t cv) const {
	float code = std::floor(getVolts(cv) * kVoltCode + 0.5f);
	return static_cast<uint16_t>(clamp(code, 0.0f, kMaxCode));
}

uint8_t MarblesEngine::getGateLevels() const {
	uint8_t levels = 0;
	for (int32_t g = 0; g < kNumGates; g++) {
		if (gateTime_[g] > 0.0f) {
			levels |= static_cast<uint8_t>(1 << g);
		}
	}
	return levels;
}

float MarblesEngine::getPeriod(float bpm) const {
	if (params_.timeMode == TimeMode::FREE) {
		return params_.freePeriod;
	}
	float b = clamp(bpm, 1.0f, 999.0f);
	return 60.0f / b / stepsPerBeat(params_.rate);
}

float MarblesEngine::nextInterval(float period) {
	if (params_.jitter <= 0.0f) {
		return period;
	}
	// wobble by up to half a step either way, scaled by the jitter amount
	float wobble = (random01() * 2.0f - 1.0f) * 0.5f * params_.jitter;
	return period * (1.0f + wobble);
}

void MarblesEngine::clockStep() {
	uint32_t index = clockCount_;
	clockCount_++;

	for (int32_t cv = 0; cv < kNumCV; cv++) {
		int32_t pos = static_cast<int32_t>(index % params_.length[cv]);
		// déjà vu is the chance a stored number is replayed; otherwise a new one replaces it
		if (random01() >= params_.dejaVu) {
			loop_[cv][pos] = random01();
		}
		target_[cv] = shape(loop_[cv][pos]);
	}

	int32_t choicePos = static_cast<int32_t>(index % params_.length[0]);
	if (random01() >= params_.dejaVu) {
		choice_[choicePos] = random01();
	}
	bool toGate2 = choice_[choicePos] < params_.bernoulli;

	float pulse = kPulseSeconds < 0.5f * lastPeriod_ ? kPulseSeconds : 0.5f * lastPeriod_;
	gateTime_[0] = pulse;
	gateTime_[toGate2 ? 1 : 2] = pulse;
	if (index % params_.gateDivision == 0) {
		gateTime_[3] = pulse;
	}
}

void MarblesEngine::step(float dt, float bpm) {
	if (!(dt > 0.0f)) {
		return;
	}
	float period = getPeriod(bpm);
	lastPeriod_ = period;

	timeToNext_ -= dt;
	int32_t clocks = 0;
	while (timeToNext_ <= 0.0f && clocks < kMaxClocksPerStep) {
		clockStep();
		timeToNext_ += nextInterval(period);
		clocks++;
	}
	if (timeToNext_ <= 0.0f) {
		timeToNext_ = period; // fell far behind; drop the backlog
	}

	for (float& t : gateTime_) {
		if (t > 0.0f) {
			t -= dt;
			if (t < 0.0f) {
				t = 0.0f;
			}
		}
	}

	// steps: 1 jumps to each new value, lower glides towards it over most of the step
	for (int32_t cv = 0; cv < kNumCV; cv++) {
		if (params_.steps >= 0.999f) {
			out_[cv] = target_[cv];
		}
		else {
			float tau = (1.0f - params_.steps) * 0.8f * period;
			float alpha = 1.0f - std::exp(-dt / (tau > 1e-6f ? tau : 1e-6f));
			out_[cv] += (target_[cv] - out_[cv]) * alpha;
		}
	}
}

} // namespace deluge::marbles
