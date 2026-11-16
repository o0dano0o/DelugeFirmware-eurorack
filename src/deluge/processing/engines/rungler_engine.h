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

/// Rungler: two oscillators and an 8-bit shift register, in the spirit of the Benjolin.
///
/// Oscillator B is the clock. Each time it completes a cycle the register shifts one place. The bit that enters is the
/// bit that fell off the far end, so with chance 0 the register just circulates and the pattern repeats every 8 shifts.
/// With a chance event the oscillator A comparator (high for the first half of its cycle) is XORed in, which disturbs
/// the pattern. Chance 1 disturbs it on every shift. The register value also bends both oscillators (mod), as the
/// rungler does on the Benjolin, so a pattern that locks in can pull the two rates into a repeating relationship.
///
/// Outputs:
///   CV 1  the last three bits as a stepped voltage with 8 levels
///   CV 2  all eight bits as a weighted value, slewed
///   gate 1  a short pulse on every shift
///   gates 2 to 4  the three newest bits of the register, as levels
///
/// Pure compute with no hardware calls, so it can be tested on the host.
namespace deluge::rungler {

using marbles::TimeMode;

constexpr int32_t kNumCV = 2;
constexpr int32_t kNumGates = 4;

struct Params {
	float chance = 0.2f;          ///< chance per shift that oscillator A disturbs the new bit, 0 to 1
	float modAmount = 0.3f;       ///< how far the register bends the oscillator rates, 0 to 1
	float slew = 0.3f;            ///< smoothing on CV 2, 0 (none) to 1 (about half a second)
	float rangeVolts = 5.0f;      ///< outputs reach this many volts at most (1 to 10)
	TimeMode timeMode = TimeMode::FREE;
	float freqA = 3.1f;           ///< oscillator A in Hz (FREE mode)
	float freqB = 2.0f;           ///< oscillator B, the clock, in Hz (FREE mode)
	uint8_t rateA = 4;            ///< tempo rate index of oscillator A (TEMPO mode)
	uint8_t rateB = 3;            ///< tempo rate index of oscillator B (TEMPO mode)
};

class RunglerEngine {
public:
	RunglerEngine() { reset(); }

	/// Back to the start: same register, same phases, same random numbers
	void reset();

	const Params& params() const { return params_; }
	void setParams(const Params& p);
	void setChance(float v);
	void setModAmount(float v);
	void setSlew(float v);
	void setRange(float volts);
	void setTimeMode(TimeMode m);
	void setFreqA(float hz);
	void setFreqB(float hz);
	void setRateA(int32_t index);
	void setRateB(int32_t index);

	/// Advances by dt seconds. bpm is the song tempo, used in TEMPO mode.
	void step(float dt, float bpm);

	uint8_t getRegister() const { return register_; }
	void setRegister(uint8_t bits);
	void toggleBit(int32_t index);
	uint32_t getShiftCount() const { return shiftCount_; }
	/// 0 to 1 through the current cycle of each oscillator
	float getPhaseA() const { return phaseA_; }
	float getPhaseB() const { return phaseB_; }
	bool isAHigh() const { return phaseA_ < 0.5f; }

	float getVolts(int32_t cv) const;
	uint16_t getCVCode(int32_t cv) const;
	/// Bit n is gate n+1
	uint8_t getGateLevels() const;

	/// Oscillator rates as they stand now, with the register's bend included
	float getRateA(float bpm) const;
	float getRateB(float bpm) const;

private:
	uint32_t nextRandom();
	float random01();
	float baseRate(bool isA, float bpm) const;
	void shift();

	Params params_;
	uint32_t rng_ = 1;
	uint8_t register_ = 0;
	float phaseA_ = 0.0f;
	float phaseB_ = 0.0f;
	uint32_t shiftCount_ = 0;
	float pulseTime_ = 0.0f;
	float out_[kNumCV] = {0.0f, 0.0f};
};

} // namespace deluge::rungler
