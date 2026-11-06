#include "CppUTest/TestHarness.h"
#include "processing/engines/quantiser.h"

using deluge::quantiser::snapCode;

namespace {
constexpr float kSemi = 546.0f; // 1 V/oct at 6552 codes per volt
constexpr uint16_t kMajor = 0b101010110101; // 0 2 4 5 7 9 11
}

TEST_GROUP(Quantiser){};

TEST(Quantiser, NotesInScaleStay) {
	for (int32_t note : {0, 2, 4, 5, 7, 9, 11, 12, 26}) {
		int32_t code = static_cast<int32_t>(note * kSemi + 0.5f);
		CHECK_EQUAL(code, snapCode(code, kSemi, 0, kMajor));
	}
}

TEST(Quantiser, SnapsToNearestScaleNote) {
	// C# (1) is a tie between C and D in C major: ties go low
	CHECK_EQUAL(0, snapCode(static_cast<int32_t>(1 * kSemi), kSemi, 0, kMajor));
	// just above C# goes to D
	CHECK_EQUAL(static_cast<int32_t>(2 * kSemi), snapCode(static_cast<int32_t>(1.3f * kSemi), kSemi, 0, kMajor));
	// F# (6) is nearest F (5) or G (7); a bit above goes to G
	CHECK_EQUAL(static_cast<int32_t>(7 * kSemi), snapCode(static_cast<int32_t>(6.4f * kSemi), kSemi, 0, kMajor));
}

TEST(Quantiser, RootShiftsTheScale) {
	// D major: C# is in the scale, C is not
	int32_t cSharp = static_cast<int32_t>(1 * kSemi);
	CHECK_EQUAL(cSharp, snapCode(cSharp, kSemi, 2, kMajor));
	CHECK(snapCode(0, kSemi, 2, kMajor) != 0); // C is out of D major, so it moves
}

TEST(Quantiser, EmptyScaleIsChromatic) {
	CHECK_EQUAL(static_cast<int32_t>(3 * kSemi), snapCode(static_cast<int32_t>(3.2f * kSemi), kSemi, 0, 0));
}

TEST(Quantiser, NeverGoesNegative) {
	CHECK(snapCode(0, kSemi, 5, 0b000000010001) >= 0);
}

TEST(Quantiser, HzPerVoltIsLeftAlone) {
	CHECK_EQUAL(1234, snapCode(1234, 0.0f, 0, kMajor));
}
