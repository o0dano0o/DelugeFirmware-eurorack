#include "CppUTest/TestHarness.h"
#include "processing/engines/pitch_tracker.h"

#include <cmath>
#include <vector>

using namespace deluge::voice;

namespace {
constexpr float kPi = 3.14159265358979f;

/// Harmonic tone with a given set of harmonic amplitudes (a rough voice)
std::vector<float> tone(float frequency, float seconds, float amplitude, const std::vector<float>& harmonics = {1.0f},
                        float startPhase = 0.0f) {
	int n = static_cast<int>(seconds * kInputSampleRate);
	std::vector<float> out(n);
	for (int i = 0; i < n; i++) {
		float t = i / kInputSampleRate;
		float v = 0.0f;
		for (size_t h = 0; h < harmonics.size(); h++) {
			v += harmonics[h] * std::sin(2.0f * kPi * frequency * (h + 1) * t + startPhase);
		}
		out[i] = amplitude * v;
	}
	return out;
}

/// Feeds in blocks of an awkward size, collecting every estimate
std::vector<PitchResult> runTracker(PitchTracker& tracker, const std::vector<float>& audio) {
	std::vector<PitchResult> results;
	const int block = 37;
	for (size_t i = 0; i < audio.size(); i += block) {
		int n = static_cast<int>(std::min<size_t>(block, audio.size() - i));
		int produced = tracker.feed(audio.data() + i, n);
		for (int k = 0; k < produced; k++) {
			results.push_back(tracker.latest());
		}
	}
	return results;
}

float noteOf(float frequency) {
	return frequencyToNote(frequency);
}
} // namespace

TEST_GROUP(PitchTrackerTests){};

TEST(PitchTrackerTests, sineWavesAcrossTheVocalRange) {
	for (float f : {110.0f, 146.8f, 220.0f, 329.6f, 440.0f, 622.3f, 880.0f}) {
		PitchTracker t;
		auto results = runTracker(t, tone(f, 0.6f, 0.3f));
		CHECK_TRUE(results.size() > 20);
		// skip the first few while the buffer fills
		for (size_t i = 5; i < results.size(); i++) {
			CHECK_TRUE(results[i].voiced);
			DOUBLES_EQUAL(f, results[i].frequency, f * 0.01);
		}
	}
}

TEST(PitchTrackerTests, voiceLikeTonesDoNotJumpAnOctave) {
	// strong second and third harmonics, weaker fundamental, as in many voices
	std::vector<float> harmonics = {0.5f, 1.0f, 0.8f, 0.4f, 0.2f};
	for (float f : {110.0f, 196.0f, 261.6f, 392.0f}) {
		PitchTracker t;
		auto results = runTracker(t, tone(f, 0.6f, 0.2f, harmonics, 0.7f));
		for (size_t i = 5; i < results.size(); i++) {
			CHECK_TRUE(results[i].voiced);
			DOUBLES_EQUAL(noteOf(f), noteOf(results[i].frequency), 0.3);
		}
	}
}

TEST(PitchTrackerTests, silenceAndQuietSoundAreNotVoiced) {
	PitchTracker t;
	t.setLevelThreshold(0.02f);
	auto silence = runTracker(t, std::vector<float>(20000, 0.0f));
	for (auto& r : silence) {
		CHECK_FALSE(r.voiced);
	}
	t.reset();
	auto quiet = runTracker(t, tone(220.0f, 0.5f, 0.005f)); // RMS about 0.0035
	for (auto& r : quiet) {
		CHECK_FALSE(r.voiced);
	}
}

TEST(PitchTrackerTests, noiseIsMostlyNotVoiced) {
	PitchTracker t;
	std::vector<float> noise(44100);
	uint32_t x = 1234567;
	for (auto& v : noise) {
		x = x * 1664525u + 1013904223u;
		v = (static_cast<float>(x >> 8) / 8388608.0f - 1.0f) * 0.3f;
	}
	auto results = runTracker(t, noise);
	int voiced = 0;
	for (auto& r : results) {
		voiced += r.voiced ? 1 : 0;
	}
	CHECK_TRUE(voiced < static_cast<int>(results.size()) / 10);
}

TEST(PitchTrackerTests, followsANoteChange) {
	PitchTracker t;
	std::vector<float> audio = tone(220.0f, 0.4f, 0.3f);
	auto second = tone(330.0f, 0.4f, 0.3f);
	audio.insert(audio.end(), second.begin(), second.end());
	auto results = runTracker(t, audio);
	CHECK_TRUE(results.size() > 50);
	DOUBLES_EQUAL(220.0, results[10].frequency, 3.0);
	DOUBLES_EQUAL(330.0, results.back().frequency, 4.0);
}

TEST(PitchTrackerTests, inputBlockSizeDoesNotMatter) {
	auto audio = tone(261.6f, 0.4f, 0.3f);
	PitchTracker a, b;
	auto ra = runTracker(a, audio);
	std::vector<PitchResult> rb;
	int produced = b.feed(audio.data(), static_cast<int>(audio.size()));
	CHECK_TRUE(produced > 0);
	CHECK_EQUAL(ra.size(), static_cast<size_t>(produced));
	DOUBLES_EQUAL(ra.back().frequency, b.latest().frequency, 0.01);
}

TEST_GROUP(NoteFollowerTests){};

namespace {
PitchResult voicedAt(float midi) {
	PitchResult r;
	r.voiced = true;
	r.confidence = 0.95f;
	r.level = 0.1f;
	r.frequency = 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
	return r;
}
PitchResult unvoiced() {
	return PitchResult{};
}
} // namespace

TEST(NoteFollowerTests, gateOpensAfterAttackFramesOnTheRightNote) {
	NoteFollower f;
	CHECK_FALSE(f.update(voicedAt(60.1f)));
	CHECK_FALSE(f.update(voicedAt(59.9f)));
	CHECK_FALSE(f.gate());
	CHECK_TRUE(f.update(voicedAt(60.0f)));
	CHECK_TRUE(f.gate());
	CHECK_EQUAL(60, f.note());
}

TEST(NoteFollowerTests, vibratoDoesNotRetrigger) {
	NoteFollower f;
	for (int i = 0; i < 3; i++) {
		f.update(voicedAt(64.0f));
	}
	int newNotes = 0;
	for (int i = 0; i < 200; i++) {
		float midi = 64.0f + 0.5f * std::sin(i * 0.4f); // about +-50 cents
		newNotes += f.update(voicedAt(midi)) ? 1 : 0;
	}
	CHECK_EQUAL(0, newNotes);
	CHECK_EQUAL(64, f.note());
	CHECK_TRUE(f.gate());
}

TEST(NoteFollowerTests, singleGlitchDoesNotChangeNote) {
	NoteFollower f;
	for (int i = 0; i < 5; i++) {
		f.update(voicedAt(60.0f));
	}
	CHECK_FALSE(f.update(voicedAt(72.0f))); // octave glitch for one frame
	for (int i = 0; i < 5; i++) {
		CHECK_FALSE(f.update(voicedAt(60.0f)));
	}
	CHECK_EQUAL(60, f.note());
}

TEST(NoteFollowerTests, realChangeIsFollowedAfterAttackFrames) {
	NoteFollower f;
	for (int i = 0; i < 5; i++) {
		f.update(voicedAt(60.0f));
	}
	CHECK_FALSE(f.update(voicedAt(65.0f)));
	CHECK_FALSE(f.update(voicedAt(65.0f)));
	CHECK_TRUE(f.update(voicedAt(65.0f)));
	CHECK_EQUAL(65, f.note());
	CHECK_TRUE(f.gate());
}

TEST(NoteFollowerTests, shortDropoutKeepsGateButLongOneCloses) {
	NoteFollower f;
	for (int i = 0; i < 5; i++) {
		f.update(voicedAt(60.0f));
	}
	for (int i = 0; i < 3; i++) {
		f.update(unvoiced());
	}
	CHECK_TRUE(f.gate());
	f.update(voicedAt(60.0f));
	for (int i = 0; i < 4; i++) {
		f.update(unvoiced());
	}
	CHECK_TRUE(f.gate());
	f.update(unvoiced());
	CHECK_FALSE(f.gate());
	CHECK_EQUAL(60, f.note()); // note is held after the gate closes
}

TEST(NoteFollowerTests, endToEndWithRealAudio) {
	PitchTracker t;
	NoteFollower f;
	std::vector<float> audio = tone(220.0f, 0.5f, 0.3f);
	auto b = tone(293.7f, 0.5f, 0.3f);
	audio.insert(audio.end(), b.begin(), b.end());
	audio.insert(audio.end(), 8000, 0.0f);
	std::vector<int> changes;
	const int block = 64;
	for (size_t i = 0; i < audio.size(); i += block) {
		int n = static_cast<int>(std::min<size_t>(block, audio.size() - i));
		int produced = t.feed(audio.data() + i, n);
		for (int k = 0; k < produced; k++) {
			if (f.update(t.latest())) {
				changes.push_back(f.note());
			}
		}
	}
	CHECK_EQUAL(2, static_cast<int>(changes.size()));
	CHECK_EQUAL(57, changes[0]); // A3
	CHECK_EQUAL(62, changes[1]); // D4
	CHECK_FALSE(f.gate());
}
