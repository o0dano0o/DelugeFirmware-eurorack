#include "CppUTest/TestHarness.h"
#include "processing/engines/output_ownership.h"

using namespace deluge::owners;

namespace {
int32_t idx(Owner o) {
	return static_cast<int32_t>(o);
}
} // namespace

TEST_GROUP(OutputOwnershipTests){};

TEST(OutputOwnershipTests, startsWithEveryOutputOnTheNotePath) {
	OutputOwnership o;
	for (int32_t c = 0; c < kNumCVOutputs; c++) {
		CHECK(o.cvOwner(c) == Owner::NONE);
	}
	for (int32_t g = 0; g < kNumGateOutputs; g++) {
		CHECK(o.gateOwner(g) == Owner::NONE);
	}
	CHECK(!o.owned().any());
}

TEST(OutputOwnershipTests, claimTakesOutputsAndReportsWhatEachOwnerLost) {
	OutputOwnership o;
	o.claim(Owner::CHAOS, 0b11, 0b1111);
	Change change = o.claim(Owner::MARBLES, 0b01, 0b0110);
	CHECK(o.cvOwner(0) == Owner::MARBLES);
	CHECK(o.cvOwner(1) == Owner::CHAOS);
	CHECK(o.gateOwner(0) == Owner::CHAOS);
	CHECK(o.gateOwner(1) == Owner::MARBLES);
	CHECK(o.gateOwner(2) == Owner::MARBLES);
	CHECK(o.gateOwner(3) == Owner::CHAOS);
	CHECK_EQUAL(0b01, change.lost[idx(Owner::CHAOS)].cv);
	CHECK_EQUAL(0b0110, change.lost[idx(Owner::CHAOS)].gate);
	CHECK(!change.lost[idx(Owner::MARBLES)].any());
}

TEST(OutputOwnershipTests, claimSetsExactlyTheRequestedOutputs) {
	OutputOwnership o;
	o.claim(Owner::CHAOS, 0b11, 0b1111);
	Change change = o.claim(Owner::CHAOS, 0b10, 0b0001);
	CHECK(o.cvOwner(0) == Owner::NONE);
	CHECK(o.cvOwner(1) == Owner::CHAOS);
	CHECK(o.gateOwner(0) == Owner::CHAOS);
	CHECK(o.gateOwner(1) == Owner::NONE);
	// giving outputs up is not a loss
	CHECK(!change.any());
}

TEST(OutputOwnershipTests, claimingWhatYouAlreadyHoldChangesNothing) {
	OutputOwnership o;
	o.claim(Owner::CHAOS, 0b01, 0b0011);
	Change change = o.claim(Owner::CHAOS, 0b01, 0b0011);
	CHECK(!change.any());
	CHECK_EQUAL(0b01, o.masksOf(Owner::CHAOS).cv);
	CHECK_EQUAL(0b0011, o.masksOf(Owner::CHAOS).gate);
}

TEST(OutputOwnershipTests, releaseHandsBackToTheNotePath) {
	OutputOwnership o;
	o.claim(Owner::CHAOS, 0b10, 0b0100);
	Change change = o.release(0b10, 0b0100);
	CHECK(!o.owned().any());
	CHECK_EQUAL(0b10, change.lost[idx(Owner::CHAOS)].cv);
	CHECK_EQUAL(0b0100, change.lost[idx(Owner::CHAOS)].gate);
}

TEST(OutputOwnershipTests, pairOwnersAlwaysHoldWholeChannels) {
	OutputOwnership o;
	// asking for only the CV still gets the gate, and gates 3 and 4 are never paired
	o.claim(Owner::TURING, 0b01, 0b1000);
	CHECK(o.cvOwner(0) == Owner::TURING);
	CHECK(o.gateOwner(0) == Owner::TURING);
	CHECK(o.gateOwner(3) == Owner::NONE);
	CHECK(o.cvOwner(1) == Owner::NONE);
}

TEST(OutputOwnershipTests, losingHalfAPairLosesBothHalves) {
	OutputOwnership o;
	o.claim(Owner::VOICE, 0b10, 0b10);
	// chaos takes only the gate of channel 2
	Change change = o.claim(Owner::CHAOS, 0, 0b0010);
	CHECK(o.gateOwner(1) == Owner::CHAOS);
	CHECK(o.cvOwner(1) == Owner::NONE);
	CHECK_EQUAL(0b10, change.lost[idx(Owner::VOICE)].cv);
	CHECK_EQUAL(0b10, change.lost[idx(Owner::VOICE)].gate);
}

TEST(OutputOwnershipTests, releasingHalfAPairReleasesBothHalves) {
	OutputOwnership o;
	o.claim(Owner::TURING, 0b11, 0b11);
	o.release(0b01, 0);
	CHECK(o.cvOwner(0) == Owner::NONE);
	CHECK(o.gateOwner(0) == Owner::NONE);
	CHECK(o.cvOwner(1) == Owner::TURING);
	CHECK(o.gateOwner(1) == Owner::TURING);
}

TEST(OutputOwnershipTests, pairOwnerTakingAChannelTakesFromTwoOwners) {
	OutputOwnership o;
	o.claim(Owner::CHAOS, 0b01, 0);
	o.claim(Owner::MARBLES, 0, 0b0001);
	Change change = o.claim(Owner::TURING, 0b01, 0b01);
	CHECK(o.cvOwner(0) == Owner::TURING);
	CHECK(o.gateOwner(0) == Owner::TURING);
	CHECK_EQUAL(0b01, change.lost[idx(Owner::CHAOS)].cv);
	CHECK_EQUAL(0b0001, change.lost[idx(Owner::MARBLES)].gate);
}

TEST(OutputOwnershipTests, twoPairOwnersCannotShareAChannel) {
	OutputOwnership o;
	o.claim(Owner::TURING, 0b01, 0b01);
	Change change = o.claim(Owner::VOICE, 0b01, 0b01);
	CHECK(o.cvOwner(0) == Owner::VOICE);
	CHECK(o.gateOwner(0) == Owner::VOICE);
	CHECK(!o.masksOf(Owner::TURING).any());
	CHECK_EQUAL(0b01, change.lost[idx(Owner::TURING)].cv);
	CHECK_EQUAL(0b01, change.lost[idx(Owner::TURING)].gate);
}

TEST(OutputOwnershipTests, blockedGatesAreNeverGranted) {
	OutputOwnership o;
	// gate 2 is the run signal: chaos can't have it, and a pair owner gets only the CV of that channel
	o.claim(Owner::CHAOS, 0, 0b1110, 0b0010);
	CHECK(o.gateOwner(1) == Owner::NONE);
	CHECK(o.gateOwner(2) == Owner::CHAOS);
	o.claim(Owner::TURING, 0b10, 0b10, 0b0010);
	CHECK(o.cvOwner(1) == Owner::TURING);
	CHECK(o.gateOwner(1) == Owner::NONE);
	// losing the CV takes nothing else
	Change change = o.claim(Owner::CHAOS, 0b10, 0, 0b0010);
	CHECK(o.cvOwner(1) == Owner::CHAOS);
	CHECK_EQUAL(0b10, change.lost[idx(Owner::TURING)].cv);
	CHECK_EQUAL(0, change.lost[idx(Owner::TURING)].gate);
}

TEST(OutputOwnershipTests, everyOutputHasOneOwnerWhateverTheSequence) {
	OutputOwnership o;
	uint32_t seed = 1;
	const Owner owners[] = {Owner::CHAOS, Owner::TURING, Owner::VOICE, Owner::MARBLES};
	for (int32_t i = 0; i < 2000; i++) {
		seed = seed * 1664525u + 1013904223u;
		Owner owner = owners[(seed >> 8) % 4];
		uint8_t cv = (seed >> 12) & 0x3;
		uint8_t gate = (seed >> 16) & 0xF;
		if ((seed >> 22) % 5 == 0) {
			o.release(cv, gate);
		}
		else {
			o.claim(owner, cv, gate);
		}
		// the per-owner masks never overlap and add up to the owned set
		uint8_t cvSeen = 0;
		uint8_t gateSeen = 0;
		for (int32_t k = 1; k < kNumOwners; k++) {
			Masks m = o.masksOf(static_cast<Owner>(k));
			CHECK_EQUAL(0, cvSeen & m.cv);
			CHECK_EQUAL(0, gateSeen & m.gate);
			cvSeen |= m.cv;
			gateSeen |= m.gate;
			// pair owners always hold matching CV and gate bits on channels 1 and 2
			if (OutputOwnership::holdsPairs(static_cast<Owner>(k))) {
				CHECK_EQUAL(m.cv, m.gate);
			}
		}
		CHECK_EQUAL(o.owned().cv, cvSeen);
		CHECK_EQUAL(o.owned().gate, gateSeen);
	}
}

TEST(OutputOwnershipTests, outOfRangeQueriesAreSafe) {
	OutputOwnership o;
	CHECK(o.cvOwner(-1) == Owner::NONE);
	CHECK(o.cvOwner(2) == Owner::NONE);
	CHECK(o.gateOwner(4) == Owner::NONE);
	Change change = o.claim(Owner::NONE, 0b11, 0b1111);
	CHECK(!change.any());
	CHECK(!o.owned().any());
}
