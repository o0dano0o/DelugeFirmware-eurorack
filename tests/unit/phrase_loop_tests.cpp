#include "CppUTest/TestHarness.h"
#include "processing/engines/phrase_loop.h"

#include <vector>

using namespace deluge::voice;

namespace {
struct Out {
	bool gate;
	int note;
	uint32_t at;
};
} // namespace

TEST_GROUP(PhraseLoopTests){};

static PhraseLoop makePhrase() {
	PhraseLoop p;
	p.beginRecording(100);
	p.record(100, true, 60);  // t 0
	p.record(103, false, 60); // t 3
	p.record(104, true, 64);  // t 4
	p.record(106, true, 67);  // t 6 note change
	p.record(108, false, 67); // t 8
	CHECK_TRUE(p.endRecording(110));
	return p;
}

TEST(PhraseLoopTests, recordingKeepsEventsAndLength) {
	PhraseLoop p = makePhrase();
	CHECK_EQUAL(10, p.length());
	CHECK_EQUAL(5, p.numEvents());
	CHECK_EQUAL(4, p.event(2).time);
	CHECK_EQUAL(64, p.event(2).note);
}

TEST(PhraseLoopTests, emptyRecordingMakesNoPhrase) {
	PhraseLoop p;
	p.beginRecording(0);
	CHECK_FALSE(p.endRecording(50));
	CHECK_FALSE(p.hasPhrase());
}

TEST(PhraseLoopTests, closeAndReopenInOneSlotIsLegato) {
	PhraseLoop p;
	p.beginRecording(0);
	p.record(0, true, 60);
	p.record(4, false, 60);
	p.record(4, true, 62);
	p.endRecording(8);
	CHECK_EQUAL(2, p.numEvents());
	CHECK_EQUAL(62, p.event(1).note);
	CHECK_TRUE(p.event(1).gate);
}

TEST(PhraseLoopTests, loopsAndReplaysEventsInOrder) {
	PhraseLoop p = makePhrase();
	p.start(1000, false);
	std::vector<Out> out;
	for (uint32_t now = 1000; now < 1025; now++) {
		p.advance(now, [&](bool g, uint8_t n) { out.push_back({g, n, now}); });
	}
	// 2 full passes (5 events each) plus the first events of the third pass (t 20 to 24: events at 0, 3 and 4)
	CHECK_EQUAL(13, (int)out.size());
	CHECK_EQUAL(1000, out[0].at);
	CHECK_TRUE(out[0].gate);
	CHECK_EQUAL(60, out[0].note);
	CHECK_EQUAL(1003, out[1].at);
	CHECK_FALSE(out[1].gate);
	CHECK_EQUAL(1010, out[5].at); // second pass starts
	CHECK_EQUAL(60, out[5].note);
	CHECK_EQUAL(1020, out[10].at);
}

TEST(PhraseLoopTests, advanceInBigStepsMissesNothing) {
	PhraseLoop p = makePhrase();
	p.start(0, false);
	int count = 0;
	p.advance(0, [&](bool, uint8_t) { count++; });
	p.advance(25, [&](bool, uint8_t) { count++; });
	CHECK_EQUAL(13, count);
}

TEST(PhraseLoopTests, oneShotPlaysOnceThenStopsWithGateOff) {
	PhraseLoop p;
	p.beginRecording(0);
	p.record(0, true, 60);
	p.endRecording(10); // gate still open at the end
	p.start(500, true);
	std::vector<Out> out;
	for (uint32_t now = 500; now < 530; now++) {
		p.advance(now, [&](bool g, uint8_t n) { out.push_back({g, n, now}); });
	}
	CHECK_FALSE(p.isPlaying());
	CHECK_EQUAL(2, (int)out.size());
	CHECK_TRUE(out[0].gate);
	CHECK_FALSE(out[1].gate);
	CHECK_EQUAL(510, out[1].at);
}

TEST(PhraseLoopTests, restartBeginsFromTheTop) {
	PhraseLoop p = makePhrase();
	p.start(0, false);
	p.advance(5, [](bool, uint8_t) {});
	p.start(5, false);
	std::vector<Out> out;
	p.advance(5, [&](bool g, uint8_t n) { out.push_back({g, n, 5}); });
	CHECK_EQUAL(1, (int)out.size());
	CHECK_EQUAL(60, out[0].note);
}

TEST(PhraseLoopTests, noteAtShowsTheSoundingNote) {
	PhraseLoop p = makePhrase();
	CHECK_EQUAL(60, p.noteAt(0));
	CHECK_EQUAL(60, p.noteAt(2));
	CHECK_EQUAL(-1, p.noteAt(3));
	CHECK_EQUAL(64, p.noteAt(5));
	CHECK_EQUAL(67, p.noteAt(7));
	CHECK_EQUAL(-1, p.noteAt(9));
}

TEST(PhraseLoopTests, positionWrapsInALoop) {
	PhraseLoop p = makePhrase();
	p.start(100, false);
	CHECK_EQUAL(0, p.position(100));
	CHECK_EQUAL(4, p.position(114));
}

TEST(PhraseLoopTests, noteHeldToTheEndIsClosedWhenThePhraseStartsWithSilence) {
	PhraseLoop p;
	p.beginRecording(0);
	p.record(4, true, 60);
	p.endRecording(10);
	CHECK_EQUAL(2, p.numEvents());
	CHECK_FALSE(p.event(1).gate);
	CHECK_EQUAL(9, p.event(1).time);
}

TEST(PhraseLoopTests, noteHeldToTheEndStaysOpenWhenThePhraseStartsOnANote) {
	PhraseLoop p;
	p.beginRecording(0);
	p.record(0, true, 60);
	p.endRecording(10);
	CHECK_EQUAL(1, p.numEvents());
}

TEST(PhraseLoopTests, loadRestoresAPhrase) {
	PhraseLoop p;
	PhraseEvent ev[2] = {{0, 62, true}, {5, 62, false}};
	CHECK_TRUE(p.load(ev, 2, 8));
	CHECK_EQUAL(8, p.length());
	CHECK_EQUAL(62, p.noteAt(2));
	CHECK_FALSE(p.load(ev, 0, 8));
	CHECK_FALSE(p.hasPhrase());
}
