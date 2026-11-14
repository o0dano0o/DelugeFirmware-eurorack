#include "CppUTest/TestHarness.h"
#include "processing/engines/marbles_engine.h"
#include <cmath>
#include <vector>

using namespace deluge::marbles;

namespace {

constexpr float kDt = 0.002f;

/// Runs the engine until `clocks` more clock steps have happened and returns the values after each one
std::vector<float> collect(MarblesEngine& e, int32_t cv, int32_t clocks, float bpm = 120.0f) {
	std::vector<float> values;
	uint32_t target = e.getClockCount() + clocks;
	int32_t guard = 0;
	while (e.getClockCount() < target && guard++ < 10000000) {
		uint32_t before = e.getClockCount();
		e.step(kDt, bpm);
		if (e.getClockCount() != before) {
			values.push_back(e.getValue(cv));
		}
	}
	return values;
}

Params fastParams() {
	Params p;
	p.freePeriod = 0.02f;
	p.steps = 1.0f;
	return p;
}

} // namespace

TEST_GROUP(MarblesEngineTests){};

TEST(MarblesEngineTests, sameSettingsGiveTheSameSequence) {
	MarblesEngine a, b;
	a.setParams(fastParams());
	b.setParams(fastParams());
	auto va = collect(a, 0, 100);
	auto vb = collect(b, 0, 100);
	CHECK_EQUAL(va.size(), vb.size());
	for (size_t i = 0; i < va.size(); i++) {
		CHECK(va[i] == vb[i]);
	}
}

TEST(MarblesEngineTests, resetEqualsAFreshEngine) {
	MarblesEngine a, b;
	a.setParams(fastParams());
	b.setParams(fastParams());
	collect(a, 0, 37);
	a.randomise();
	a.reset();
	auto va = collect(a, 1, 50);
	auto vb = collect(b, 1, 50);
	for (size_t i = 0; i < va.size(); i++) {
		CHECK(va[i] == vb[i]);
	}
}

TEST(MarblesEngineTests, fullDejaVuRepeatsAfterTheLoopLength) {
	MarblesEngine e;
	Params p = fastParams();
	p.dejaVu = 1.0f;
	p.length[0] = 6;
	p.length[1] = 4;
	e.setParams(p);
	auto x1 = collect(e, 0, 30);
	for (size_t i = 6; i < x1.size(); i++) {
		DOUBLES_EQUAL(x1[i - 6], x1[i], 1e-6);
	}
	MarblesEngine f;
	f.setParams(p);
	auto x2 = collect(f, 1, 30);
	for (size_t i = 4; i < x2.size(); i++) {
		DOUBLES_EQUAL(x2[i - 4], x2[i], 1e-6);
	}
}

TEST(MarblesEngineTests, zeroDejaVuNeverRepeats) {
	MarblesEngine e;
	Params p = fastParams();
	p.dejaVu = 0.0f;
	p.length[0] = 4;
	e.setParams(p);
	auto x = collect(e, 0, 60);
	int32_t same = 0;
	for (size_t i = 4; i < x.size(); i++) {
		if (std::fabs(x[i] - x[i - 4]) < 1e-6f) {
			same++;
		}
	}
	CHECK(same < 3);
}

TEST(MarblesEngineTests, middleDejaVuChangesSomeStepsButNotAll) {
	MarblesEngine e;
	Params p = fastParams();
	p.dejaVu = 0.5f;
	p.length[0] = 4;
	e.setParams(p);
	auto x = collect(e, 0, 400);
	int32_t same = 0;
	for (size_t i = 4; i < x.size(); i++) {
		if (std::fabs(x[i] - x[i - 4]) < 1e-6f) {
			same++;
		}
	}
	CHECK(same > 40);
	CHECK(same < 360);
}

TEST(MarblesEngineTests, valuesAndCodesStayInRangeWhateverTheSettings) {
	for (int32_t trial = 0; trial < 20; trial++) {
		MarblesEngine e;
		Params p = fastParams();
		p.spread = (trial % 5) / 4.0f;
		p.bias = ((trial / 5) % 4) / 3.0f;
		p.rangeVolts = 1.0f + trial * 0.5f;
		p.steps = (trial % 3) / 2.0f;
		e.setParams(p);
		for (int32_t i = 0; i < 2000; i++) {
			e.step(kDt, 120.0f);
			for (int32_t cv = 0; cv < kNumCV; cv++) {
				CHECK(e.getValue(cv) >= 0.0f && e.getValue(cv) <= 1.0f);
				CHECK(e.getCVCode(cv) <= 65520);
			}
		}
	}
}

TEST(MarblesEngineTests, noSpreadSitsAtTheBias) {
	MarblesEngine e;
	Params p = fastParams();
	p.spread = 0.0f;
	p.bias = 0.3f;
	e.setParams(p);
	auto x = collect(e, 0, 40);
	for (float v : x) {
		DOUBLES_EQUAL(0.3, v, 1e-5);
	}
}

TEST(MarblesEngineTests, higherBiasGivesHigherValues) {
	float means[3];
	float biases[3] = {0.1f, 0.5f, 0.9f};
	for (int32_t k = 0; k < 3; k++) {
		MarblesEngine e;
		Params p = fastParams();
		p.dejaVu = 0.0f;
		p.spread = 1.0f;
		p.bias = biases[k];
		e.setParams(p);
		auto x = collect(e, 0, 600);
		float sum = 0.0f;
		for (float v : x) {
			sum += v;
		}
		means[k] = sum / x.size();
	}
	CHECK(means[0] < means[1]);
	CHECK(means[1] < means[2]);
	CHECK(means[0] < 0.3f);
	CHECK(means[2] > 0.7f);
}

TEST(MarblesEngineTests, widerSpreadGivesAWiderRange) {
	float widths[2];
	float spreads[2] = {0.2f, 1.0f};
	for (int32_t k = 0; k < 2; k++) {
		MarblesEngine e;
		Params p = fastParams();
		p.dejaVu = 0.0f;
		p.spread = spreads[k];
		e.setParams(p);
		auto x = collect(e, 0, 400);
		float lo = 1.0f, hi = 0.0f;
		for (float v : x) {
			lo = v < lo ? v : lo;
			hi = v > hi ? v : hi;
		}
		widths[k] = hi - lo;
	}
	CHECK(widths[0] < widths[1]);
	CHECK(widths[0] < 0.4f);
}

TEST(MarblesEngineTests, editingALoopStepPlaysThatValue) {
	MarblesEngine e;
	Params p = fastParams();
	p.dejaVu = 1.0f;
	p.length[0] = 4;
	p.spread = 0.8f;
	p.bias = 0.4f;
	e.setParams(p);
	e.setLoopValue(0, 2, 0.45f);
	DOUBLES_EQUAL(0.45, e.getLoopValue(0, 2), 0.01);
	auto x = collect(e, 0, 8);
	DOUBLES_EQUAL(0.45, x[2], 0.01);
	DOUBLES_EQUAL(0.45, x[6], 0.01);
}

TEST(MarblesEngineTests, gateOneFiresOnEveryStepAndTwoAndThreeShareThem) {
	MarblesEngine e;
	Params p = fastParams();
	p.bernoulli = 0.5f;
	e.setParams(p);
	int32_t rise[kNumGates] = {};
	uint8_t last = 0;
	for (int32_t i = 0; i < 20000; i++) {
		e.step(kDt, 120.0f);
		uint8_t now = e.getGateLevels();
		for (int32_t g = 0; g < kNumGates; g++) {
			if ((now & ~last) & (1 << g)) {
				rise[g]++;
			}
		}
		last = now;
	}
	CHECK_EQUAL(static_cast<int32_t>(e.getClockCount()), rise[0]);
	CHECK_EQUAL(rise[0], rise[1] + rise[2]);
	CHECK(rise[1] > 0 && rise[2] > 0);
}

TEST(MarblesEngineTests, bernoulliExtremesSendEverythingOneWay) {
	for (int32_t k = 0; k < 2; k++) {
		MarblesEngine e;
		Params p = fastParams();
		p.bernoulli = k == 0 ? 1.0f : 0.0f;
		e.setParams(p);
		int32_t rise[kNumGates] = {};
		uint8_t last = 0;
		for (int32_t i = 0; i < 5000; i++) {
			e.step(kDt, 120.0f);
			uint8_t now = e.getGateLevels();
			for (int32_t g = 0; g < kNumGates; g++) {
				if ((now & ~last) & (1 << g)) {
					rise[g]++;
				}
			}
			last = now;
		}
		CHECK(rise[k == 0 ? 1 : 2] > 0);
		CHECK_EQUAL(0, rise[k == 0 ? 2 : 1]);
	}
}

TEST(MarblesEngineTests, gateFourFiresEveryNthStep) {
	MarblesEngine e;
	Params p = fastParams();
	p.gateDivision = 4;
	e.setParams(p);
	int32_t rises = 0;
	uint8_t last = 0;
	for (int32_t i = 0; i < 10000; i++) {
		e.step(kDt, 120.0f);
		uint8_t now = e.getGateLevels();
		if ((now & ~last) & 0b1000) {
			rises++;
		}
		last = now;
	}
	int32_t clocks = static_cast<int32_t>(e.getClockCount());
	CHECK_EQUAL((clocks + 3) / 4, rises);
}

TEST(MarblesEngineTests, gatePulsesAreTenMilliseconds) {
	MarblesEngine e;
	Params p;
	p.freePeriod = 1.0f;
	e.setParams(p);
	int32_t high = 0;
	for (int32_t i = 0; i < 400; i++) {
		e.step(kDt, 120.0f);
		if (e.getGateLevels() & 1) {
			high++;
		}
	}
	// 2 ms steps, so about 5 steps for a 10 ms pulse, in one pulse over 0.8 s
	CHECK(high >= 4 && high <= 6);
}

TEST(MarblesEngineTests, tempoModeKeepsTimeWithTheBpm) {
	MarblesEngine e;
	Params p;
	p.timeMode = TimeMode::TEMPO;
	p.rate = 3; // 2 steps per beat
	e.setParams(p);
	for (int32_t i = 0; i < 5000; i++) { // 10 seconds at 120 BPM is 20 beats, 40 steps (the first fires at once)
		e.step(kDt, 120.0f);
	}
	CHECK(e.getClockCount() >= 40 && e.getClockCount() <= 42);
}

TEST(MarblesEngineTests, tempoAtAMatchingRateEqualsFreeMode) {
	MarblesEngine a, b;
	Params pa = fastParams();
	Params pb = pa;
	pa.timeMode = TimeMode::TEMPO;
	pa.rate = 3;
	pa.freePeriod = 0.5f;
	pb.timeMode = TimeMode::FREE;
	pb.freePeriod = 0.25f; // 2 steps per beat at 120 BPM
	a.setParams(pa);
	b.setParams(pb);
	for (int32_t i = 0; i < 3000; i++) {
		a.step(kDt, 120.0f);
		b.step(kDt, 120.0f);
		CHECK_EQUAL(a.getClockCount(), b.getClockCount());
		CHECK(a.getValue(0) == b.getValue(0));
		CHECK_EQUAL(a.getGateLevels(), b.getGateLevels());
	}
}

TEST(MarblesEngineTests, jitterKeepsTheAverageTempo) {
	MarblesEngine steady, wobbly;
	Params p;
	p.freePeriod = 0.1f;
	steady.setParams(p);
	p.jitter = 1.0f;
	wobbly.setParams(p);
	for (int32_t i = 0; i < 100000; i++) { // 200 seconds
		steady.step(kDt, 120.0f);
		wobbly.step(kDt, 120.0f);
	}
	float ratio = static_cast<float>(wobbly.getClockCount()) / static_cast<float>(steady.getClockCount());
	CHECK(ratio > 0.95f && ratio < 1.05f);
	// and it really does wobble: the sequences of gaps are not the same
	CHECK(steady.getClockCount() != wobbly.getClockCount() || wobbly.getValue(0) != steady.getValue(0));
}

TEST(MarblesEngineTests, glideSmoothsTheJumpsBetweenValues) {
	float biggestMove[2];
	float stepsSetting[2] = {1.0f, 0.0f};
	for (int32_t k = 0; k < 2; k++) {
		MarblesEngine e;
		Params p;
		p.freePeriod = 0.5f;
		p.dejaVu = 0.0f;
		p.spread = 1.0f;
		p.steps = stepsSetting[k];
		e.setParams(p);
		float prev = e.getValue(0);
		float biggest = 0.0f;
		for (int32_t i = 0; i < 5000; i++) {
			e.step(kDt, 120.0f);
			float move = std::fabs(e.getValue(0) - prev);
			biggest = move > biggest ? move : biggest;
			prev = e.getValue(0);
		}
		biggestMove[k] = biggest;
	}
	CHECK(biggestMove[0] > 0.1f);              // jumps
	CHECK(biggestMove[1] < biggestMove[0] / 5); // glides in small moves
}

TEST(MarblesEngineTests, extremeInputsStayFinite) {
	MarblesEngine e;
	Params p;
	p.timeMode = TimeMode::TEMPO;
	p.rate = 7;
	e.setParams(p);
	e.step(100.0f, 999.0f); // a huge time step
	e.step(0.0f, 120.0f);
	e.step(-1.0f, 120.0f);
	e.step(kDt, 0.0f);
	e.step(kDt, 1e9f);
	for (int32_t cv = 0; cv < kNumCV; cv++) {
		CHECK(std::isfinite(e.getValue(cv)));
		CHECK(e.getCVCode(cv) <= 65520);
	}
	e.setLength(0, 0);
	e.setLength(1, 99);
	CHECK_EQUAL(1, e.params().length[0]);
	CHECK_EQUAL(16, e.params().length[1]);
}

TEST(MarblesEngineTests, rateTableIsSane) {
	for (int32_t i = 1; i < kNumRates; i++) {
		CHECK(stepsPerBeat(i) > stepsPerBeat(i - 1));
	}
	CHECK(stepsPerBeat(-5) == stepsPerBeat(0));
	CHECK(stepsPerBeat(99) == stepsPerBeat(kNumRates - 1));
}
