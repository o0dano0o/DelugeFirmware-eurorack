#include "CppUTest/TestHarness.h"
#include "processing/engines/neurons_engine.h"
#include <cmath>

using namespace deluge::neurons;

namespace {

constexpr float kDt = 0.002f;

void advance(NeuronsEngine& e, float seconds, float bpm = 120.0f) {
	int steps = static_cast<int>(seconds / kDt);
	for (int i = 0; i < steps; i++) {
		e.step(kDt, bpm);
	}
}

Params quiet() {
	Params p;
	p.noise = 0.0f;
	p.leak = 0.0f;
	return p;
}

} // namespace

TEST_GROUP(NeuronsEngineTests){};

TEST(NeuronsEngineTests, sameSettingsGiveTheSameSpikes) {
	NeuronsEngine a, b;
	Params p;
	p.noise = 0.6f;
	p.weight[1][0] = 2;
	a.setParams(p);
	b.setParams(p);
	a.reset();
	b.reset();
	advance(a, 20.0f);
	advance(b, 20.0f);
	CHECK_EQUAL(a.getSpikeCount(), b.getSpikeCount());
	for (int i = 0; i < kNumNeurons; i++) {
		CHECK_EQUAL(a.getSpikes(i), b.getSpikes(i));
	}
}

TEST(NeuronsEngineTests, resetRestartsTheSequence) {
	NeuronsEngine e;
	Params p;
	p.noise = 0.6f;
	e.setParams(p);
	e.reset();
	advance(e, 10.0f);
	uint32_t first = e.getSpikeCount();
	e.reset();
	advance(e, 10.0f);
	CHECK_EQUAL(first, e.getSpikeCount());
}

TEST(NeuronsEngineTests, aLoneNeuronFiresAtItsRate) {
	NeuronsEngine e;
	e.setParams(quiet());
	e.reset();
	advance(e, 20.0f);
	for (int i = 0; i < kNumNeurons; i++) {
		DOUBLES_EQUAL(e.params().rate[i] * 20.0, e.getSpikes(i), 2.5);
	}
}

TEST(NeuronsEngineTests, theRateHoldsWithLeak) {
	for (float leak : {0.2f, 0.6f, 1.0f}) {
		NeuronsEngine e;
		Params p = quiet();
		p.leak = leak;
		p.rate[0] = 2.0f;
		e.setParams(p);
		e.reset();
		advance(e, 30.0f);
		DOUBLES_EQUAL(60.0, e.getSpikes(0), 3.0);
	}
}

TEST(NeuronsEngineTests, excitationSpeedsTheTargetUp) {
	NeuronsEngine alone, driven;
	Params p = quiet();
	alone.setParams(p);
	p.weight[1][0] = 2;
	driven.setParams(p);
	alone.reset();
	driven.reset();
	advance(alone, 20.0f);
	advance(driven, 20.0f);
	CHECK(driven.getSpikes(1) > alone.getSpikes(1) + 5);
	CHECK_EQUAL(alone.getSpikes(0), driven.getSpikes(0)); // nothing feeds neuron 0
}

TEST(NeuronsEngineTests, inhibitionSlowsTheTargetDown) {
	NeuronsEngine alone, held;
	Params p = quiet();
	alone.setParams(p);
	p.weight[1][0] = -2;
	held.setParams(p);
	alone.reset();
	held.reset();
	advance(alone, 20.0f);
	advance(held, 20.0f);
	CHECK(held.getSpikes(1) + 5 < alone.getSpikes(1));
}

TEST(NeuronsEngineTests, aSpikeNeverSetsOffAChainInOneStep) {
	NeuronsEngine e;
	Params p = quiet();
	for (int i = 0; i < kNumNeurons; i++) {
		for (int j = 0; j < kNumNeurons; j++) {
			p.weight[i][j] = (i == j) ? 0 : 2;
		}
	}
	e.setParams(p);
	e.reset();
	for (int i = 0; i < 5000; i++) {
		uint32_t before = e.getSpikeCount();
		e.step(kDt, 120.0f);
		CHECK(e.getSpikeCount() - before <= static_cast<uint32_t>(kNumNeurons));
	}
}

TEST(NeuronsEngineTests, membranesStayBetweenZeroAndOne) {
	NeuronsEngine e;
	Params p;
	p.noise = 1.0f;
	p.leak = 1.0f;
	e.setParams(p);
	e.applyPreset(4);
	e.reset();
	for (int i = 0; i < 20000; i++) {
		e.step(kDt, 120.0f);
		for (int n = 0; n < kNumNeurons; n++) {
			CHECK(e.getMembrane(n) >= 0.0f);
			CHECK(e.getMembrane(n) < 1.0f);
		}
	}
}

TEST(NeuronsEngineTests, tempoModeScalesTime) {
	NeuronsEngine slow, fast;
	Params p = quiet();
	p.timeMode = deluge::marbles::TimeMode::TEMPO;
	slow.setParams(p);
	fast.setParams(p);
	slow.reset();
	fast.reset();
	advance(slow, 20.0f, 60.0f);
	advance(fast, 20.0f, 240.0f);
	DOUBLES_EQUAL(4.0, static_cast<double>(fast.getSpikes(0)) / slow.getSpikes(0), 0.4);
	// at 120 BPM it matches free time
	NeuronsEngine freeRun, synced;
	freeRun.setParams(quiet());
	p.timeMode = deluge::marbles::TimeMode::TEMPO;
	synced.setParams(p);
	freeRun.reset();
	synced.reset();
	advance(freeRun, 10.0f);
	advance(synced, 10.0f, 120.0f);
	CHECK_EQUAL(freeRun.getSpikeCount(), synced.getSpikeCount());
}

TEST(NeuronsEngineTests, gatesPulseForAboutTenMilliseconds) {
	NeuronsEngine e;
	Params p = quiet();
	p.rate[0] = 1.0f;
	e.setParams(p);
	e.reset();
	int high = 0;
	bool seen = false;
	for (int i = 0; i < 2000; i++) {
		e.step(kDt, 120.0f);
		if (e.getGateLevels() & 1) {
			seen = true;
			high++;
		}
		else if (seen) {
			break;
		}
	}
	CHECK(seen);
	CHECK(high >= 4 && high <= 6);
}

TEST(NeuronsEngineTests, cvFollowsTheChosenMembrane) {
	NeuronsEngine e;
	Params p = quiet();
	p.rangeVolts = 8.0f;
	e.setParams(p);
	e.setCVSource(0, 2);
	e.setCVSource(1, kAverageSource);
	e.reset();
	advance(e, 1.234f);
	DOUBLES_EQUAL(e.getMembrane(2) * 8.0, e.getVolts(0), 0.001);
	float mean = (e.getMembrane(0) + e.getMembrane(1) + e.getMembrane(2) + e.getMembrane(3)) / 4.0f;
	DOUBLES_EQUAL(mean * 8.0, e.getVolts(1), 0.001);
}

TEST(NeuronsEngineTests, codesStayInRange) {
	NeuronsEngine e;
	Params p;
	p.rangeVolts = 10.0f;
	e.setParams(p);
	for (int i = 0; i < 5000; i++) {
		e.step(kDt, 120.0f);
		CHECK(e.getCVCode(0) <= 65520);
		CHECK(e.getCVCode(1) <= 65520);
	}
	CHECK_EQUAL(0, e.getCVCode(7));
}

TEST(NeuronsEngineTests, presetsFillTheMatrix) {
	NeuronsEngine e;
	e.applyPreset(1); // ring
	CHECK_EQUAL(2, e.params().weight[1][0]);
	CHECK_EQUAL(0, e.params().weight[0][1]);
	e.applyPreset(2); // pairs inhibit
	CHECK_EQUAL(-2, e.params().weight[0][1]);
	CHECK_EQUAL(-2, e.params().weight[3][2]);
	e.applyPreset(0);
	for (int i = 0; i < kNumNeurons; i++) {
		for (int j = 0; j < kNumNeurons; j++) {
			CHECK_EQUAL(0, e.params().weight[i][j]);
		}
	}
	e.applyPreset(99); // ignored
	CHECK_EQUAL(0, e.params().weight[0][1]);
}

TEST(NeuronsEngineTests, randomWeightsStayInRangeWithAnEmptyDiagonal) {
	NeuronsEngine e;
	for (int round = 0; round < 20; round++) {
		e.randomiseWeights();
		for (int i = 0; i < kNumNeurons; i++) {
			CHECK_EQUAL(0, e.params().weight[i][i]);
			for (int j = 0; j < kNumNeurons; j++) {
				CHECK(e.params().weight[i][j] >= -2 && e.params().weight[i][j] <= 2);
			}
		}
	}
}

TEST(NeuronsEngineTests, noiseKeepsTheAverageRateAboutRight) {
	NeuronsEngine e;
	Params p;
	p.leak = 0.3f;
	p.noise = 0.3f;
	p.rate[0] = 3.0f;
	e.setParams(p);
	e.reset();
	advance(e, 100.0f);
	double rate = e.getSpikes(0) / 100.0;
	CHECK(rate > 2.4 && rate < 4.2);
}

TEST(NeuronsEngineTests, extremeInputsAreClamped) {
	NeuronsEngine e;
	e.setRate(0, 0.0f);
	e.setRate(1, 1e9f);
	e.setRate(9, 5.0f); // ignored
	e.setWeight(0, 1, 99);
	e.setWeight(1, 0, -99);
	e.setWeight(7, 0, 1); // ignored
	e.setLeak(5.0f);
	e.setNoise(-1.0f);
	e.setRange(99.0f);
	e.setCVSource(0, 99);
	e.setCVSource(5, 1); // ignored
	const Params& p = e.params();
	CHECK(p.rate[0] > 0.0f);
	CHECK(p.rate[1] <= 16.0f);
	CHECK_EQUAL(2, p.weight[0][1]);
	CHECK_EQUAL(-2, p.weight[1][0]);
	DOUBLES_EQUAL(1.0, p.leak, 0.0001);
	DOUBLES_EQUAL(0.0, p.noise, 0.0001);
	DOUBLES_EQUAL(10.0, p.rangeVolts, 0.0001);
	CHECK_EQUAL(kAverageSource, p.cvSource[0]);
	for (int i = 0; i < 3000; i++) {
		e.step(kDt, 0.0f);
	}
	CHECK(e.getCVCode(0) <= 65520);
}
