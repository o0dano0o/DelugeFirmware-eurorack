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

#include "processing/engines/rungler_engine.h"
#include <cmath>

namespace deluge::rungler {

namespace {
constexpr uint32_t kSeed = 0x6D2B79F5u;
constexpr float kPulseSeconds = 0.010f;
constexpr float kVoltCode = 6552.0f;
constexpr float kMaxCode = 65520.0f;
constexpr float kMinHz = 0.05f;
constexpr float kMaxHz = 100.0f;
constexpr float kMaxSlewSeconds = 0.5f;
constexpr float kModOctaves = 4.0f; ///< at full mod the register bends a rate by up to plus or minus two octaves

float clamp(float v, float lo, float hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

uint32_t RunglerEngine::nextRandom() {
	rng_ ^= rng_ << 13;
	rng_ ^= rng_ >> 17;
	rng_ ^= rng_ << 5;
	return rng_;
}

float RunglerEngine::random01() {
	return static_cast<float>(nextRandom() >> 8) * (1.0f / 16777216.0f);
}

void RunglerEngine::reset() {
	rng_ = kSeed;
	register_ = static_cast<uint8_t>(nextRandom() & 0xFF);
	if (register_ == 0) {
		register_ = 0x5A; // an empty register would stay empty
	}
	phaseA_ = 0.0f;
	phaseB_ = 0.0f;
	shiftCount_ = 0;
	pulseTime_ = 0.0f;
	float top = params_.rangeVolts;
	out_[0] = static_cast<float>(register_ & 7) / 7.0f * top;
	out_[1] = static_cast<float>(register_) / 255.0f * top;
}

void RunglerEngine::setParams(const Params& p) {
	params_ = p;
	setChance(p.chance);
	setModAmount(p.modAmount);
	setSlew(p.slew);
	setRange(p.rangeVolts);
	setFreqA(p.freqA);
	setFreqB(p.freqB);
	setRateA(p.rateA);
	setRateB(p.rateB);
}

void RunglerEngine::setChance(float v) {
	params_.chance = clamp(v, 0.0f, 1.0f);
}
void RunglerEngine::setModAmount(float v) {
	params_.modAmount = clamp(v, 0.0f, 1.0f);
}
void RunglerEngine::setSlew(float v) {
	params_.slew = clamp(v, 0.0f, 1.0f);
}
void RunglerEngine::setRange(float volts) {
	params_.rangeVolts = clamp(volts, 1.0f, 10.0f);
}
void RunglerEngine::setTimeMode(TimeMode m) {
	params_.timeMode = m;
}
void RunglerEngine::setFreqA(float hz) {
	params_.freqA = clamp(hz, kMinHz, kMaxHz);
}
void RunglerEngine::setFreqB(float hz) {
	params_.freqB = clamp(hz, kMinHz, kMaxHz);
}
void RunglerEngine::setRateA(int32_t index) {
	params_.rateA = static_cast<uint8_t>(index < 0 ? 0 : (index >= marbles::kNumRates ? marbles::kNumRates - 1 : index));
}
void RunglerEngine::setRateB(int32_t index) {
	params_.rateB = static_cast<uint8_t>(index < 0 ? 0 : (index >= marbles::kNumRates ? marbles::kNumRates - 1 : index));
}

void RunglerEngine::setRegister(uint8_t bits) {
	register_ = bits;
}

void RunglerEngine::toggleBit(int32_t index) {
	if (index >= 0 && index < 8) {
		register_ ^= static_cast<uint8_t>(1 << index);
	}
}

float RunglerEngine::baseRate(bool isA, float bpm) const {
	if (params_.timeMode == TimeMode::FREE) {
		return isA ? params_.freqA : params_.freqB;
	}
	float b = clamp(bpm, 1.0f, 999.0f);
	return b / 60.0f * marbles::stepsPerBeat(isA ? params_.rateA : params_.rateB);
}

float RunglerEngine::getRateA(float bpm) const {
	float v = static_cast<float>(register_) / 255.0f;
	float hz = baseRate(true, bpm) * std::exp2((v - 0.5f) * kModOctaves * params_.modAmount);
	return clamp(hz, kMinHz, kMaxHz * 2.0f);
}

float RunglerEngine::getRateB(float bpm) const {
	// B bends the opposite way, so a rising register speeds A up and slows the clock down
	float v = static_cast<float>(register_) / 255.0f;
	float hz = baseRate(false, bpm) * std::exp2((0.5f - v) * kModOctaves * params_.modAmount);
	return clamp(hz, kMinHz, kMaxHz * 2.0f);
}

void RunglerEngine::shift() {
	uint8_t oldest = (register_ >> 7) & 1;
	uint8_t incoming = oldest;
	if (random01() < params_.chance) {
		incoming = oldest ^ (isAHigh() ? 1 : 0);
	}
	register_ = static_cast<uint8_t>((register_ << 1) | incoming);
	shiftCount_++;
	pulseTime_ = kPulseSeconds;
}

void RunglerEngine::step(float dt, float bpm) {
	if (!(dt > 0.0f)) {
		return;
	}
	float rateA = getRateA(bpm);
	float rateB = getRateB(bpm);

	phaseA_ += rateA * dt;
	while (phaseA_ >= 1.0f) {
		phaseA_ -= 1.0f;
	}
	phaseB_ += rateB * dt;
	int32_t guard = 0;
	while (phaseB_ >= 1.0f && guard < 8) {
		phaseB_ -= 1.0f;
		shift();
		guard++;
	}
	if (phaseB_ >= 1.0f) {
		phaseB_ = 0.0f;
	}

	if (pulseTime_ > 0.0f) {
		pulseTime_ -= dt;
		if (pulseTime_ < 0.0f) {
			pulseTime_ = 0.0f;
		}
	}

	float top = params_.rangeVolts;
	out_[0] = static_cast<float>(register_ & 7) / 7.0f * top;
	float target = static_cast<float>(register_) / 255.0f * top;
	float tau = params_.slew * kMaxSlewSeconds;
	if (tau <= 0.0f) {
		out_[1] = target;
	}
	else {
		out_[1] += (target - out_[1]) * (1.0f - std::exp(-dt / tau));
	}
}

float RunglerEngine::getVolts(int32_t cv) const {
	return (cv >= 0 && cv < kNumCV) ? out_[cv] : 0.0f;
}

uint16_t RunglerEngine::getCVCode(int32_t cv) const {
	float code = clamp(getVolts(cv) * kVoltCode + 0.5f, 0.0f, kMaxCode);
	return static_cast<uint16_t>(code);
}

uint8_t RunglerEngine::getGateLevels() const {
	uint8_t levels = pulseTime_ > 0.0f ? 1 : 0;
	levels |= static_cast<uint8_t>((register_ & 7) << 1);
	return levels;
}

} // namespace deluge::rungler
