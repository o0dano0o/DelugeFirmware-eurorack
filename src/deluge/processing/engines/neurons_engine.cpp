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

#include "processing/engines/neurons_engine.h"
#include <cmath>

namespace deluge::neurons {

namespace {
constexpr uint32_t kSeed = 0x9E3779B9u;
constexpr float kPulseSeconds = 0.010f;
constexpr float kVoltCode = 6552.0f;
constexpr float kMaxCode = 65520.0f;
constexpr float kMinRate = 0.1f;
constexpr float kMaxRate = 16.0f;
constexpr float kMaxLeakPerSecond = 4.0f;
constexpr float kMaxNoise = 0.5f;  ///< membrane units per square root of a second at noise 1
constexpr float kKick = 0.5f;      ///< how far a weight of 1 moves a neuron's membrane
constexpr float kMaxStepSeconds = 0.01f;

float clamp(float v, float lo, float hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

/// Input current that makes a neuron with this leak fire at rate hz on its own
float drive(float hz, float leak) {
	if (leak < 1e-4f) {
		return hz;
	}
	return leak / (1.0f - std::exp(-leak / hz));
}

// presets: weight[target][source], in half steps
constexpr int8_t kPresets[kNumPresets][kNumNeurons][kNumNeurons] = {
    {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}, // independent
    {{0, 0, 0, 2}, {2, 0, 0, 0}, {0, 2, 0, 0}, {0, 0, 2, 0}}, // ring: each neuron excites the next
    {{0, -2, 0, 0}, {-2, 0, 0, 0}, {0, 0, 0, -2}, {0, 0, -2, 0}}, // pairs inhibit each other
    {{0, 0, 0, 0}, {2, 0, 0, 0}, {1, 2, 0, 0}, {0, 1, 2, 0}}, // cascade: earlier neurons push later ones
    {{0, 1, 1, 1}, {1, 0, 1, 1}, {1, 1, 0, 1}, {1, 1, 1, 0}}, // everyone excites everyone
};
char const* const kPresetNames[kNumPresets] = {"Independent", "Ring", "Pairs inhibit", "Cascade", "All excite"};
} // namespace

uint32_t NeuronsEngine::nextRandom() {
	rng_ ^= rng_ << 13;
	rng_ ^= rng_ >> 17;
	rng_ ^= rng_ << 5;
	return rng_;
}

float NeuronsEngine::random01() {
	return static_cast<float>(nextRandom() >> 8) * (1.0f / 16777216.0f);
}

float NeuronsEngine::gaussian() {
	// the sum of four uniform numbers is close enough to a bell curve; scaled to unit variance
	float sum = random01() + random01() + random01() + random01() - 2.0f;
	return sum * 1.7320508f;
}

void NeuronsEngine::reset() {
	rng_ = kSeed;
	for (int32_t i = 0; i < kNumNeurons; i++) {
		v_[i] = 0.2f * static_cast<float>(i); // staggered so they don't all fire together
		pulse_[i] = 0.0f;
		spikes_[i] = 0;
	}
	spikeCount_ = 0;
}

void NeuronsEngine::setParams(const Params& p) {
	params_ = p;
	for (int32_t i = 0; i < kNumNeurons; i++) {
		setRate(i, p.rate[i]);
		for (int32_t j = 0; j < kNumNeurons; j++) {
			setWeight(i, j, p.weight[i][j]);
		}
	}
	setLeak(p.leak);
	setNoise(p.noise);
	setRange(p.rangeVolts);
	setCVSource(0, p.cvSource[0]);
	setCVSource(1, p.cvSource[1]);
}

void NeuronsEngine::setRate(int32_t neuron, float hz) {
	if (neuron >= 0 && neuron < kNumNeurons) {
		params_.rate[neuron] = clamp(hz, kMinRate, kMaxRate);
	}
}

void NeuronsEngine::setWeight(int32_t target, int32_t source, int32_t level) {
	if (target >= 0 && target < kNumNeurons && source >= 0 && source < kNumNeurons) {
		params_.weight[target][source] =
		    static_cast<int8_t>(level < -kMaxWeightLevel ? -kMaxWeightLevel : (level > kMaxWeightLevel ? kMaxWeightLevel : level));
	}
}

void NeuronsEngine::setLeak(float v) {
	params_.leak = clamp(v, 0.0f, 1.0f);
}
void NeuronsEngine::setNoise(float v) {
	params_.noise = clamp(v, 0.0f, 1.0f);
}
void NeuronsEngine::setRange(float volts) {
	params_.rangeVolts = clamp(volts, 1.0f, 10.0f);
}
void NeuronsEngine::setTimeMode(TimeMode m) {
	params_.timeMode = m;
}
void NeuronsEngine::setCVSource(int32_t cv, int32_t source) {
	if (cv >= 0 && cv < kNumCV) {
		params_.cvSource[cv] = static_cast<uint8_t>(source < 0 ? 0 : (source > kAverageSource ? kAverageSource : source));
	}
}

void NeuronsEngine::applyPreset(int32_t preset) {
	if (preset < 0 || preset >= kNumPresets) {
		return;
	}
	for (int32_t i = 0; i < kNumNeurons; i++) {
		for (int32_t j = 0; j < kNumNeurons; j++) {
			params_.weight[i][j] = kPresets[preset][i][j];
		}
	}
}

char const* NeuronsEngine::presetName(int32_t preset) {
	return kPresetNames[preset < 0 ? 0 : (preset >= kNumPresets ? kNumPresets - 1 : preset)];
}

void NeuronsEngine::randomiseWeights() {
	for (int32_t i = 0; i < kNumNeurons; i++) {
		for (int32_t j = 0; j < kNumNeurons; j++) {
			if (i == j) {
				params_.weight[i][j] = 0;
				continue;
			}
			// mostly empty, with some excitation and inhibition
			float r = random01();
			int32_t level = 0;
			if (r > 0.45f) {
				level = static_cast<int32_t>(random01() * 4.0f) - 2;
				if (level >= 0) {
					level++;
				}
				level = level > kMaxWeightLevel ? kMaxWeightLevel : level;
			}
			params_.weight[i][j] = static_cast<int8_t>(level);
		}
	}
}

void NeuronsEngine::step(float dt, float bpm) {
	if (!(dt > 0.0f)) {
		return;
	}
	const float realDt = dt;
	if (params_.timeMode == TimeMode::TEMPO) {
		dt *= clamp(bpm, 1.0f, 999.0f) / 120.0f;
	}
	dt = dt > kMaxStepSeconds ? kMaxStepSeconds : dt;
	float leakRate = params_.leak * kMaxLeakPerSecond;
	float sigma = params_.noise * kMaxNoise * std::sqrt(dt);

	bool fired[kNumNeurons] = {};
	for (int32_t i = 0; i < kNumNeurons; i++) {
		float input = drive(params_.rate[i], leakRate);
		float v = v_[i] + (input - leakRate * v_[i]) * dt;
		if (sigma > 0.0f) {
			v += sigma * gaussian();
		}
		v = v < 0.0f ? 0.0f : v;
		if (v >= 1.0f) {
			fired[i] = true;
			v = 0.0f;
		}
		v_[i] = v;
	}

	// everything fires on this step's state; the kicks land afterwards, so a spike can't set off a chain in one step
	for (int32_t j = 0; j < kNumNeurons; j++) {
		if (!fired[j]) {
			continue;
		}
		spikes_[j]++;
		spikeCount_++;
		pulse_[j] = kPulseSeconds;
		for (int32_t i = 0; i < kNumNeurons; i++) {
			if (i == j || fired[i]) {
				continue;
			}
			float v = v_[i] + kKick * 0.5f * static_cast<float>(params_.weight[i][j]);
			v_[i] = v < 0.0f ? 0.0f : (v > 0.999f ? 0.999f : v);
		}
	}

	for (float& p : pulse_) {
		if (p > 0.0f) {
			p -= realDt;
			if (p < 0.0f) {
				p = 0.0f;
			}
		}
	}
}

float NeuronsEngine::getMembrane(int32_t neuron) const {
	return (neuron >= 0 && neuron < kNumNeurons) ? v_[neuron] : 0.0f;
}

uint32_t NeuronsEngine::getSpikes(int32_t neuron) const {
	return (neuron >= 0 && neuron < kNumNeurons) ? spikes_[neuron] : 0;
}

float NeuronsEngine::getVolts(int32_t cv) const {
	if (cv < 0 || cv >= kNumCV) {
		return 0.0f;
	}
	int32_t src = params_.cvSource[cv];
	float level;
	if (src >= kNumNeurons) {
		level = 0.0f;
		for (float v : v_) {
			level += v;
		}
		level /= static_cast<float>(kNumNeurons);
	}
	else {
		level = v_[src];
	}
	return clamp(level, 0.0f, 1.0f) * params_.rangeVolts;
}

uint16_t NeuronsEngine::getCVCode(int32_t cv) const {
	float code = clamp(getVolts(cv) * kVoltCode + 0.5f, 0.0f, kMaxCode);
	return static_cast<uint16_t>(code);
}

uint8_t NeuronsEngine::getGateLevels() const {
	uint8_t levels = 0;
	for (int32_t i = 0; i < kNumNeurons; i++) {
		if (pulse_[i] > 0.0f) {
			levels |= static_cast<uint8_t>(1 << i);
		}
	}
	return levels;
}

} // namespace deluge::neurons
