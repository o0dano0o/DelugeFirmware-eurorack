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

#include "CppUTest/TestHarness.h"
#include "processing/engines/billiards_engine.h"
#include <cmath>

using namespace deluge::billiards;

namespace {

constexpr float kDt = 0.002f;

void advance(BilliardsEngine& e, float seconds, float bpm = 120.0f) {
	int steps = static_cast<int>(seconds / kDt);
	for (int i = 0; i < steps; i++) {
		e.step(kDt, bpm);
	}
}

/// One ball, nothing else
BilliardsEngine single() {
	BilliardsEngine e;
	e.setNumBalls(1);
	return e;
}

uint8_t gatesSeen(BilliardsEngine& e, float seconds) {
	uint8_t seen = 0;
	int steps = static_cast<int>(seconds / kDt);
	for (int i = 0; i < steps; i++) {
		e.step(kDt, 120.0f);
		seen |= e.getGateLevels();
	}
	return seen;
}

} // namespace

TEST_GROUP(Billiards){};

TEST(Billiards, ballsStayOnTheTable) {
	BilliardsEngine e;
	e.setNumBalls(4);
	e.setGravity(8.0f);
	e.setBounce(0.8f);
	for (int i = 0; i < 20000; i++) {
		e.step(kDt, 120.0f);
		for (int b = 0; b < 4; b++) {
			CHECK(e.getX(b) >= 0.0f && e.getX(b) < kWidth);
			CHECK(e.getY(b) >= 0.0f && e.getY(b) < kHeight);
		}
	}
}

TEST(Billiards, noLossKeepsTheSpeed) {
	BilliardsEngine e = single();
	float before = std::hypot(e.getVx(0), e.getVy(0));
	advance(e, 30.0f);
	float after = std::hypot(e.getVx(0), e.getVy(0));
	DOUBLES_EQUAL(before, after, 0.001);
	CHECK(e.getTotalHits() > 10);
}

TEST(Billiards, knownPathHitsLeftAndRightWallsOnce) {
	BilliardsEngine e = single();
	e.setBall(0, 8.0f, 4.0f, 8.0f, 0.0f);
	uint8_t seen = gatesSeen(e, 3.5f);
	// right wall at 1 s, left wall at 3 s: two hits, on gates 2 then 1
	CHECK_EQUAL(2u, e.getTotalHits());
	CHECK_EQUAL(3, seen & 3);
	CHECK_EQUAL(0, seen & 12);
}

TEST(Billiards, bounceBelowOneTakesSpeedOffTheWall) {
	BilliardsEngine e = single();
	e.setBounce(0.5f);
	e.setBall(0, 8.0f, 4.0f, 8.0f, 0.0f);
	advance(e, 1.2f);
	DOUBLES_EQUAL(-4.0, e.getVx(0), 0.05);
}

TEST(Billiards, gravityDropsABallToTheFloorOnTime) {
	BilliardsEngine e = single();
	e.setGravity(10.0f);
	e.setBall(0, 8.0f, 7.0f, 0.0f, 0.0f);
	advance(e, 1.0f);
	CHECK_EQUAL(0u, e.getTotalHits());
	advance(e, 0.4f);
	CHECK_EQUAL(1u, e.getTotalHits()); // sqrt(2 * 7 / 10) is about 1.18 s
}

TEST(Billiards, bouncingBallLosesHeightEachBounce) {
	BilliardsEngine e = single();
	e.setGravity(10.0f);
	e.setBounce(0.7f);
	e.setBall(0, 8.0f, 7.5f, 0.0f, 0.0f);
	float firstPeak = 0.0f;
	float laterPeak = 0.0f;
	for (int i = 0; i < 4000; i++) { // 8 s
		e.step(kDt, 120.0f);
		if (i > 600 && i < 1500) { // after the first bounce, before the third
			if (e.getY(0) > firstPeak) {
				firstPeak = e.getY(0);
			}
		}
		if (i > 2200 && i < 3200) {
			if (e.getY(0) > laterPeak) {
				laterPeak = e.getY(0);
			}
		}
	}
	CHECK(firstPeak > laterPeak);
}

TEST(Billiards, slowBallIsLaunchedAgain) {
	BilliardsEngine e = single();
	e.setGravity(10.0f);
	e.setBounce(0.3f);
	e.setBall(0, 8.0f, 1.0f, 0.0f, 0.0f);
	advance(e, 20.0f);
	// it must still be moving or in the air after twenty seconds, not stuck on the floor
	CHECK(e.getTotalHits() > 5);
}

TEST(Billiards, bumperDeflectsABall) {
	BilliardsEngine e = single();
	e.toggleBumper(10, 4);
	CHECK(e.hasBumper(10, 4));
	e.setBall(0, 8.5f, 4.5f, 8.0f, 0.0f);
	uint8_t seen = gatesSeen(e, 0.5f);
	CHECK(e.getVx(0) < 0.0f);
	CHECK(seen & 8); // gate 4 is bumpers
	CHECK(e.getX(0) < 10.0f);
}

TEST(Billiards, bumpersCanBeClearedAndToggledBack) {
	BilliardsEngine e;
	e.toggleBumper(3, 3);
	e.toggleBumper(3, 3);
	CHECK(!e.hasBumper(3, 3));
	e.toggleBumper(5, 2);
	e.clearBumpers();
	CHECK(!e.hasBumper(5, 2));
	e.toggleBumper(99, 99); // off the table does nothing
}

TEST(Billiards, headOnBallsSwapVelocities) {
	BilliardsEngine e;
	e.setNumBalls(2);
	e.setBall(0, 6.0f, 4.0f, 5.0f, 0.0f);
	e.setBall(1, 8.0f, 4.0f, -3.0f, 0.0f);
	advance(e, 0.3f);
	DOUBLES_EQUAL(-3.0, e.getVx(0), 0.01);
	DOUBLES_EQUAL(5.0, e.getVx(1), 0.01);
	CHECK(e.getHitCount(0) >= 1);
	CHECK(e.getHitCount(1) >= 1);
}

TEST(Billiards, ballsModeFiresTheGateOfTheBallThatHit) {
	BilliardsEngine e;
	e.setNumBalls(2);
	e.setGateMode(GateMode::BALLS);
	e.setBall(0, 8.0f, 4.0f, 0.0f, 0.0f);
	e.setBall(1, 8.0f, 6.0f, 0.0f, 10.0f); // goes up into the ceiling
	e.setSpeed(10.0f);
	e.setBall(0, 8.0f, 4.0f, 0.0f, 0.0f);
	e.setBall(1, 8.0f, 6.0f, 0.0f, 10.0f);
	uint8_t seen = gatesSeen(e, 0.3f);
	CHECK(seen & 2);   // ball 2
	CHECK(!(seen & 1)); // ball 1 never hit anything... (it is launched again, but a launch is not a hit)
}

TEST(Billiards, cvFollowsTheChosenBall) {
	BilliardsEngine e;
	e.setNumBalls(2);
	e.setCvBall(1);
	e.setRange(10.0f);
	e.setBall(1, 4.0f, 2.0f, 0.0f, 0.0f);
	DOUBLES_EQUAL(2.5, e.getVolts(0), 0.001); // 4 / 16 of 10 V
	DOUBLES_EQUAL(2.5, e.getVolts(1), 0.001); // 2 / 8 of 10 V
	e.setBall(1, 16.0f, 8.0f, 0.0f, 0.0f);
	CHECK(e.getCVCode(0) <= 65520);
	CHECK(e.getCVCode(0) > 60000);
}

TEST(Billiards, tempoModeScalesTime) {
	BilliardsEngine a = single();
	BilliardsEngine b = single();
	b.setTimeMode(deluge::marbles::TimeMode::TEMPO);
	a.setBall(0, 2.0f, 4.0f, 4.0f, 0.0f);
	b.setBall(0, 2.0f, 4.0f, 4.0f, 0.0f);
	for (int i = 0; i < 100; i++) {
		a.step(kDt, 240.0f);
		b.step(kDt, 240.0f);
	}
	DOUBLES_EQUAL(2.0 + 4.0 * 0.2, a.getX(0), 0.01);
	DOUBLES_EQUAL(2.0 + 4.0 * 0.4, b.getX(0), 0.01);
}

TEST(Billiards, resetRepeatsTheSameRun) {
	BilliardsEngine e;
	e.setGravity(5.0f);
	e.setBounce(0.6f);
	advance(e, 10.0f);
	float x = e.getX(0);
	uint32_t hits = e.getTotalHits();
	e.reset();
	advance(e, 10.0f);
	DOUBLES_EQUAL(x, e.getX(0), 0.0001);
	CHECK_EQUAL(hits, e.getTotalHits());
}

TEST(Billiards, speedChangeRescalesMovingBalls) {
	BilliardsEngine e = single();
	e.setBall(0, 8.0f, 4.0f, 3.0f, 4.0f);
	e.setSpeed(e.params().speed * 2.0f);
	DOUBLES_EQUAL(6.0, e.getVx(0), 0.001);
	DOUBLES_EQUAL(8.0, e.getVy(0), 0.001);
}

TEST(Billiards, gatePulsesLastAboutTenMilliseconds) {
	BilliardsEngine e = single();
	e.setBall(0, 15.9f, 4.0f, 8.0f, 0.0f);
	for (int i = 0; i < 30 && e.getTotalHits() == 0; i++) {
		e.step(kDt, 120.0f);
	}
	CHECK(e.getTotalHits() >= 1);
	int high = 0;
	while (e.getGateLevels() && high < 100) {
		e.step(kDt, 120.0f);
		high++;
	}
	CHECK(high >= 3 && high <= 6);
}
