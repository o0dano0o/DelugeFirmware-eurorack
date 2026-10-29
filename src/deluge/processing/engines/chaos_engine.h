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

#pragma once

#include <cstdint>

namespace deluge::chaos {

constexpr int32_t kNumSloths = 3;
constexpr int32_t kNumVars = 3; ///< x, y, z of each sloth
constexpr int32_t kNumGateRules = 4;
constexpr int32_t kNumCVOutputs = 2;

/// The CV engine's DAC scale: 1 V is 6552 codes, so 10 V is 65520
constexpr int32_t kDacCodesPerVolt = 6552;
constexpr int32_t kDacMaxCode = 10 * kDacCodesPerVolt;

/// Which chaotic system each of the three coupled "sloths" runs
enum class Model : uint8_t {
	LORENZ,
	ROSSLER,
	THOMAS,
	kNumModels,
};

/// How the sloth periods are interpreted
enum class TimeMode : uint8_t {
	FREE,  ///< periods are in seconds
	TEMPO, ///< periods are in beats, so the chaos follows the BPM
};

/// Three coupled chaotic oscillators ("sloths", A fast, B medium, C slow) with a CV and gate output stage.
///
/// This class is pure computation: it knows nothing about the DAC, gate pins, the scheduler or the transport. The
/// caller steps it at a fixed control rate, reads the outputs, and decides what to do when playback starts or stops
/// (call reset() on play to make the output repeatable, or simply stop calling step() to freeze it).
///
/// Outputs:
///   CV 1  a slow-weighted blend of the sloths' x values
///   CV 2  a fast-weighted blend of one other variable (chosen per model)
///   gate rules (bit n of gateLevels() is rule n + 1):
///     1 Section    level, sloth C crossing its centre (Schmitt)
///     2 Lobe flip  trigger on every sign change of sloth B's x
///     3 Big loop   trigger on each peak of sloth A's z above a per-model threshold
///     4 Disagree   level, sloth A's x sign differs from sloth B's x sign
///
/// All the numbers for each model (loop time, centre, scale, seed, gains) were measured with the Python prototypes
/// (sloths_proto6.py, sloths_tune.py) and live in the model table in chaos_engine.cpp.
class ChaosEngine {
public:
	ChaosEngine();

	/// Selects the model and restarts the sloths from that model's seed. The CV outputs glide to their new values
	/// over about a second instead of jumping.
	void setModel(Model model);
	Model getModel() const { return model_; }

	/// Restarts the sloths from the current model's seed. Calling this when playback starts makes the output
	/// repeat every time.
	void reset();

	void setTimeMode(TimeMode mode) { timeMode_ = mode; }
	TimeMode getTimeMode() const { return timeMode_; }

	/// Loop period of each sloth: seconds in FREE mode, beats in TEMPO mode. Very small values are clamped.
	void setPeriod(int32_t sloth, float period);
	float getPeriod(int32_t sloth) const { return periods_[sloth]; }

	/// How strongly each sloth is pushed by the previous one (A is driven by C, B by A, C by B), relative to the
	/// model's typical rate of change. 0 leaves them independent; the default is 0.04.
	void setCoupling(float coupling) { coupling_ = coupling; }
	float getCoupling() const { return coupling_; }

	/// Length of the trigger pulses of gate rules 2 and 3
	void setPulseLength(float seconds) { pulseLength_ = seconds; }

	/// Extra smoothing of the CV outputs (time constant in seconds, 0 for none)
	void setCVSlew(float seconds) { cvSlew_ = seconds; }

	/// Advances the sloths by `dt` seconds (`dt` is the caller's fixed control period, e.g. 0.002) and updates the
	/// outputs. `bpm` is only used in TEMPO mode.
	void step(float dt, float bpm);

	/// DAC code for CV output 0 or 1, 0 to kDacMaxCode
	uint16_t getCVCode(int32_t output) const { return cvCode_[output]; }
	/// Voltage for CV output 0 or 1, 0 to 10 V
	float getCVVolts(int32_t output) const { return cvVolts_[output]; }
	/// One bit per gate rule: bit 0 is rule 1 and so on
	uint8_t getGateLevels() const { return gateLevels_; }

	/// A sloth's variable, centred and scaled to about -1..1. For displays.
	float getNormalised(int32_t sloth, int32_t var) const { return norm_[sloth][var]; }

private:
	void seedState();
	void integrate(const float h[kNumSloths]);
	void updateOutputs(float dt);
	void updateGates(float dt);
	bool stateIsBad() const;

	Model model_{Model::LORENZ};
	TimeMode timeMode_{TimeMode::FREE};
	float periods_[kNumSloths] = {12.0f, 30.0f, 90.0f};
	float coupling_{0.04f};
	float pulseLength_{0.010f};
	float cvSlew_{0.0f};

	float state_[kNumSloths][kNumVars];
	float norm_[kNumSloths][kNumVars];

	// output stage
	bool cvPrimed_{false};
	float switchGlide_{0.0f}; ///< seconds left of the glide after a model change
	float cvVolts_[kNumCVOutputs] = {5.0f, 5.0f};
	uint16_t cvCode_[kNumCVOutputs] = {0, 0};

	// gate rules
	bool aLobe_{false};
	bool bLobe_{false};
	bool cLevel_{false};
	bool zRising_{false};
	float zPrev_{0.0f};
	float pulseRemaining_[kNumGateRules] = {0.0f, 0.0f, 0.0f, 0.0f};
	uint8_t gateLevels_{0};
};

} // namespace deluge::chaos
