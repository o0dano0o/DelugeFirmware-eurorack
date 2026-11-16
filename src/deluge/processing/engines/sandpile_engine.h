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

/// An abelian sandpile on a 16 x 8 grid, one cell per pad. Grains drop on the grid on a clock; a cell holding four or
/// more grains topples, sending one grain to each of its four neighbours (grains pushed off the edge are lost), which
/// can topple the neighbours in turn. Most drops do nothing, some set off a small slide, and now and then there is a
/// big avalanche: the sizes follow a power law once the pile has filled up.
///
/// Gates: 1 a grain drops, 2 a drop sets off an avalanche, 3 each round of toppling while it slides, 4 an avalanche
/// that ends up at least the "big" size. CV 1 is the size of the current (or last) avalanche on a log scale, CV 2 is how
/// full the pile is.
///
/// Pure compute, no hardware calls.
namespace deluge::sandpile {

using marbles::TimeMode;

constexpr int32_t kWidth = 16;
constexpr int32_t kHeight = 8;
constexpr int32_t kNumCells = kWidth * kHeight;
constexpr int32_t kLimit = 4; ///< a cell with this many grains topples
constexpr int32_t kNumCV = 2;
constexpr int32_t kNumGates = 4;

enum class DropMode : uint8_t { RANDOM, CENTRE };

struct Params {
	float dropRate = 3.0f;               ///< grains per second, 0.25 to 20
	DropMode dropMode = DropMode::RANDOM;
	int32_t bigSize = 12;                ///< an avalanche this big or bigger fires gate 4 (3 to 60 topples)
	float toppleRate = 30.0f;            ///< rounds of toppling per second, 5 to 100
	float rangeVolts = 5.0f;             ///< outputs reach this many volts at most (1 to 10)
	TimeMode timeMode = TimeMode::FREE;  ///< TEMPO makes time run at bpm / 120
};

class SandpileEngine {
public:
	SandpileEngine() { reset(); }

	/// Starts again from an empty table and drops enough grains to fill the pile to its usual level
	void reset();
	/// Takes every grain off
	void clear();

	const Params& params() const { return params_; }
	void setParams(const Params& p);
	void setDropRate(float v);
	void setDropMode(DropMode m);
	void setBigSize(int32_t n);
	void setToppleRate(float v);
	void setRange(float volts);
	void setTimeMode(TimeMode m);

	/// Puts one grain on a cell (as a drop does). Returns false off the table.
	bool addGrain(int32_t x, int32_t y);
	/// Takes one grain off a cell
	bool removeGrain(int32_t x, int32_t y);
	/// Sets a cell directly (tests)
	void setCell(int32_t x, int32_t y, int32_t grains);
	/// Topples everything until nothing is left to topple, at once, and returns the number of topples (tests)
	int32_t relaxNow();

	/// Advances by dt seconds of real time
	void step(float dt, float bpm);

	int32_t getCell(int32_t x, int32_t y) const;
	/// Above zero for a moment after the cell toppled
	float getFlash(int32_t x, int32_t y) const;
	int32_t getTotalGrains() const;
	uint32_t getTotalDropped() const { return dropped_; }
	uint32_t getTotalLost() const { return lost_; }
	uint32_t getTotalTopples() const { return topples_; }
	uint32_t getAvalancheCount() const { return avalanches_; }
	bool isSliding() const { return sliding_; }
	int32_t getCurrentSize() const { return size_; }
	int32_t getLastSize() const { return lastSize_; }
	float getVolts(int32_t cv) const;
	uint16_t getCVCode(int32_t cv) const;
	/// Bit n is set while gate n+1's pulse is high
	uint8_t getGateLevels() const;

private:
	uint32_t nextRandom();
	void drop();
	void topplingRound();
	void firePulse(int32_t gate);
	bool anyUnstable() const;

	Params params_;
	uint8_t cells_[kNumCells];
	float flash_[kNumCells];
	float pulse_[kNumGates];
	float dropTimer_ = 0.0f;
	float roundTimer_ = 0.0f;
	bool sliding_ = false;
	int32_t size_ = 0;
	int32_t lastSize_ = 0;
	uint32_t dropped_ = 0;
	uint32_t lost_ = 0;
	uint32_t topples_ = 0;
	uint32_t avalanches_ = 0;
	uint32_t rng_ = 1;
};

} // namespace deluge::sandpile
