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

/// Marbles-style random: a clock with a bit of jitter, two random voltages and four gates.
///
/// Each voltage (X1, X2) plays a looping buffer of random numbers. The "déjà vu" amount is the chance that each step
/// replays what is stored there instead of drawing a new number, so 0 is fully random and 1 is a locked loop. The
/// buffers have their own lengths, so the two voltages drift against each other. Spread and bias shape what the stored
/// numbers turn into (a narrow or wide range, leaning low or high), so they can be turned while a loop plays.
///
/// Gates, all from the same clock:
///   gate 1  every clock step
///   gate 2  a random half of the steps (the chance is the Bernoulli setting; the choice loops with the déjà vu)
///   gate 3  the steps gate 2 did not take
///   gate 4  every Nth step
///
/// Pure compute with no hardware calls, so it can be tested on the host.
namespace deluge::marbles {

constexpr int32_t kMaxLength = 16;
constexpr int32_t kNumCV = 2;
constexpr int32_t kNumGates = 4;
constexpr int32_t kNumRates = 8;

enum class TimeMode : uint8_t { FREE, TEMPO };

/// Clock steps per beat for each tempo rate
float stepsPerBeat(int32_t rateIndex);
char const* rateName(int32_t rateIndex);

struct Params {
	float dejaVu = 0.5f;             ///< 0 (always new) to 1 (locked loop)
	uint8_t length[kNumCV] = {8, 5}; ///< loop length of each voltage, 1 to 16
	float spread = 0.6f;             ///< 0 (always the bias value) to 1 (the full range)
	float bias = 0.5f;               ///< 0 leans low, 1 leans high
	float steps = 1.0f;              ///< 0 glides over the whole step, 1 jumps
	float rangeVolts = 5.0f;         ///< output reaches this many volts at most (1 to 10)
	float jitter = 0.0f;             ///< 0 (steady clock) to 1 (wobbles by up to half a step)
	float bernoulli = 0.5f;          ///< chance a step goes to gate 2 instead of gate 3
	uint8_t gateDivision = 4;        ///< gate 4 fires every this many steps (1 to 16)
	TimeMode timeMode = TimeMode::FREE;
	float freePeriod = 0.5f;         ///< seconds per step in FREE mode (0.02 to 20)
	uint8_t rate = 3;                ///< index of the tempo rate in TEMPO mode
};

class MarblesEngine {
public:
	MarblesEngine() { reset(); }

	/// Back to the start: same random numbers, same loop position, clock about to fire
	void reset();
	/// Fills the loops with new random numbers
	void randomise();

	const Params& params() const { return params_; }
	void setParams(const Params& p);
	void setDejaVu(float v);
	void setLength(int32_t cv, int32_t length);
	void setSpread(float v);
	void setBias(float v);
	void setSteps(float v);
	void setRange(float volts);
	void setJitter(float v);
	void setBernoulli(float v);
	void setGateDivision(int32_t n);
	void setTimeMode(TimeMode m);
	void setFreePeriod(float seconds);
	void setRate(int32_t index);

	/// Advances time. `bpm` is only used in TEMPO mode. Several clock steps can happen in one call if dt is large.
	void step(float dt, float bpm);

	/// The two voltages, 0 to 1 before the range is applied
	float getValue(int32_t cv) const { return out_[cv]; }
	float getVolts(int32_t cv) const;
	/// DAC code, 1 V = 6552, clamped to 0 to 65520
	uint16_t getCVCode(int32_t cv) const;
	/// Bit n is gate n+1
	uint8_t getGateLevels() const;
	/// Clock steps since reset
	uint32_t getClockCount() const { return clockCount_; }
	/// Where each voltage is in its loop (0 to length-1)
	int32_t getPosition(int32_t cv) const;
	/// What the loop holds at a step once spread and bias are applied (0 to 1)
	float getLoopValue(int32_t cv, int32_t index) const;
	/// Sets a loop step so that it plays as `value` (0 to 1) with the current spread and bias
	void setLoopValue(int32_t cv, int32_t index, float value);
	/// Seconds per clock step at this tempo
	float getPeriod(float bpm) const;

private:
	uint32_t nextRandom();
	float random01();
	float shape(float u) const;
	float unshape(float value) const;
	void clockStep();
	float nextInterval(float period);

	Params params_;
	uint32_t rng_ = 1;
	float loop_[kNumCV][kMaxLength];
	float choice_[kMaxLength]; ///< gate 2/3 decisions, looped with the first voltage's length
	float out_[kNumCV];
	float target_[kNumCV];
	float timeToNext_ = 0.0f;
	float gateTime_[kNumGates];
	uint32_t clockCount_ = 0;
	float lastPeriod_ = 0.5f;
};

} // namespace deluge::marbles
