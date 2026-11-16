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

#include "processing/engines/kuramoto_engine.h"
#include <cmath>

namespace deluge::kuramoto {

namespace {
constexpr uint32_t kSeed = 0x2545F491u;
constexpr float kTwoPi = 6.28318530718f;
constexpr float kPulseSeconds = 0.010f;
constexpr float kVoltCode = 6552.0f;
constexpr float kMaxCode = 65520.0f;
constexpr float kMinRate = 0.1f;
constexpr float kMaxRate = 16.0f;
constexpr float kMaxCoupling = 4.0f; ///< Hz of pull at coupling 1
constexpr float kMaxNoise = 0.5f;    ///< cycles per square root of a second at noise 1
constexpr float kMaxStepSeconds = 0.01f;

float clamp(float v, float lo, float hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

constexpr float kPresets[kNumPresets][kNumOscillators] = {
    {1.0f, 1.5f, 2.25f, 0.75f}, // spread
    {1.0f, 1.1f, 1.2f, 0.9f},   // close
    {0.5f, 1.0f, 1.5f, 2.0f},   // harmonic
    {1.5f, 1.5f, 1.5f, 1.5f},   // equal
};
char const* const kPresetNames[kNumPresets] = {"Spread", "Close", "Harmonic", "Equal"};
} // namespace

uint32_t KuramotoEngine::nextRandom() {
	rng_ ^= rng_ << 13;
	rng_ ^= rng_ >> 17;
	rng_ ^= rng_ << 5;
	return rng_;
}

float KuramotoEngine::random01() {
	return static_cast<float>(nextRandom() >> 8) * (1.0f / 16777216.0f);
}

float KuramotoEngine::gaussian() {
	float sum = random01() + random01() + random01() + random01() - 2.0f;
	return sum * 1.7320508f;
}

void KuramotoEngine::reset() {
	rng_ = kSeed;
	for (int32_t i = 0; i < kNumOscillators; i++) {
		phase_[i] = 0.25f * static_cast<float>(i); // spread evenly, so the order starts at zero
		owed_[i] = 0;
		pulse_[i] = 0.0f;
		wraps_[i] = 0;
	}
	totalWraps_ = 0;
	updateOrder();
}

void KuramotoEngine::setParams(const Params& p) {
	params_ = p;
	for (int32_t i = 0; i < kNumOscillators; i++) {
		setRate(i, p.rate[i]);
	}
	setCoupling(p.coupling);
	setNoise(p.noise);
	setRange(p.rangeVolts);
}

void KuramotoEngine::setRate(int32_t oscillator, float hz) {
	if (oscillator >= 0 && oscillator < kNumOscillators) {
		params_.rate[oscillator] = clamp(hz, kMinRate, kMaxRate);
	}
}

void KuramotoEngine::setCoupling(float v) {
	params_.coupling = clamp(v, 0.0f, 1.0f);
}

void KuramotoEngine::setNoise(float v) {
	params_.noise = clamp(v, 0.0f, 1.0f);
}

void KuramotoEngine::setRange(float volts) {
	params_.rangeVolts = clamp(volts, 1.0f, 10.0f);
}

void KuramotoEngine::setTimeMode(TimeMode m) {
	params_.timeMode = m;
}

void KuramotoEngine::applyPreset(int32_t preset) {
	if (preset < 0 || preset >= kNumPresets) {
		return;
	}
	for (int32_t i = 0; i < kNumOscillators; i++) {
		setRate(i, kPresets[preset][i]);
	}
}

char const* KuramotoEngine::presetName(int32_t preset) {
	return (preset >= 0 && preset < kNumPresets) ? kPresetNames[preset] : "";
}

void KuramotoEngine::randomiseRates() {
	for (int32_t i = 0; i < kNumOscillators; i++) {
		setRate(i, 0.5f + 2.5f * random01());
	}
}

void KuramotoEngine::updateOrder() {
	float c = 0.0f;
	float s = 0.0f;
	for (int32_t i = 0; i < kNumOscillators; i++) {
		c += std::cos(kTwoPi * phase_[i]);
		s += std::sin(kTwoPi * phase_[i]);
	}
	c /= static_cast<float>(kNumOscillators);
	s /= static_cast<float>(kNumOscillators);
	order_ = clamp(std::sqrt(c * c + s * s), 0.0f, 1.0f);
	float mean = std::atan2(s, c) / kTwoPi;
	mean_ = mean < 0.0f ? mean + 1.0f : mean;
	if (mean_ >= 1.0f) {
		mean_ -= 1.0f;
	}
}

void KuramotoEngine::step(float dt, float bpm) {
	if (!(dt > 0.0f)) {
		return;
	}
	const float realDt = dt;
	if (params_.timeMode == TimeMode::TEMPO) {
		dt *= clamp(bpm, 1.0f, 999.0f) / 120.0f;
	}
	dt = dt > kMaxStepSeconds ? kMaxStepSeconds : dt;

	const float k = params_.coupling * kMaxCoupling / static_cast<float>(kNumOscillators);
	const float sigma = params_.noise * kMaxNoise * std::sqrt(dt);

	float next[kNumOscillators];
	for (int32_t i = 0; i < kNumOscillators; i++) {
		float pull = 0.0f;
		if (k > 0.0f) {
			for (int32_t j = 0; j < kNumOscillators; j++) {
				pull += std::sin(kTwoPi * (phase_[j] - phase_[i]));
			}
			pull *= k;
		}
		float p = phase_[i] + (params_.rate[i] + pull) * dt;
		if (sigma > 0.0f) {
			p += sigma * gaussian();
		}
		next[i] = p;
	}

	for (int32_t i = 0; i < kNumOscillators; i++) {
		float p = next[i];
		while (p >= 1.0f) {
			p -= 1.0f;
			if (owed_[i] > 0) {
				owed_[i]--; // this only undoes a backwards wrap, it isn't a new cycle
			}
			else {
				wraps_[i]++;
				totalWraps_++;
				pulse_[i] = kPulseSeconds;
			}
		}
		while (p < 0.0f) {
			p += 1.0f;
			if (owed_[i] < 100) {
				owed_[i]++;
			}
		}
		phase_[i] = p >= 1.0f ? 0.0f : p;
	}
	updateOrder();

	for (float& p : pulse_) {
		if (p > 0.0f) {
			p -= realDt;
			if (p < 0.0f) {
				p = 0.0f;
			}
		}
	}
}

float KuramotoEngine::getPhase(int32_t oscillator) const {
	return (oscillator >= 0 && oscillator < kNumOscillators) ? phase_[oscillator] : 0.0f;
}

uint32_t KuramotoEngine::getWrapCount(int32_t oscillator) const {
	return (oscillator >= 0 && oscillator < kNumOscillators) ? wraps_[oscillator] : 0;
}

float KuramotoEngine::getVolts(int32_t cv) const {
	if (cv == 0) {
		return order_ * params_.rangeVolts;
	}
	if (cv == 1) {
		return mean_ * params_.rangeVolts;
	}
	return 0.0f;
}

uint16_t KuramotoEngine::getCVCode(int32_t cv) const {
	float code = clamp(getVolts(cv) * kVoltCode + 0.5f, 0.0f, kMaxCode);
	return static_cast<uint16_t>(code);
}

uint8_t KuramotoEngine::getGateLevels() const {
	uint8_t levels = 0;
	for (int32_t i = 0; i < kNumOscillators; i++) {
		if (pulse_[i] > 0.0f) {
			levels |= static_cast<uint8_t>(1 << i);
		}
	}
	return levels;
}

} // namespace deluge::kuramoto
