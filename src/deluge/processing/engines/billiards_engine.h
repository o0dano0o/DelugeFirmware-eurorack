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

/// One to four balls bouncing round a 16 x 8 table (one cell per pad). Every bounce fires a gate. Pad cells can be
/// switched into bumpers that deflect the balls, gravity pulls them to the floor, and a bounce below 100% takes
/// energy out of them (a slow ball is launched again, so it never stops). Balls bounce off each other too.
///
/// Gates, in walls mode: 1 left wall, 2 right wall, 3 floor or ceiling, 4 a bumper or another ball. In balls mode gate n
/// fires whenever ball n bounces off anything. CV 1 is one ball's position along the table, CV 2 its height.
///
/// Pure compute, no hardware calls.
namespace deluge::billiards {

using marbles::TimeMode;

constexpr int32_t kWidth = 16;
constexpr int32_t kHeight = 8;
constexpr int32_t kMaxBalls = 4;
constexpr int32_t kNumCV = 2;
constexpr int32_t kNumGates = 4;

enum class GateMode : uint8_t { WALLS, BALLS };

struct Params {
	int32_t numBalls = 2;                ///< 1 to 4
	float speed = 6.0f;                  ///< launch speed in cells per second, 0.5 to 30
	float gravity = 0.0f;                ///< cells per second squared towards the floor, 0 to 30
	float bounce = 1.0f;                 ///< how much speed a wall or bumper keeps, 0.3 to 1
	int32_t cvBall = 0;                  ///< which ball CV 1 and CV 2 follow, 0 to 3
	GateMode gateMode = GateMode::WALLS; ///< what each gate means
	float rangeVolts = 5.0f;             ///< outputs reach this many volts at most (1 to 10)
	TimeMode timeMode = TimeMode::FREE;  ///< TEMPO makes time run at bpm / 120
	uint16_t bumpers[kHeight] = {};      ///< bit x of row y set: that cell is a bumper
};

class BilliardsEngine {
public:
	BilliardsEngine() { reset(); }

	/// Puts the balls back on their starting spots and starts the random numbers over. Bumpers stay.
	void reset();

	const Params& params() const { return params_; }
	void setParams(const Params& p);
	void setNumBalls(int32_t n);
	void setSpeed(float cellsPerSecond); ///< also rescales the balls that are moving now
	void setGravity(float v);
	void setBounce(float v);
	void setCvBall(int32_t ball);
	void setGateMode(GateMode m);
	void setRange(float volts);
	void setTimeMode(TimeMode m);
	bool hasBumper(int32_t x, int32_t y) const;
	void toggleBumper(int32_t x, int32_t y);
	void clearBumpers();
	/// Gives every ball a new random direction at the current speed
	void relaunch();
	/// Puts a ball exactly where you say (for tests and for the view)
	void setBall(int32_t ball, float x, float y, float vx, float vy);

	/// Advances by dt seconds of real time
	void step(float dt, float bpm);

	float getX(int32_t ball) const;
	float getY(int32_t ball) const;
	float getVx(int32_t ball) const;
	float getVy(int32_t ball) const;
	uint32_t getTotalHits() const { return totalHits_; }
	uint32_t getHitCount(int32_t ball) const;
	float getVolts(int32_t cv) const;
	uint16_t getCVCode(int32_t cv) const;
	/// Bit n is set while gate n+1's pulse is high
	uint8_t getGateLevels() const;

private:
	uint32_t nextRandom();
	float random01();
	void launch(int32_t ball);
	void fire(int32_t ball, int32_t wallGate);
	bool bumperAt(float x, float y) const;
	void moveBall(int32_t i, float dt);
	void collideBalls();

	Params params_;
	float x_[kMaxBalls];
	float y_[kMaxBalls];
	float vx_[kMaxBalls];
	float vy_[kMaxBalls];
	uint32_t hits_[kMaxBalls];
	uint32_t totalHits_ = 0;
	float pulse_[kNumGates];
	uint32_t rng_ = 1;
};

} // namespace deluge::billiards
