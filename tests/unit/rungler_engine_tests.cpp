#include "CppUTest/TestHarness.h"
#include "processing/engines/rungler_engine.h"
#include <cmath>
#include <vector>

using namespace deluge::rungler;

namespace {

constexpr float kDt = 0.002f;

/// Runs until `shifts` more shifts have happened and returns the register after each one
std::vector<uint8_t> collect(RunglerEngine& e, int32_t shifts, float bpm = 120.0f) {
	std::vector<uint8_t> values;
	uint32_t target = e.getShiftCount() + shifts;
	int32_t guard = 0;
	while (e.getShiftCount() < target && guard++ < 10000000) {
		uint32_t before = e.getShiftCount();
		e.step(kDt, bpm);
		if (e.getShiftCount() != before) {
			values.push_back(e.getRegister());
		}
	}
	return values;
}

Params locked() {
	Params p;
	p.chance = 0.0f;
	p.modAmount = 0.0f;
	p.freqB = 20.0f;
	return p;
}

} // namespace

TEST_GROUP(RunglerEngineTests){};

TEST(RunglerEngineTests, sameSettingsGiveTheSameSequence) {
	RunglerEngine a, b;
	Params p;
	p.chance = 0.7f;
	a.setParams(p);
	b.setParams(p);
	a.reset();
	b.reset();
	CHECK(collect(a, 200) == collect(b, 200));
}

TEST(RunglerEngineTests, resetRestartsTheSequence) {
	RunglerEngine e;
	Params p;
	p.chance = 0.7f;
	e.setParams(p);
	e.reset();
	auto first = collect(e, 100);
	e.reset();
	CHECK(first == collect(e, 100));
}

TEST(RunglerEngineTests, startingRegisterIsNeverEmpty) {
	RunglerEngine e;
	CHECK(e.getRegister() != 0);
}

TEST(RunglerEngineTests, chanceZeroRepeatsEveryEightShifts) {
	RunglerEngine e;
	e.setParams(locked());
	e.reset();
	auto v = collect(e, 64);
	for (size_t i = 8; i < v.size(); i++) {
		CHECK_EQUAL(v[i - 8], v[i]);
	}
}

TEST(RunglerEngineTests, chanceZeroKeepsTheNumberOfSetBits) {
	RunglerEngine e;
	e.setParams(locked());
	e.reset();
	auto ones = [](uint8_t b) {
		int n = 0;
		for (int i = 0; i < 8; i++) {
			n += (b >> i) & 1;
		}
		return n;
	};
	int start = ones(e.getRegister());
	for (uint8_t r : collect(e, 40)) {
		CHECK_EQUAL(start, ones(r));
	}
}

TEST(RunglerEngineTests, fullChanceDisturbsThePattern) {
	RunglerEngine e;
	Params p = locked();
	p.chance = 1.0f;
	p.freqA = 7.3f;
	e.setParams(p);
	e.reset();
	auto v = collect(e, 200);
	bool repeats = true;
	for (size_t i = 8; i < v.size(); i++) {
		if (v[i - 8] != v[i]) {
			repeats = false;
		}
	}
	CHECK(!repeats);
}

TEST(RunglerEngineTests, oneShiftMovesTheBitsUpByOne) {
	RunglerEngine e;
	e.setParams(locked());
	e.setRegister(0b00000011);
	collect(e, 1);
	CHECK_EQUAL(0b00000110, e.getRegister());
	e.setRegister(0b10000000);
	collect(e, 1);
	CHECK_EQUAL(0b00000001, e.getRegister());
}

TEST(RunglerEngineTests, shiftRateFollowsOscillatorB) {
	RunglerEngine e;
	Params p = locked();
	p.freqB = 10.0f;
	e.setParams(p);
	e.reset();
	for (int i = 0; i < 1000; i++) { // 2 seconds
		e.step(kDt, 120.0f);
	}
	CHECK(e.getShiftCount() >= 19 && e.getShiftCount() <= 21);
}

TEST(RunglerEngineTests, tempoModeFollowsTheBpm) {
	RunglerEngine slow, fast;
	Params p = locked();
	p.timeMode = deluge::marbles::TimeMode::TEMPO;
	p.rateB = 3; // 1/8: 2 per beat
	slow.setParams(p);
	fast.setParams(p);
	slow.reset();
	fast.reset();
	for (int i = 0; i < 2500; i++) { // 5 seconds
		slow.step(kDt, 60.0f);
		fast.step(kDt, 120.0f);
	}
	// 60 BPM: 2 shifts a second, 120 BPM: 4 a second
	CHECK(slow.getShiftCount() >= 9 && slow.getShiftCount() <= 11);
	CHECK(fast.getShiftCount() >= 19 && fast.getShiftCount() <= 21);
}

TEST(RunglerEngineTests, modBendsTheRatesInOppositeDirections) {
	RunglerEngine e;
	Params p;
	p.modAmount = 1.0f;
	e.setParams(p);
	e.setRegister(0);
	float a0 = e.getRateA(120.0f), b0 = e.getRateB(120.0f);
	e.setRegister(255);
	float a1 = e.getRateA(120.0f), b1 = e.getRateB(120.0f);
	CHECK(a1 > a0);
	CHECK(b1 < b0);
	e.setModAmount(0.0f);
	DOUBLES_EQUAL(p.freqA, e.getRateA(120.0f), 0.001);
	DOUBLES_EQUAL(p.freqB, e.getRateB(120.0f), 0.001);
}

TEST(RunglerEngineTests, cv1HasEightStepsAcrossTheRange) {
	RunglerEngine e;
	Params p = locked();
	p.rangeVolts = 7.0f;
	e.setParams(p);
	for (int bits = 0; bits < 8; bits++) {
		e.setRegister(static_cast<uint8_t>(0xA0 | bits));
		e.step(kDt, 120.0f);
		DOUBLES_EQUAL(bits / 7.0f * 7.0f, e.getVolts(0), 0.02);
	}
}

TEST(RunglerEngineTests, cv2IsTheWeightedRegister) {
	RunglerEngine e;
	Params p = locked();
	p.slew = 0.0f;
	p.rangeVolts = 5.0f;
	e.setParams(p);
	e.setRegister(255);
	e.step(kDt, 120.0f);
	DOUBLES_EQUAL(5.0, e.getVolts(1), 0.02);
	e.setRegister(0);
	e.step(kDt, 120.0f);
	DOUBLES_EQUAL(0.0, e.getVolts(1), 0.02);
}

TEST(RunglerEngineTests, slewDelaysCv2) {
	RunglerEngine e;
	Params p = locked();
	p.slew = 1.0f;
	e.setParams(p);
	e.setRegister(0);
	for (int i = 0; i < 1000; i++) {
		e.step(kDt, 120.0f);
		if (e.getShiftCount() > 0) {
			break;
		}
	}
	float before = e.getVolts(1);
	e.setRegister(255);
	e.step(kDt, 120.0f);
	float after = e.getVolts(1);
	CHECK(after > before);
	CHECK(after < 5.0f * 0.2f); // moved only a little in 2 ms against a 0.5 s slew
}

TEST(RunglerEngineTests, codesStayInRange) {
	RunglerEngine e;
	Params p;
	p.rangeVolts = 10.0f;
	p.slew = 0.0f;
	e.setParams(p);
	for (int bits = 0; bits < 256; bits += 5) {
		e.setRegister(static_cast<uint8_t>(bits));
		e.step(kDt, 120.0f);
		CHECK(e.getCVCode(0) <= 65520);
		CHECK(e.getCVCode(1) <= 65520);
	}
	CHECK_EQUAL(0, e.getCVCode(5));
}

TEST(RunglerEngineTests, gatesShowTheNewestThreeBits) {
	RunglerEngine e;
	e.setParams(locked());
	e.setRegister(0b00000101);
	CHECK_EQUAL(0b1010, e.getGateLevels() & 0b1110);
	e.setRegister(0b00000010);
	CHECK_EQUAL(0b0100, e.getGateLevels() & 0b1110);
}

TEST(RunglerEngineTests, gateOnePulsesForAboutTenMilliseconds) {
	RunglerEngine e;
	Params p = locked();
	p.freqB = 2.0f;
	e.setParams(p);
	e.reset();
	int highSteps = 0;
	bool seenHigh = false;
	for (int i = 0; i < 1500 && !(seenHigh && !(e.getGateLevels() & 1)); i++) {
		e.step(kDt, 120.0f);
		if (e.getGateLevels() & 1) {
			seenHigh = true;
			highSteps++;
		}
	}
	CHECK(seenHigh);
	CHECK(highSteps >= 4 && highSteps <= 6); // 10 ms at 2 ms a step
}

TEST(RunglerEngineTests, extremeInputsAreClamped) {
	RunglerEngine e;
	e.setChance(5.0f);
	e.setModAmount(-1.0f);
	e.setSlew(9.0f);
	e.setRange(99.0f);
	e.setFreqA(0.0f);
	e.setFreqB(1e9f);
	e.setRateA(-4);
	e.setRateB(99);
	const Params& p = e.params();
	DOUBLES_EQUAL(1.0, p.chance, 0.0001);
	DOUBLES_EQUAL(0.0, p.modAmount, 0.0001);
	DOUBLES_EQUAL(1.0, p.slew, 0.0001);
	DOUBLES_EQUAL(10.0, p.rangeVolts, 0.0001);
	CHECK(p.freqA > 0.0f);
	CHECK(p.freqB <= 100.0f);
	CHECK_EQUAL(0, p.rateA);
	CHECK_EQUAL(7, p.rateB);
	for (int i = 0; i < 500; i++) {
		e.step(kDt, 0.0f);
	}
	CHECK(e.getCVCode(0) <= 65520);
}

TEST(RunglerEngineTests, toggleBitFlipsOneBit) {
	RunglerEngine e;
	e.setRegister(0);
	e.toggleBit(3);
	CHECK_EQUAL(8, e.getRegister());
	e.toggleBit(3);
	CHECK_EQUAL(0, e.getRegister());
	e.toggleBit(9);
	CHECK_EQUAL(0, e.getRegister());
}
