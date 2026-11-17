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
#include "processing/engines/sandpile_engine.h"
#include <cmath>
#include <vector>

using namespace deluge::sandpile;

namespace {

constexpr float kDt = 0.002f;

void advance(SandpileEngine& e, float seconds, float bpm = 120.0f) {
	int steps = static_cast<int>(seconds / kDt);
	for (int i = 0; i < steps; i++) {
		e.step(kDt, bpm);
	}
}

bool allStable(const SandpileEngine& e) {
	for (int y = 0; y < kHeight; y++) {
		for (int x = 0; x < kWidth; x++) {
			if (e.getCell(x, y) >= kLimit) {
				return false;
			}
		}
	}
	return true;
}

/// Reference: topples one cell at a time, from a stack, in a quite different order to the engine
std::vector<int> relaxByStack(std::vector<int> g) {
	std::vector<int> stack;
	for (int i = 0; i < kNumCells; i++) {
		if (g[i] >= kLimit) {
			stack.push_back(i);
		}
	}
	while (!stack.empty()) {
		int i = stack.back();
		stack.pop_back();
		while (g[i] >= kLimit) {
			g[i] -= kLimit;
			int x = i % kWidth;
			int y = i / kWidth;
			int nx[4] = {x + 1, x - 1, x, x};
			int ny[4] = {y, y, y + 1, y - 1};
			for (int d = 0; d < 4; d++) {
				if (nx[d] < 0 || nx[d] >= kWidth || ny[d] < 0 || ny[d] >= kHeight) {
					continue;
				}
				int j = ny[d] * kWidth + nx[d];
				g[j]++;
				if (g[j] >= kLimit) {
					stack.push_back(j);
				}
			}
		}
	}
	return g;
}

uint8_t gatesSeen(SandpileEngine& e, float seconds) {
	uint8_t seen = 0;
	int steps = static_cast<int>(seconds / kDt);
	for (int i = 0; i < steps; i++) {
		e.step(kDt, 120.0f);
		seen |= e.getGateLevels();
	}
	return seen;
}

} // namespace

TEST_GROUP(Sandpile){};

TEST(Sandpile, startsAtAStablePileThatIsPartlyFull) {
	SandpileEngine e;
	CHECK(allStable(e));
	CHECK(e.getTotalGrains() > 100);
	CHECK(e.getTotalGrains() < 3 * kNumCells);
	CHECK_EQUAL(0u, e.getTotalDropped());
}

TEST(Sandpile, grainsAreConservedApartFromThoseLostOverTheEdge) {
	SandpileEngine e;
	e.clear();
	e.setDropRate(20.0f);
	advance(e, 120.0f);
	CHECK(e.getTotalDropped() > 1000);
	CHECK_EQUAL(static_cast<int>(e.getTotalDropped() - e.getTotalLost()), e.getTotalGrains());
	CHECK(allStable(e) || e.isSliding());
}

TEST(Sandpile, finalStateDoesNotDependOnToppleOrder) {
	uint32_t seed = 12345;
	for (int trial = 0; trial < 20; trial++) {
		SandpileEngine e;
		e.clear();
		std::vector<int> g(kNumCells);
		for (int i = 0; i < kNumCells; i++) {
			seed = seed * 1664525u + 1013904223u;
			g[i] = static_cast<int>((seed >> 24) % 9); // 0 to 8: plenty of unstable cells
			e.setCell(i % kWidth, i / kWidth, g[i]);
		}
		std::vector<int> expected = relaxByStack(g);
		e.relaxNow();
		for (int i = 0; i < kNumCells; i++) {
			CHECK_EQUAL(expected[i], e.getCell(i % kWidth, i / kWidth));
		}
	}
}

TEST(Sandpile, aSingleOverfullCellSlidesToItsNeighbours) {
	SandpileEngine e;
	e.clear();
	e.setCell(5, 3, 4);
	int topples = e.relaxNow();
	CHECK_EQUAL(1, topples);
	CHECK_EQUAL(0, e.getCell(5, 3));
	CHECK_EQUAL(1, e.getCell(4, 3));
	CHECK_EQUAL(1, e.getCell(6, 3));
	CHECK_EQUAL(1, e.getCell(5, 2));
	CHECK_EQUAL(1, e.getCell(5, 4));
}

TEST(Sandpile, grainsFallOffTheEdge) {
	SandpileEngine e;
	e.clear();
	e.setCell(0, 0, 4);
	e.relaxNow();
	CHECK_EQUAL(2, e.getTotalGrains());
	CHECK_EQUAL(2u, e.getTotalLost());
}

TEST(Sandpile, emptyTableDropsDoNothingForAWhile) {
	SandpileEngine e;
	e.clear();
	e.setDropRate(10.0f);
	uint8_t seen = gatesSeen(e, 1.0f);
	CHECK(seen & 1);    // grains drop
	CHECK(!(seen & 2)); // none of them can set off an avalanche yet
	CHECK_EQUAL(0u, e.getAvalancheCount());
}

TEST(Sandpile, anAvalancheFiresTheStartRoundAndBigGates) {
	SandpileEngine e;
	e.clear();
	e.setBigSize(3);
	e.setDropRate(1.0f);
	for (int y = 0; y < kHeight; y++) {
		for (int x = 0; x < kWidth; x++) {
			e.setCell(x, y, 3); // a full pile: any drop is a big slide
		}
	}
	e.setDropMode(DropMode::CENTRE);
	uint8_t seen = gatesSeen(e, 8.0f);
	CHECK(seen & 1);
	CHECK(seen & 2);
	CHECK(seen & 4);
	CHECK(seen & 8);
	CHECK(e.getAvalancheCount() >= 1);
	CHECK(e.getLastSize() >= 3);
	CHECK(!e.isSliding() || e.getCurrentSize() > 0);
}

TEST(Sandpile, smallAvalanchesAreCommonAndBigOnesRare) {
	SandpileEngine e;
	e.setDropRate(2.0f);
	e.setToppleRate(100.0f);
	int small = 0;
	int big = 0;
	int biggest = 0;
	uint32_t lastCount = 0;
	for (int i = 0; i < 500000; i++) {
		e.step(kDt, 120.0f);
		if (e.getAvalancheCount() != lastCount) {
			lastCount = e.getAvalancheCount();
			int s = e.getLastSize();
			if (s <= 5) {
				small++;
			}
			if (s >= 30) {
				big++;
			}
			if (s > biggest) {
				biggest = s;
			}
		}
	}
	CHECK(lastCount > 100);
	CHECK(small > big * 2);
	CHECK(big >= 1);
	CHECK(biggest > 30);
}

TEST(Sandpile, cvOneIsTheLogOfTheAvalancheSizeAndCvTwoTheFill) {
	SandpileEngine e;
	e.clear();
	e.setRange(10.0f);
	DOUBLES_EQUAL(0.0, e.getVolts(0), 0.001);
	DOUBLES_EQUAL(0.0, e.getVolts(1), 0.001);
	for (int y = 0; y < kHeight; y++) {
		for (int x = 0; x < kWidth; x++) {
			e.setCell(x, y, 3);
		}
	}
	DOUBLES_EQUAL(10.0, e.getVolts(1), 0.001);
	CHECK(e.getCVCode(1) <= 65520);
	CHECK(e.getCVCode(1) > 65000);
}

TEST(Sandpile, centreModeDropsOnTheMiddleCell) {
	SandpileEngine e;
	e.clear();
	e.setDropMode(DropMode::CENTRE);
	e.setDropRate(10.0f);
	advance(e, 0.25f);
	CHECK_EQUAL(2, e.getCell(kWidth / 2, kHeight / 2));
	CHECK_EQUAL(2, e.getTotalGrains());
}

TEST(Sandpile, tempoModeScalesTheDropRate) {
	SandpileEngine a;
	SandpileEngine b;
	a.clear();
	b.clear();
	b.setTimeMode(deluge::marbles::TimeMode::TEMPO);
	a.setDropRate(4.0f);
	b.setDropRate(4.0f);
	for (int i = 0; i < 1000; i++) {
		a.step(kDt, 240.0f);
		b.step(kDt, 240.0f);
	}
	CHECK_EQUAL(8u, a.getTotalDropped());
	CHECK(b.getTotalDropped() >= 15 && b.getTotalDropped() <= 16);
}

TEST(Sandpile, resetRepeatsTheSameRun) {
	SandpileEngine e;
	advance(e, 30.0f);
	int grains = e.getTotalGrains();
	uint32_t av = e.getAvalancheCount();
	e.reset();
	advance(e, 30.0f);
	CHECK_EQUAL(grains, e.getTotalGrains());
	CHECK_EQUAL(av, e.getAvalancheCount());
}

TEST(Sandpile, tappingAddsAndRemovesGrains) {
	SandpileEngine e;
	e.clear();
	CHECK(e.addGrain(3, 3));
	CHECK(!e.addGrain(99, 3));
	CHECK_EQUAL(1, e.getCell(3, 3));
	CHECK(e.removeGrain(3, 3));
	CHECK(!e.removeGrain(3, 3));
}

TEST(Sandpile, toppledCellsFlashForAMoment) {
	SandpileEngine e;
	e.clear();
	e.setCell(4, 4, 3);
	e.setDropMode(DropMode::CENTRE);
	e.setCell(8, 4, 3);
	e.setDropRate(20.0f);
	bool flashed = false;
	for (int i = 0; i < 200; i++) {
		e.step(kDt, 120.0f);
		if (e.getFlash(8, 4) > 0.0f) {
			flashed = true;
		}
	}
	CHECK(flashed);
}

TEST(Sandpile, fillCvMovesWithEveryDropAndSlide) {
	SandpileEngine e;
	e.setRange(10.0f);
	float before = e.getVolts(1);
	CHECK(before > 1.0f && before < 9.0f); // the starting pile sits inside the scale, not at either end
	e.addGrain(0, 0);
	float after = e.getVolts(1);
	DOUBLES_EQUAL(10.0 / kNumCells, after - before, 0.001); // one grain is 1/128 of a grain per cell
	// over a minute it wanders by a good fraction of a volt
	float lo = after;
	float hi = after;
	e.setDropRate(5.0f);
	for (int i = 0; i < 30000; i++) {
		e.step(kDt, 120.0f);
		float v = e.getVolts(1);
		lo = v < lo ? v : lo;
		hi = v > hi ? v : hi;
	}
	CHECK(hi - lo > 0.5f);
}
