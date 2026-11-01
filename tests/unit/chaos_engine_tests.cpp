#include "CppUTest/TestHarness.h"
#include "processing/engines/chaos_engine.h"
#include "processing/engines/chaos_probability.h"

#include <cmath>
#include <cstring>

using namespace deluge::chaos;

namespace {
constexpr float kDt = 0.002f;

struct Stats {
	double sum[2]{}, sumSq[2]{}, cross{};
	double minV[2]{100, 100}, maxV[2]{-100, -100};
	int32_t rising[kNumGateRules]{};
	int32_t highSteps[kNumGateRules]{};
	int32_t n{0};
};

Stats runFor(ChaosEngine& e, float seconds, float bpm = 120.0f) {
	Stats s;
	uint8_t prev = e.getGateLevels();
	int32_t steps = static_cast<int32_t>(seconds / kDt);
	for (int32_t i = 0; i < steps; i++) {
		e.step(kDt, bpm);
		for (int c = 0; c < 2; c++) {
			double v = e.getCVVolts(c);
			s.sum[c] += v;
			s.sumSq[c] += v * v;
			s.minV[c] = std::fmin(s.minV[c], v);
			s.maxV[c] = std::fmax(s.maxV[c], v);
		}
		s.cross += static_cast<double>(e.getCVVolts(0)) * e.getCVVolts(1);
		uint8_t g = e.getGateLevels();
		for (int r = 0; r < kNumGateRules; r++) {
			if ((g >> r & 1) && !(prev >> r & 1)) {
				s.rising[r]++;
			}
			if (g >> r & 1) {
				s.highSteps[r]++;
			}
		}
		prev = g;
		s.n++;
	}
	return s;
}

double corr(const Stats& s) {
	double m0 = s.sum[0] / s.n, m1 = s.sum[1] / s.n;
	double v0 = s.sumSq[0] / s.n - m0 * m0, v1 = s.sumSq[1] / s.n - m1 * m1;
	double cov = s.cross / s.n - m0 * m1;
	return cov / std::sqrt(v0 * v1);
}
} // namespace

TEST_GROUP(ChaosEngineTests){};

TEST(ChaosEngineTests, deterministic) {
	ChaosEngine a, b;
	for (int i = 0; i < 20000; i++) {
		a.step(kDt, 120);
		b.step(kDt, 120);
		CHECK_EQUAL(a.getCVCode(0), b.getCVCode(0));
		CHECK_EQUAL(a.getCVCode(1), b.getCVCode(1));
		CHECK_EQUAL(a.getGateLevels(), b.getGateLevels());
	}
}

TEST(ChaosEngineTests, resetMatchesFreshEngine) {
	ChaosEngine a, b;
	runFor(a, 30);
	a.reset();
	for (int i = 0; i < 5000; i++) {
		a.step(kDt, 120);
		b.step(kDt, 120);
		CHECK_EQUAL(a.getCVCode(0), b.getCVCode(0));
		CHECK_EQUAL(a.getCVCode(1), b.getCVCode(1));
		CHECK_EQUAL(a.getGateLevels(), b.getGateLevels());
	}
}

TEST(ChaosEngineTests, allModelsBehave) {
	for (int m = 0; m < static_cast<int>(Model::kNumModels); m++) {
		ChaosEngine e;
		e.setModel(static_cast<Model>(m));
		Stats s = runFor(e, 600);
		for (int c = 0; c < 2; c++) {
			CHECK_TRUE(s.minV[c] >= 0.0 && s.maxV[c] <= 10.0);
			CHECK_TRUE(s.maxV[c] - s.minV[c] > 4.0); // actually moves
		}
		CHECK_TRUE(std::fabs(corr(s)) < 0.3);
		for (int r = 0; r < kNumGateRules; r++) {
			CHECK_TRUE(s.rising[r] >= 3);
		}
	}
}

TEST(ChaosEngineTests, codesWithinDacRange) {
	ChaosEngine e;
	for (int i = 0; i < 100000; i++) {
		e.step(kDt, 120);
		CHECK_TRUE(e.getCVCode(0) <= kDacMaxCode);
		CHECK_TRUE(e.getCVCode(1) <= kDacMaxCode);
	}
}

TEST(ChaosEngineTests, triggerPulseWidth) {
	ChaosEngine e;
	Stats s = runFor(e, 600);
	// rules 2 and 3 are triggers: 10 ms at 2 ms steps is 5 steps per pulse
	for (int r : {1, 2}) {
		CHECK_TRUE(s.rising[r] > 0);
		DOUBLES_EQUAL(5.0, static_cast<double>(s.highSteps[r]) / s.rising[r], 0.2);
	}
}

TEST(ChaosEngineTests, tempoModeMatchesFree) {
	ChaosEngine a, b;
	b.setTimeMode(TimeMode::TEMPO);
	// 18 / 36 / 108 beats at 90 BPM = 12 / 24 / 72 s ... use periods in beats giving 12/30/90 s at 90 BPM
	b.setPeriod(0, 18.0f);
	b.setPeriod(1, 45.0f);
	b.setPeriod(2, 135.0f);
	for (int i = 0; i < 10000; i++) {
		a.step(kDt, 90);
		b.step(kDt, 90);
		CHECK_EQUAL(a.getCVCode(0), b.getCVCode(0));
		CHECK_EQUAL(a.getCVCode(1), b.getCVCode(1));
	}
}

TEST(ChaosEngineTests, extremeInputsStayFinite) {
	ChaosEngine e;
	e.setPeriod(0, 0.0f);
	e.setPeriod(1, -5.0f);
	e.setPeriod(2, 1.0e-9f);
	for (int i = 0; i < 20000; i++) {
		e.step(kDt, i % 2 ? 0.0f : 1.0e6f);
		CHECK_TRUE(e.getCVCode(0) <= kDacMaxCode);
		CHECK_TRUE(std::fabs(e.getNormalised(0, 0)) <= 1.0f);
	}
}

TEST(ChaosEngineTests, modelSwitchGlides) {
	ChaosEngine e;
	runFor(e, 20);
	e.setModel(Model::THOMAS);
	float prev = e.getCVVolts(0);
	float maxJump = 0;
	for (int i = 0; i < 1500; i++) { // 3 s
		e.step(kDt, 120);
		maxJump = std::fmax(maxJump, std::fabs(e.getCVVolts(0) - prev));
		prev = e.getCVVolts(0);
	}
	CHECK_TRUE(maxJump < 0.2f);
}

TEST_GROUP(ChaosProbabilityTests){};

TEST(ChaosProbabilityTests, depthZeroAlwaysPasses) {
	for (float x = -1.0f; x <= 1.0f; x += 0.25f) {
		DOUBLES_EQUAL(1.0, deluge::chaos::probability::passProbability(x, 0.0f), 1e-6);
	}
}

TEST(ChaosProbabilityTests, fullDepthFollowsChaos) {
	using deluge::chaos::probability::passProbability;
	DOUBLES_EQUAL(0.0, passProbability(-1.0f, 1.0f), 1e-6);
	DOUBLES_EQUAL(0.5, passProbability(0.0f, 1.0f), 1e-6);
	DOUBLES_EQUAL(1.0, passProbability(1.0f, 1.0f), 1e-6);
	DOUBLES_EQUAL(0.75, passProbability(0.0f, 0.5f), 1e-6);
}

TEST(ChaosProbabilityTests, inputsAreClamped) {
	using deluge::chaos::probability::passProbability;
	DOUBLES_EQUAL(0.0, passProbability(-5.0f, 2.0f), 1e-6);
	DOUBLES_EQUAL(1.0, passProbability(5.0f, -2.0f), 1e-6);
}

TEST(ChaosProbabilityTests, rollerIsRepeatableAfterReset) {
	deluge::chaos::probability::Roller a;
	float first[16];
	for (float& v : first) {
		v = a.next();
	}
	a.reset();
	for (float v : first) {
		DOUBLES_EQUAL(v, a.next(), 0.0);
	}
}

TEST(ChaosProbabilityTests, rollerExtremesAndRate) {
	deluge::chaos::probability::Roller r;
	int32_t passed = 0;
	for (int32_t i = 0; i < 20000; i++) {
		CHECK(r.roll(1.0f));
		CHECK(!r.roll(0.0f));
		float v = r.next();
		CHECK(v >= 0.0f && v < 1.0f);
	}
	for (int32_t i = 0; i < 20000; i++) {
		passed += r.roll(0.3f) ? 1 : 0;
	}
	DOUBLES_EQUAL(0.3, passed / 20000.0, 0.02);
}
