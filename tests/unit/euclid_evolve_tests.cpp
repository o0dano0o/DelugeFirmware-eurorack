#include "CppUTest/TestHarness.h"
#include "processing/engines/euclid_evolve_logic.h"

using namespace deluge::euclid;

TEST_GROUP(EuclidEvolve){};

TEST(EuclidEvolve, UpWrapsAroundTheRange) {
	EvolveRule r;
	r.direction = EvolveRule::UP;
	int8_t sign = 1;
	CHECK_EQUAL(4, evolveValue(r, 3, 1, 8, sign, 0));
	CHECK_EQUAL(1, evolveValue(r, 8, 1, 8, sign, 0));
}

TEST(EuclidEvolve, DownWrapsAroundTheRange) {
	EvolveRule r;
	r.direction = EvolveRule::DOWN;
	r.amount = 2;
	int8_t sign = 1;
	CHECK_EQUAL(6, evolveValue(r, 8, 1, 8, sign, 0));
	CHECK_EQUAL(8, evolveValue(r, 2, 1, 8, sign, 0));
}

TEST(EuclidEvolve, PingPongBouncesOffTheEnds) {
	EvolveRule r;
	r.direction = EvolveRule::PINGPONG;
	int8_t sign = 1;
	int32_t v = 6;
	int32_t seen[8];
	for (int32_t i = 0; i < 8; i++) {
		v = evolveValue(r, v, 1, 8, sign, 0);
		seen[i] = v;
	}
	// 7 8 7 6 5 4 3 2 ... turns round at 8 and never leaves 1..8
	CHECK_EQUAL(7, seen[0]);
	CHECK_EQUAL(8, seen[1]);
	CHECK_EQUAL(7, seen[2]);
	for (int32_t i = 0; i < 8; i++) {
		CHECK(seen[i] >= 1 && seen[i] <= 8);
	}
}

TEST(EuclidEvolve, RandomStaysInRange) {
	EvolveRule r;
	r.direction = EvolveRule::RANDOM;
	r.amount = 3;
	int8_t sign = 1;
	int32_t v = 1;
	for (uint32_t i = 0; i < 200; i++) {
		v = evolveValue(r, v, 1, 8, sign, i * 2654435761u >> 7);
		CHECK(v >= 1 && v <= 8);
	}
}

TEST(EuclidEvolve, EmptyRangeReturnsLow) {
	EvolveRule r;
	int8_t sign = 1;
	CHECK_EQUAL(1, evolveValue(r, 1, 1, 1, sign, 0));
}

TEST(EuclidEvolve, PackRoundTrips) {
	EvolveRule r;
	r.source = 2;
	r.every = 11;
	r.target = EvolveRule::ROTATE;
	r.direction = EvolveRule::PINGPONG;
	r.amount = 3;
	EvolveRule u = EvolveRule::unpack(r.pack());
	CHECK_EQUAL(2, u.source);
	CHECK_EQUAL(11, u.every);
	CHECK_EQUAL(EvolveRule::ROTATE, u.target);
	CHECK_EQUAL(EvolveRule::PINGPONG, u.direction);
	CHECK_EQUAL(3, u.amount);
	CHECK(!EvolveRule::unpack(0).active());
}
