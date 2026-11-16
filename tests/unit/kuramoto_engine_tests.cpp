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
#include "processing/engines/kuramoto_engine.h"
#include <cmath>

using namespace deluge::kuramoto;

namespace {

constexpr float kDt = 0.002f;

void advance(KuramotoEngine& e, float seconds, float bpm = 120.0f) {
	int steps = static_cast<int>(seconds / kDt);
	for (int i = 0; i < steps; i++) {
		e.step(kDt, bpm);
	}
}

Params alone() {
	Params p;
	p.coupling = 0.0f;
	p.noise = 0.0f;
	return p;
}

} // namespace

TEST_GROUP(KuramotoEngineTests){};

TEST(KuramotoEngineTests, sameSettingsGiveTheSameResult) {
	KuramotoEngine a, b;
	Params p;
	p.noise = 0.3f;
	a.setParams(p);
	b.setParams(p);
	advance(a, 8.0f);
	advance(b, 8.0f);
	for (int i = 0; i < kNumOscillators; i++) {
		CHECK_EQUAL(a.getWrapCount(i), b.getWrapCount(i));
		DOUBLES_EQUAL(a.getPhase(i), b.getPhase(i), 1e-9);
	}
}

TEST(KuramotoEngineTests, startsSpreadEvenlyWithZeroOrder) {
	KuramotoEngine e;
	DOUBLES_EQUAL(0.0, e.getOrder(), 1e-5);
	DOUBLES_EQUAL(0.0, e.getPhase(0), 1e-6);
	DOUBLES_EQUAL(0.25, e.getPhase(1), 1e-6);
	DOUBLES_EQUAL(0.75, e.getPhase(3), 1e-6);
}

TEST(KuramotoEngineTests, resetRestoresTheStart) {
	KuramotoEngine e;
	e.setParams(alone());
	advance(e, 5.0f);
	e.reset();
	CHECK_EQUAL(0u, e.getTotalWraps());
	DOUBLES_EQUAL(0.25, e.getPhase(1), 1e-6);
}

TEST(KuramotoEngineTests, withoutCouplingEachKeepsItsOwnRate) {
	KuramotoEngine e;
	Params p = alone();
	e.setParams(p);
	advance(e, 20.0f);
	for (int i = 0; i < kNumOscillators; i++) {
		// starts a quarter-cycle apart, so the count is the rate x 20 s give or take the start offset
		double expected = p.rate[i] * 20.0 + 0.25 * i;
		DOUBLES_EQUAL(expected, e.getWrapCount(i), 1.01);
	}
}

TEST(KuramotoEngineTests, strongCouplingLocksCloseRatesTogether) {
	KuramotoEngine e;
	Params p;
	p.coupling = 1.0f;
	e.setParams(p);
	e.applyPreset(1); // close
	advance(e, 10.0f);
	DOUBLES_EQUAL(1.0, e.getOrder(), 0.05);
	uint32_t before[kNumOscillators];
	for (int i = 0; i < kNumOscillators; i++) {
		before[i] = e.getWrapCount(i);
	}
	advance(e, 20.0f);
	uint32_t first = e.getWrapCount(0) - before[0];
	for (int i = 1; i < kNumOscillators; i++) {
		LONGS_EQUAL(first, e.getWrapCount(i) - before[i]); // all the same count: locked
	}
}

TEST(KuramotoEngineTests, equalRatesWithCouplingMergeAndWithoutStayApart) {
	KuramotoEngine locked;
	Params p;
	p.coupling = 0.5f;
	locked.setParams(p);
	locked.applyPreset(3);
	advance(locked, 10.0f);
	DOUBLES_EQUAL(1.0, locked.getOrder(), 0.02);

	KuramotoEngine apart;
	apart.setParams(alone());
	apart.applyPreset(3);
	advance(apart, 10.0f);
	DOUBLES_EQUAL(0.0, apart.getOrder(), 0.02); // same rate, never pulled: still a quarter-cycle apart
}

TEST(KuramotoEngineTests, weakCouplingDoesNotLockWideRates) {
	KuramotoEngine e;
	Params p;
	p.coupling = 0.05f;
	e.setParams(p);
	e.applyPreset(2); // harmonic
	advance(e, 20.0f);
	uint32_t a = e.getWrapCount(0);
	uint32_t d = e.getWrapCount(3);
	CHECK(d > a * 2); // 2 Hz against 0.5 Hz still about four to one
}

TEST(KuramotoEngineTests, orderAndMeanPhaseStayInRange) {
	KuramotoEngine e;
	Params p;
	p.coupling = 0.6f;
	p.noise = 0.8f;
	e.setParams(p);
	for (int i = 0; i < 20000; i++) {
		e.step(kDt, 120.0f);
		CHECK(e.getOrder() >= 0.0f && e.getOrder() <= 1.0f);
		CHECK(e.getMeanPhase() >= 0.0f && e.getMeanPhase() < 1.0f);
		for (int j = 0; j < kNumOscillators; j++) {
			CHECK(e.getPhase(j) >= 0.0f && e.getPhase(j) < 1.0f);
		}
	}
}

TEST(KuramotoEngineTests, gatePulsesLastTenMilliseconds) {
	KuramotoEngine e;
	e.setParams(alone());
	e.applyPreset(3);
	int high = 0;
	int risen = 0;
	uint8_t last = 0;
	for (int i = 0; i < 2000; i++) {
		e.step(kDt, 120.0f);
		uint8_t g = e.getGateLevels();
		if ((g & 1) && !(last & 1)) {
			risen++;
		}
		if (g & 1) {
			high++;
		}
		last = g;
	}
	CHECK(risen >= 5);
	DOUBLES_EQUAL(4.0, static_cast<double>(high) / risen, 1.0); // 10 ms, with the step it starts in already used up
}

TEST(KuramotoEngineTests, tempoSyncScalesTime) {
	KuramotoEngine slow, fast;
	Params p = alone();
	p.timeMode = deluge::marbles::TimeMode::TEMPO;
	slow.setParams(p);
	fast.setParams(p);
	advance(slow, 20.0f, 60.0f);
	advance(fast, 20.0f, 240.0f);
	DOUBLES_EQUAL(4.0, static_cast<double>(fast.getWrapCount(1)) / slow.getWrapCount(1), 0.4);
}

TEST(KuramotoEngineTests, cvCodesAreInRangeAndScaleWithRange) {
	KuramotoEngine e;
	Params p;
	p.coupling = 1.0f;
	e.setParams(p);
	e.applyPreset(3);
	advance(e, 10.0f);
	CHECK(e.getCVCode(0) <= 65520);
	CHECK(e.getCVCode(1) <= 65520);
	DOUBLES_EQUAL(5.0, e.getVolts(0), 0.1); // fully locked is the whole 5 V range
	e.setRange(10.0f);
	DOUBLES_EQUAL(10.0, e.getVolts(0), 0.2);
	DOUBLES_EQUAL(0.0, e.getVolts(7), 1e-9);
}

TEST(KuramotoEngineTests, noiseDoesNotChangeTheAverageRate) {
	KuramotoEngine e;
	Params p = alone();
	p.noise = 0.5f;
	e.setParams(p);
	advance(e, 60.0f);
	DOUBLES_EQUAL(60.0, e.getWrapCount(0), 6.0); // rate 1 Hz
}

TEST(KuramotoEngineTests, settingsAreClamped) {
	KuramotoEngine e;
	e.setRate(0, 99.0f);
	DOUBLES_EQUAL(16.0, e.params().rate[0], 1e-6);
	e.setRate(0, 0.0f);
	DOUBLES_EQUAL(0.1, e.params().rate[0], 1e-6);
	e.setCoupling(5.0f);
	DOUBLES_EQUAL(1.0, e.params().coupling, 1e-6);
	e.setNoise(-1.0f);
	DOUBLES_EQUAL(0.0, e.params().noise, 1e-6);
	e.setRange(50.0f);
	DOUBLES_EQUAL(10.0, e.params().rangeVolts, 1e-6);
	e.setRate(-1, 3.0f);
	e.setRate(kNumOscillators, 3.0f);
}

TEST(KuramotoEngineTests, presetsReplaceTheRates) {
	KuramotoEngine e;
	e.applyPreset(3);
	for (int i = 0; i < kNumOscillators; i++) {
		DOUBLES_EQUAL(1.5, e.params().rate[i], 1e-6);
	}
	e.applyPreset(2);
	DOUBLES_EQUAL(0.5, e.params().rate[0], 1e-6);
	DOUBLES_EQUAL(2.0, e.params().rate[3], 1e-6);
	STRCMP_EQUAL("Harmonic", KuramotoEngine::presetName(2));
	e.applyPreset(99); // ignored
	DOUBLES_EQUAL(0.5, e.params().rate[0], 1e-6);
}

TEST(KuramotoEngineTests, randomRatesAreInRange) {
	KuramotoEngine e;
	for (int n = 0; n < 50; n++) {
		e.randomiseRates();
		for (int i = 0; i < kNumOscillators; i++) {
			CHECK(e.params().rate[i] >= 0.5f && e.params().rate[i] <= 3.0f);
		}
	}
}

TEST(KuramotoEngineTests, strongNegativePullNeverCountsBackwardsWrapsAsCycles) {
	// with a very slow oscillator among fast ones the coupling can drag a phase backwards over zero; the count
	// must still never run ahead of the time elapsed
	KuramotoEngine e;
	Params p;
	p.coupling = 1.0f;
	e.setParams(p);
	e.setRate(0, 0.1f);
	e.setRate(1, 3.0f);
	e.setRate(2, 3.0f);
	e.setRate(3, 3.0f);
	advance(e, 30.0f);
	CHECK(e.getWrapCount(0) <= 3 * 30 + 1);
	CHECK(e.getWrapCount(0) >= 1);
}
