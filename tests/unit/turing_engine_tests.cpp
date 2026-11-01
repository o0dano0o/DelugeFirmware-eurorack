#include "CppUTest/TestHarness.h"
#include "processing/engines/turing_engine.h"

#include <set>
#include <vector>

using namespace deluge::turing;

namespace {
uint32_t lcg = 12345;
float nextRandom() {
	lcg = lcg * 1664525u + 1013904223u;
	return static_cast<float>(lcg >> 8) / 16777216.0f;
}
} // namespace

TEST_GROUP(TuringEngineTests){};

TEST(TuringEngineTests, lengthIsClampedAndRegisterMasked) {
	TuringEngine t;
	t.setLength(100);
	CHECK_EQUAL(16, t.getLength());
	t.setLength(0);
	CHECK_EQUAL(2, t.getLength());
	t.setLength(8);
	t.setRegister(0xFFFF);
	CHECK_EQUAL(0xFF, t.getRegister());
	t.setLength(4);
	CHECK_EQUAL(0x0F, t.getRegister());
}

TEST(TuringEngineTests, lockedSequenceLoops) {
	TuringEngine t;
	t.setLength(8);
	t.randomise(42);
	t.setFlipProbability(0.0f);
	std::vector<uint8_t> first;
	for (int i = 0; i < 8; i++) {
		first.push_back(t.getValue());
		t.clock(nextRandom());
	}
	for (int loop = 0; loop < 5; loop++) {
		for (int i = 0; i < 8; i++) {
			CHECK_EQUAL(first[i], t.getValue());
			t.clock(nextRandom());
		}
	}
}

TEST(TuringEngineTests, alwaysFlipRepeatsAtTwiceTheLengthWithInvertedHalves) {
	TuringEngine t;
	t.setLength(8);
	t.setRegister(0xA5);
	t.setFlipProbability(1.0f);
	uint16_t start = t.getRegister();
	for (int i = 0; i < 8; i++) {
		t.clock(0.5f);
	}
	CHECK_EQUAL(static_cast<uint16_t>(~start & 0xFF), t.getRegister());
	for (int i = 0; i < 8; i++) {
		t.clock(0.5f);
	}
	CHECK_EQUAL(start, t.getRegister());
}

TEST(TuringEngineTests, halfProbabilityGivesRandomBits) {
	TuringEngine t;
	t.setLength(16);
	t.setRegister(0);
	t.setFlipProbability(0.5f);
	int ones = 0;
	const int n = 20000;
	for (int i = 0; i < n; i++) {
		t.clock(nextRandom());
		ones += t.getRegister() & 1;
	}
	DOUBLES_EQUAL(0.5, static_cast<double>(ones) / n, 0.02);
}

TEST(TuringEngineTests, flipReportAndProbabilityBounds) {
	TuringEngine t;
	t.setFlipProbability(-3.0f);
	CHECK(t.getFlipProbability() == 0.0f);
	t.setFlipProbability(7.0f);
	CHECK(t.getFlipProbability() == 1.0f);
	t.setFlipProbability(0.3f);
	CHECK_TRUE(t.clock(0.1f));
	CHECK_FALSE(t.clock(0.9f));
}

TEST(TuringEngineTests, randomiseIsDeterministicAndVaries) {
	TuringEngine a, b;
	a.randomise(7);
	b.randomise(7);
	CHECK_EQUAL(a.getRegister(), b.getRegister());
	std::set<uint16_t> seen;
	for (uint32_t s = 1; s < 50; s++) {
		a.randomise(s);
		seen.insert(a.getRegister());
	}
	CHECK_TRUE(seen.size() > 30);
}

TEST(TuringEngineTests, pitchStaysInScaleAndSpansTheRange) {
	const uint16_t major = 0b101010110101; // 0 2 4 5 7 9 11
	std::set<int> seen;
	int last = -1;
	for (int v = 0; v < 256; v++) {
		int n = TuringEngine::noteOffsetFromValue(static_cast<uint8_t>(v), major, 2);
		CHECK_TRUE(n >= last); // never goes down as the value rises
		last = n;
		int semitone = n % 12;
		CHECK_TRUE((major >> semitone) & 1);
		CHECK_TRUE(n >= 0 && n < 24);
		seen.insert(n);
	}
	CHECK_EQUAL(0, *seen.begin());
	CHECK_EQUAL(23, *seen.rbegin()); // 11 in the second octave
	CHECK_EQUAL(14, static_cast<int>(seen.size()));
}

TEST(TuringEngineTests, emptyScaleMeansChromatic) {
	CHECK_EQUAL(0, TuringEngine::noteOffsetFromValue(0, 0, 1));
	CHECK_EQUAL(11, TuringEngine::noteOffsetFromValue(255, 0, 1));
	CHECK_EQUAL(23, TuringEngine::noteOffsetFromValue(255, 0, 2));
}
