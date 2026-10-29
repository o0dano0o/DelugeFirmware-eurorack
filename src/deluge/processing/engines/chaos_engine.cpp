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

#include "processing/engines/chaos_engine.h"

#include <algorithm>
#include <iterator>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace deluge::chaos {

namespace {

/// Everything the engine needs to know about one chaotic system. The numbers were measured by the calibration
/// run in sloths_proto6.py (uncoupled, speed 1) and the per-model output choices come from sloths_tune.py.
struct ModelConfig {
	float nativeLoop;              ///< model time units between z peaks above the mean at speed 1
	float rate;                    ///< standard deviation of dx/dt at speed 1; scales the coupling
	float centre[kNumVars];        ///< long-run mean of x, y, z
	float scale[kNumVars];         ///< 99.9th percentile deviation from the mean of x, y, z
	float seed[kNumSloths][kNumVars]; ///< three distinct states already on the attractor
	uint8_t cv2Var;                ///< variable (0 x, 1 y, 2 z) blended into CV 2
	float cv1Gain;                 ///< gains chosen to keep each CV at its rails about 1.5% of the time or less
	float cv2Gain;
	uint8_t sectionVar;            ///< variable of sloth C watched by gate rule 1
	float bigLoopThreshold;        ///< rule 3 fires on peaks of sloth A's normalised z above this
};

// clang-format off
constexpr ModelConfig kModels[] = {
    // LORENZ
    {0.750432f, 42.812756f,
     {0.245438f, 0.244942f, 23.560916f}, {17.939956f, 23.990965f, 20.813336f},
     {{1.321151f, 1.415280f, 17.664244f}, {-12.366981f, -14.868482f, 29.152892f}, {-4.994995f, -1.995587f, 27.267543f}},
     2, 1.2f, 1.4f, 2, 0.694f},
    // ROSSLER
    {9.500127f, 5.989790f,
     {0.175231f, -0.880776f, 0.884675f}, {11.196733f, 9.866887f, 21.183101f},
     {{3.823693f, 5.961653f, 2.356288f}, {-5.861150f, 4.622953f, 0.018062f}, {5.968852f, 2.009117f, 0.397706f}},
     1, 1.6f, 1.2f, 0, 0.074f},
    // THOMAS
    {11.760495f, 0.457594f,
     {1.897743f, 1.892633f, 1.896964f}, {2.996130f, 2.992915f, 2.996192f},
     {{-0.712127f, 0.280540f, 1.371357f}, {3.502934f, 1.759808f, 1.884604f}, {0.526192f, 3.155217f, 2.694201f}},
     2, 1.4f, 1.2f, 0, 0.392f},
};
// clang-format on

static_assert(sizeof(kModels) / sizeof(kModels[0]) == static_cast<size_t>(Model::kNumModels),
              "one config per chaos model");

/// CV 1 weights the sloths towards the slow one, CV 2 towards the fast one (both sum to 1)
constexpr float kWeightsSlow[kNumSloths] = {0.25f / 1.6f, 0.35f / 1.6f, 1.00f / 1.6f};
constexpr float kWeightsFast[kNumSloths] = {1.00f / 1.5f, 0.40f / 1.5f, 0.10f / 1.5f};

constexpr float kHysteresis = 0.05f;
constexpr float kMaxStep = 0.02f;           ///< largest RK4 step in model time units; bigger steps are split
constexpr int32_t kMaxSubsteps = 16;
constexpr float kMinPeriodSeconds = 0.05f;
constexpr float kSwitchGlideTime = 1.0f;    ///< how long the CV glide after a model change lasts
constexpr float kSwitchSlewTau = 0.5f;
constexpr float kThomasB = 0.208186f;

void deriv(Model model, const float s[kNumVars], float drive, float out[kNumVars]) {
	const float x = s[0];
	const float y = s[1];
	const float z = s[2];
	switch (model) {
	case Model::LORENZ:
		out[0] = 10.0f * (y - x) + drive;
		out[1] = x * (28.0f - z) - y;
		out[2] = x * y - (8.0f / 3.0f) * z;
		break;
	case Model::ROSSLER:
		out[0] = -y - z + drive;
		out[1] = x + 0.2f * y;
		out[2] = 0.2f + z * (x - 5.7f);
		break;
	default: // THOMAS
		out[0] = std::sin(y) - kThomasB * x + drive;
		out[1] = std::sin(z) - kThomasB * y;
		out[2] = std::sin(x) - kThomasB * z;
		break;
	}
}

/// Each sloth is nudged by the previous one's normalised x: A by C, B by A, C by B
void computeDrive(const ModelConfig& cfg, float coupling, const float s[kNumSloths][kNumVars], float drive[kNumSloths]) {
	for (int32_t i = 0; i < kNumSloths; i++) {
		const int32_t prev = (i + kNumSloths - 1) % kNumSloths;
		const float xn = std::clamp((s[prev][0] - cfg.centre[0]) / cfg.scale[0], -1.0f, 1.0f);
		drive[i] = coupling * cfg.rate * xn;
	}
}

/// True for NaN and infinity. Looks at the exponent bits because the firmware builds with -ffast-math, which is
/// allowed to optimise away std::isfinite.
bool isNonFinite(float v) {
	uint32_t bits;
	std::memcpy(&bits, &v, sizeof(bits));
	return (bits & 0x7F800000u) == 0x7F800000u;
}

bool schmitt(bool state, float v) {
	if (state && v < -kHysteresis) {
		return false;
	}
	if (!state && v > kHysteresis) {
		return true;
	}
	return state;
}

} // namespace

ChaosEngine::ChaosEngine() {
	setModel(Model::LORENZ);
}

void ChaosEngine::seedState() {
	const ModelConfig& cfg = kModels[static_cast<size_t>(model_)];
	std::memcpy(state_, cfg.seed, sizeof(state_));
	for (int32_t i = 0; i < kNumSloths; i++) {
		for (int32_t v = 0; v < kNumVars; v++) {
			norm_[i][v] = std::clamp((state_[i][v] - cfg.centre[v]) / cfg.scale[v], -1.0f, 1.0f);
		}
	}
}

void ChaosEngine::setModel(Model model) {
	const bool glide = cvPrimed_;
	model_ = model;
	reset();
	if (glide) {
		cvPrimed_ = true;
		switchGlide_ = kSwitchGlideTime;
	}
}

void ChaosEngine::reset() {
	const ModelConfig& cfg = kModels[static_cast<size_t>(model_)];
	seedState();
	aLobe_ = norm_[0][0] > 0.0f;
	bLobe_ = norm_[1][0] > 0.0f;
	cLevel_ = norm_[2][cfg.sectionVar] > 0.0f;
	zPrev_ = norm_[0][2];
	zRising_ = false;
	std::fill(std::begin(pulseRemaining_), std::end(pulseRemaining_), 0.0f);
	gateLevels_ = static_cast<uint8_t>((cLevel_ ? 1 : 0) | ((aLobe_ != bLobe_) ? 8 : 0));
	cvPrimed_ = false; // the next step sets the CV outputs directly so a restart is repeatable
	switchGlide_ = 0.0f;
}

void ChaosEngine::setPeriod(int32_t sloth, float period) {
	if (sloth >= 0 && sloth < kNumSloths && !isNonFinite(period)) {
		periods_[sloth] = std::max(period, 0.0f);
	}
}

bool ChaosEngine::stateIsBad() const {
	for (int32_t i = 0; i < kNumSloths; i++) {
		for (int32_t v = 0; v < kNumVars; v++) {
			const float value = state_[i][v];
			if (isNonFinite(value) || std::fabs(value) > 1.0e4f) {
				return true;
			}
		}
	}
	return false;
}

void ChaosEngine::integrate(const float hStep[kNumSloths]) {
	const ModelConfig& cfg = kModels[static_cast<size_t>(model_)];

	float hMax = 0.0f;
	for (int32_t i = 0; i < kNumSloths; i++) {
		hMax = std::max(hMax, hStep[i]);
	}
	int32_t substeps = 1;
	if (hMax > kMaxStep) {
		substeps = std::min(kMaxSubsteps, static_cast<int32_t>(std::ceil(hMax / kMaxStep)));
	}
	float h[kNumSloths];
	for (int32_t i = 0; i < kNumSloths; i++) {
		h[i] = hStep[i] / static_cast<float>(substeps);
	}

	for (int32_t n = 0; n < substeps; n++) {
		float k1[kNumSloths][kNumVars];
		float k2[kNumSloths][kNumVars];
		float k3[kNumSloths][kNumVars];
		float k4[kNumSloths][kNumVars];
		float tmp[kNumSloths][kNumVars];
		float drive[kNumSloths];

		auto stage = [&](const float s[kNumSloths][kNumVars], float k[kNumSloths][kNumVars]) {
			computeDrive(cfg, coupling_, s, drive);
			for (int32_t i = 0; i < kNumSloths; i++) {
				deriv(model_, s[i], drive[i], k[i]);
			}
		};
		auto advance = [&](float factor, const float k[kNumSloths][kNumVars]) {
			for (int32_t i = 0; i < kNumSloths; i++) {
				for (int32_t v = 0; v < kNumVars; v++) {
					tmp[i][v] = state_[i][v] + factor * h[i] * k[i][v];
				}
			}
		};

		stage(state_, k1);
		advance(0.5f, k1);
		stage(tmp, k2);
		advance(0.5f, k2);
		stage(tmp, k3);
		advance(1.0f, k3);
		stage(tmp, k4);

		for (int32_t i = 0; i < kNumSloths; i++) {
			for (int32_t v = 0; v < kNumVars; v++) {
				state_[i][v] += h[i] / 6.0f * (k1[i][v] + 2.0f * k2[i][v] + 2.0f * k3[i][v] + k4[i][v]);
			}
		}
	}
}

void ChaosEngine::updateOutputs(float dt) {
	const ModelConfig& cfg = kModels[static_cast<size_t>(model_)];

	for (int32_t i = 0; i < kNumSloths; i++) {
		for (int32_t v = 0; v < kNumVars; v++) {
			norm_[i][v] = std::clamp((state_[i][v] - cfg.centre[v]) / cfg.scale[v], -1.0f, 1.0f);
		}
	}

	float mix1 = 0.0f;
	float mix2 = 0.0f;
	for (int32_t i = 0; i < kNumSloths; i++) {
		mix1 += kWeightsSlow[i] * norm_[i][0];
		mix2 += kWeightsFast[i] * norm_[i][cfg.cv2Var];
	}
	const float target[kNumCVOutputs] = {5.0f + std::clamp(mix1 * cfg.cv1Gain, -1.0f, 1.0f) * 5.0f,
	                                     5.0f + std::clamp(mix2 * cfg.cv2Gain, -1.0f, 1.0f) * 5.0f};

	float tau = cvSlew_;
	if (switchGlide_ > 0.0f) {
		tau = std::max(tau, kSwitchSlewTau);
		switchGlide_ = std::max(0.0f, switchGlide_ - dt);
	}
	if (!cvPrimed_ || tau <= 0.0f) {
		cvVolts_[0] = target[0];
		cvVolts_[1] = target[1];
		cvPrimed_ = true;
	}
	else {
		const float alpha = 1.0f - std::exp(-dt / tau);
		for (int32_t c = 0; c < kNumCVOutputs; c++) {
			cvVolts_[c] += alpha * (target[c] - cvVolts_[c]);
		}
	}

	for (int32_t c = 0; c < kNumCVOutputs; c++) {
		const int32_t code = static_cast<int32_t>(std::lround(cvVolts_[c] * static_cast<float>(kDacCodesPerVolt)));
		cvCode_[c] = static_cast<uint16_t>(std::clamp(code, int32_t{0}, kDacMaxCode));
	}
}

void ChaosEngine::updateGates(float dt) {
	const ModelConfig& cfg = kModels[static_cast<size_t>(model_)];
	const float halfStep = 0.5f * dt;

	for (float& remaining : pulseRemaining_) {
		remaining = std::max(0.0f, remaining - dt);
	}

	const bool aNew = schmitt(aLobe_, norm_[0][0]);
	const bool bNew = schmitt(bLobe_, norm_[1][0]);
	const bool cNew = schmitt(cLevel_, norm_[2][cfg.sectionVar]);

	// rule 2: every lobe flip of B
	if (bNew != bLobe_) {
		pulseRemaining_[1] = pulseLength_;
	}

	// rule 3: a peak of A's z above the model's threshold (rising, then falling)
	const float z = norm_[0][2];
	if (z > zPrev_) {
		zRising_ = true;
	}
	else if (z < zPrev_) {
		if (zRising_ && zPrev_ > cfg.bigLoopThreshold) {
			pulseRemaining_[2] = pulseLength_;
		}
		zRising_ = false;
	}
	zPrev_ = z;

	aLobe_ = aNew;
	bLobe_ = bNew;
	cLevel_ = cNew;

	uint8_t levels = 0;
	if (cLevel_) {
		levels |= 1;
	}
	if (pulseRemaining_[1] > halfStep) {
		levels |= 2;
	}
	if (pulseRemaining_[2] > halfStep) {
		levels |= 4;
	}
	if (aLobe_ != bLobe_) {
		levels |= 8;
	}
	gateLevels_ = levels;
}

void ChaosEngine::step(float dt, float bpm) {
	const ModelConfig& cfg = kModels[static_cast<size_t>(model_)];
	const float safeBpm = std::clamp(bpm, 1.0f, 999.0f);

	float h[kNumSloths];
	for (int32_t i = 0; i < kNumSloths; i++) {
		float seconds = (timeMode_ == TimeMode::TEMPO) ? periods_[i] * 60.0f / safeBpm : periods_[i];
		seconds = std::max(seconds, kMinPeriodSeconds);
		h[i] = cfg.nativeLoop / seconds * dt;
	}

	integrate(h);
	if (stateIsBad()) {
		seedState();
	}
	updateOutputs(dt);
	updateGates(dt);
}

} // namespace deluge::chaos
