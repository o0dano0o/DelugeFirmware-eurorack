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
#include "processing/engines/crossmod_engine.h"
#include <cmath>
#include <cstdint>
#include <cstdlib>

using namespace deluge::crossmod;

namespace {

struct Rig {
	CrossmodEngine e;
	float sources[kMaxSources];
	float current[kMaxTargets];
	bool available[kMaxTargets];
	float out[kMaxTargets];
	bool write[kMaxTargets];

	Rig() {
		for (int i = 0; i < kMaxSources; i++) {
			sources[i] = 0.5f;
		}
		for (int i = 0; i < kMaxTargets; i++) {
			current[i] = 0.5f;
			available[i] = true;
			out[i] = 0.0f;
			write[i] = false;
		}
	}
	/// Runs an update and, like the runner, feeds written values back as the targets' current settings
	void tick() {
		e.update(sources, current, available, out, write);
		for (int i = 0; i < kMaxTargets; i++) {
			if (write[i]) {
				current[i] = out[i];
			}
		}
	}
};

} // namespace

TEST_GROUP(CrossmodEngineTests){};

TEST(CrossmodEngineTests, noRoutesChangeNothing) {
	Rig r;
	r.tick();
	for (int i = 0; i < kMaxTargets; i++) {
		CHECK_FALSE(r.write[i]);
		DOUBLES_EQUAL(0.5, r.out[i], 1e-6);
	}
}

TEST(CrossmodEngineTests, sourceAtHalfDoesNothing) {
	Rig r;
	r.e.setRoute(0, 2, 3, 20);
	r.sources[2] = 0.5f;
	r.tick();
	DOUBLES_EQUAL(0.5, r.current[3], 1e-6);
}

TEST(CrossmodEngineTests, fullDepthSweepsTheWholeRange) {
	Rig r;
	r.current[3] = 0.5f;
	r.e.setRoute(0, 2, 3, 20);
	r.sources[2] = 1.0f;
	r.tick();
	DOUBLES_EQUAL(1.0, r.current[3], 1e-6); // 0.5 + 1 x (1 x 2 - 1) clamps at 1
	r.sources[2] = 0.0f;
	r.tick();
	DOUBLES_EQUAL(0.0, r.current[3], 1e-6);
}

TEST(CrossmodEngineTests, halfDepthMovesByAQuarterOfTheRange) {
	Rig r;
	r.e.setRoute(0, 1, 4, 10);
	r.sources[1] = 1.0f;
	r.tick();
	DOUBLES_EQUAL(1.0, r.current[4], 1e-6); // base 0.5 + 0.5
	r.sources[1] = 0.75f;
	r.tick();
	DOUBLES_EQUAL(0.75, r.current[4], 1e-6); // base 0.5 + 0.5 x 0.5
}

TEST(CrossmodEngineTests, negativeDepthInverts) {
	Rig r;
	r.e.setRoute(0, 1, 4, -10);
	r.sources[1] = 1.0f;
	r.tick();
	DOUBLES_EQUAL(0.0, r.current[4], 1e-6);
}

TEST(CrossmodEngineTests, depthZeroChangesNothing) {
	Rig r;
	r.e.setRoute(0, 1, 4, 0);
	r.sources[1] = 1.0f;
	r.tick();
	CHECK_FALSE(r.write[4]);
	DOUBLES_EQUAL(0.5, r.current[4], 1e-6);
}

TEST(CrossmodEngineTests, baseDoesNotDrift) {
	Rig r;
	r.current[5] = 0.3f;
	r.e.setRoute(0, 1, 5, 10);
	for (int i = 0; i < 200; i++) {
		r.sources[1] = (i % 2) ? 0.75f : 0.25f;
		r.tick();
	}
	r.sources[1] = 0.75f;
	r.tick();
	DOUBLES_EQUAL(0.55, r.current[5], 1e-5); // still 0.3 + 0.5 x 0.5, not accumulated
}

TEST(CrossmodEngineTests, severalRoutesOnOneTargetAdd) {
	Rig r;
	r.e.setRoute(0, 1, 6, 10);
	r.e.setRoute(1, 2, 6, 10);
	r.sources[1] = 0.75f;
	r.sources[2] = 0.75f;
	r.tick();
	DOUBLES_EQUAL(1.0, r.current[6], 1e-6); // 0.5 + 0.25 + 0.25
}

TEST(CrossmodEngineTests, outputIsAlwaysBetweenZeroAndOne) {
	Rig r;
	r.e.setRoute(0, 0, 7, 20);
	r.e.setRoute(1, 1, 7, 20);
	r.e.setRoute(2, 2, 7, 20);
	r.e.setRoute(3, 3, 7, 20);
	srand(5);
	for (int i = 0; i < 2000; i++) {
		for (int s = 0; s < kMaxSources; s++) {
			r.sources[s] = static_cast<float>(rand()) / RAND_MAX * 3.0f - 1.0f; // deliberately out of range
		}
		r.tick();
		CHECK(r.current[7] >= 0.0f && r.current[7] <= 1.0f);
	}
}

TEST(CrossmodEngineTests, notANumberSourceIsHalf) {
	Rig r;
	r.e.setRoute(0, 1, 8, 20);
	r.sources[1] = std::nanf("");
	r.tick();
	DOUBLES_EQUAL(0.5, r.current[8], 1e-6);
}

TEST(CrossmodEngineTests, aHandEditBecomesTheNewBase) {
	Rig r;
	r.e.setRoute(0, 1, 9, 10);
	r.sources[1] = 0.75f;
	r.tick();
	DOUBLES_EQUAL(0.75, r.current[9], 1e-6);
	r.current[9] = 0.2f; // the user turns the setting
	r.tick();
	DOUBLES_EQUAL(0.2 + 0.25, r.current[9], 1e-5);
}

TEST(CrossmodEngineTests, removingTheRouteRestoresTheBase) {
	Rig r;
	r.current[10] = 0.4f;
	r.e.setRoute(0, 1, 10, 10);
	r.sources[1] = 1.0f;
	r.tick();
	DOUBLES_EQUAL(0.9, r.current[10], 1e-6);
	r.e.clearRoute(0);
	r.tick();
	CHECK(r.write[10]);
	DOUBLES_EQUAL(0.4, r.current[10], 1e-6);
	r.tick();
	CHECK_FALSE(r.write[10]); // and then leaves it alone
}

TEST(CrossmodEngineTests, aRestoreDoesNotOverwriteAHandEdit) {
	Rig r;
	r.e.setRoute(0, 1, 11, 10);
	r.sources[1] = 1.0f;
	r.tick();
	r.current[11] = 0.1f; // edited in the same moment the route is removed
	r.e.clearRoute(0);
	r.tick();
	CHECK_FALSE(r.write[11]);
	DOUBLES_EQUAL(0.1, r.current[11], 1e-6);
}

TEST(CrossmodEngineTests, unavailableTargetsAreLeftAlone) {
	Rig r;
	r.e.setRoute(0, 1, 12, 20);
	r.available[12] = false;
	r.sources[1] = 1.0f;
	r.tick();
	CHECK_FALSE(r.write[12]);
	DOUBLES_EQUAL(0.5, r.current[12], 1e-6);
}

TEST(CrossmodEngineTests, aLoopOfRoutesStaysFinite) {
	// target 0 feeds back into source 0
	Rig r;
	r.e.setRoute(0, 0, 0, 20);
	r.sources[0] = 0.9f;
	for (int i = 0; i < 1000; i++) {
		r.tick();
		r.sources[0] = r.current[0];
		CHECK(std::isfinite(r.current[0]));
		CHECK(r.current[0] >= 0.0f && r.current[0] <= 1.0f);
	}
}

TEST(CrossmodEngineTests, badIndexesAreIgnored) {
	Rig r;
	r.e.setRoute(-1, 0, 0, 5);
	r.e.setRoute(kNumRoutes, 0, 0, 5);
	r.e.setSource(0, 99);
	r.e.setTarget(0, -5);
	r.e.setDepth(0, 1000);
	CHECK_EQUAL(-1, r.e.route(0).source);
	CHECK_EQUAL(-1, r.e.route(0).target);
	CHECK_EQUAL(kMaxDepthSteps, r.e.route(0).depth);
}

TEST(CrossmodEngineTests, activeAndUsedFlags) {
	CrossmodEngine e;
	CHECK_FALSE(e.anyUsed());
	CHECK_FALSE(e.anyActive());
	e.setSource(0, 3);
	CHECK(e.anyUsed());
	CHECK_FALSE(e.anyActive());
	e.setTarget(0, 2);
	e.setDepth(0, 4);
	CHECK(e.anyActive());
	e.reset();
	CHECK_FALSE(e.anyUsed());
}
