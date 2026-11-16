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

#include "processing/engines/marbles_engine.h"
#include <cstdint>

/// Four coupled phase oscillators (a Kuramoto model). Each has its own rate and its phase runs round once per cycle,
/// firing its gate each time it wraps. Coupling pulls every oscillator towards the others: at zero they keep their own
/// rates and drift against each other as polyrhythms; as it rises they bunch up, then lock together and fire as one.
///
/// CV 1 is how bunched up they are (the order parameter: 0 spread evenly round the cycle, 1 all together). CV 2 is the
/// phase of the bunch (where the middle of the group sits in the cycle), a ramp that wraps once per cycle of the group.
///
/// Pure compute, no hardware calls.
namespace deluge::kuramoto {

using marbles::TimeMode;

constexpr int32_t kNumOscillators = 4;
constexpr int32_t kNumCV = 2;
constexpr int32_t kNumGates = 4;
constexpr int32_t kNumPresets = 4;

struct Params {
	float rate[kNumOscillators] = {1.0f, 1.5f, 2.25f, 0.75f}; ///< cycles per second when alone, 0.1 to 16
	float coupling = 0.3f;                                     ///< 0 (independent) to 1 (strongly locked)
	float noise = 0.0f;                                        ///< random wobble in the phases, 0 to 1
	float rangeVolts = 5.0f;                                   ///< outputs reach this many volts at most (1 to 10)
	TimeMode timeMode = TimeMode::FREE;                        ///< TEMPO makes time run at bpm / 120
};

class KuramotoEngine {
public:
	KuramotoEngine() { reset(); }

	/// Back to the starting phases (spread evenly round the cycle) and the starting random numbers
	void reset();

	const Params& params() const { return params_; }
	void setParams(const Params& p);
	void setRate(int32_t oscillator, float hz);
	void setCoupling(float v);
	void setNoise(float v);
	void setRange(float volts);
	void setTimeMode(TimeMode m);
	/// 0 spread, 1 close, 2 harmonic, 3 equal. Replaces the four rates.
	void applyPreset(int32_t preset);
	static char const* presetName(int32_t preset);
	/// Gives each oscillator a new random rate between 0.5 and 3 Hz
	void randomiseRates();

	/// Advances by dt seconds of real time
	void step(float dt, float bpm);

	float getPhase(int32_t oscillator) const;     ///< 0 to 1
	float getOrder() const { return order_; }     ///< 0 to 1
	float getMeanPhase() const { return mean_; }  ///< 0 to 1
	uint32_t getWrapCount(int32_t oscillator) const;
	uint32_t getTotalWraps() const { return totalWraps_; }
	float getVolts(int32_t cv) const;
	uint16_t getCVCode(int32_t cv) const;
	/// Bit n is set while gate n+1's pulse is high
	uint8_t getGateLevels() const;

private:
	uint32_t nextRandom();
	float random01();
	float gaussian();
	void updateOrder();

	Params params_;
	float phase_[kNumOscillators];
	int8_t owed_[kNumOscillators];
	float pulse_[kNumOscillators];
	uint32_t wraps_[kNumOscillators];
	uint32_t totalWraps_ = 0;
	float order_ = 0.0f;
	float mean_ = 0.0f;
	uint32_t rng_ = 1;
};

} // namespace deluge::kuramoto
