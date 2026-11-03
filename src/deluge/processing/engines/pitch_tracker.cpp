/*
 * Copyright © 2026 The Deluge Firmware contributors
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "processing/engines/pitch_tracker.h"

#include <cmath>

namespace deluge::voice {

namespace {
constexpr float kYinThreshold = 0.20f;  ///< first dip of the normalised difference below this is taken as the period
constexpr float kVoicedLimit = 0.45f;   ///< above this, nothing periodic enough to call a pitch
} // namespace

void PitchTracker::reset() {
	for (float& v : stage1_) {
		v = 0.0f;
	}
	for (float& v : stage2_) {
		v = 0.0f;
	}
	stage1Phase_ = stage2Phase_ = false;
	for (float& v : buffer_) {
		v = 0.0f;
	}
	written_ = 0;
	sinceAnalysis_ = 0;
	rawWritten_ = 0;
	for (float& v : raw_) {
		v = 0.0f;
	}
	latest_ = PitchResult{};
}

void PitchTracker::pushDecimated(float sample) {
	buffer_[written_ & (kBufferSize - 1)] = sample;
	written_++;
	sinceAnalysis_++;
}

int32_t PitchTracker::feed(const float* samples, int32_t count) {
	int32_t produced = 0;
	for (int32_t i = 0; i < count; i++) {
		float x = samples[i];
		raw_[rawWritten_ & (kRawSize - 1)] = x;
		rawWritten_++;

		// stage 1: [1 2 1] / 4 smoothing, keep every second output
		float y1 = 0.25f * (stage1_[0] + 2.0f * stage1_[1] + x);
		stage1_[0] = stage1_[1];
		stage1_[1] = x;
		stage1Phase_ = !stage1Phase_;
		if (!stage1Phase_) {
			continue;
		}

		// stage 2: the same again on the halved stream
		float y2 = 0.25f * (stage2_[0] + 2.0f * stage2_[1] + y1);
		stage2_[0] = stage2_[1];
		stage2_[1] = y1;
		stage2Phase_ = !stage2Phase_;
		if (!stage2Phase_) {
			continue;
		}

		pushDecimated(y2);
		if (sinceAnalysis_ >= kHop && written_ >= static_cast<uint32_t>(kWindow + kMaxTau)) {
			sinceAnalysis_ = 0;
			analyse();
			produced++;
		}
	}
	return produced;
}

void PitchTracker::analyse() {
	constexpr int32_t frameLength = kWindow + kMaxTau;
	uint32_t start = written_ - frameLength;
	for (int32_t i = 0; i < frameLength; i++) {
		frame_[i] = buffer_[(start + i) & (kBufferSize - 1)];
	}

	// level: RMS of the window
	float energy = 0.0f;
	for (int32_t j = 0; j < kWindow; j++) {
		energy += frame_[j] * frame_[j];
	}
	float level = std::sqrt(energy / kWindow);

	PitchResult result;
	result.level = level;

	if (level < levelThreshold_) {
		latest_ = result;
		return;
	}

	// difference function d(tau), then the cumulative-mean-normalised version d'(tau)
	difference_[0] = 0.0f;
	for (int32_t tau = 1; tau <= kMaxTau; tau++) {
		float sum = 0.0f;
		const float* a = frame_;
		const float* b = frame_ + tau;
		for (int32_t j = 0; j < kWindow; j++) {
			float d = a[j] - b[j];
			sum += d * d;
		}
		difference_[tau] = sum;
	}
	normalised_[0] = 1.0f;
	float running = 0.0f;
	for (int32_t tau = 1; tau <= kMaxTau; tau++) {
		running += difference_[tau];
		normalised_[tau] = (running > 1.0e-12f) ? difference_[tau] * tau / running : 1.0f;
	}

	// first dip below the threshold (then follow it down to its bottom), else the lowest point of all
	int32_t best = -1;
	for (int32_t tau = kMinTau; tau <= kMaxTau; tau++) {
		if (normalised_[tau] < kYinThreshold) {
			while (tau + 1 <= kMaxTau && normalised_[tau + 1] < normalised_[tau]) {
				tau++;
			}
			best = tau;
			break;
		}
	}
	if (best < 0) {
		float lowest = 2.0f;
		for (int32_t tau = kMinTau; tau <= kMaxTau; tau++) {
			if (normalised_[tau] < lowest) {
				lowest = normalised_[tau];
				best = tau;
			}
		}
	}
	if (best < 0 || normalised_[best] > kVoicedLimit) {
		result.confidence = best < 0 ? 0.0f : 1.0f - normalised_[best];
		// nothing in the main range: it may be a whistle
		analyseHigh(result);
		latest_ = result;
		return;
	}

	// parabolic interpolation around the minimum
	float tauEstimate = static_cast<float>(best);
	if (best > kMinTau && best < kMaxTau) {
		float s0 = normalised_[best - 1];
		float s1 = normalised_[best];
		float s2 = normalised_[best + 1];
		float denominator = s0 - 2.0f * s1 + s2;
		if (denominator > 1.0e-9f) {
			tauEstimate += 0.5f * (s0 - s2) / denominator;
		}
	}

	result.frequency = kAnalysisRate / tauEstimate;
	result.confidence = 1.0f - normalised_[best];
	result.voiced = true;
	// a whistle can be mistaken for a lower pitch: a 2.6 kHz tone repeats every 4.2 samples here, but so does it every
	// 17, which is a "pitch" of 650 Hz. So also look at the full rate: if that finds a clean pitch which is a whole
	// number of times the main estimate (or the main estimate is already up in the high range), take it.
	{
		PitchResult high = result;
		if (analyseHigh(high)) {
			float ratio = high.frequency / result.frequency;
			float nearest = std::floor(ratio + 0.5f);
			bool subharmonic = nearest >= 2.0f && nearest <= 6.0f && std::fabs(ratio - nearest) < 0.03f * nearest;
			if (result.frequency > 700.0f || subharmonic) {
				result = high;
			}
		}
	}
	latest_ = result;
}

bool PitchTracker::analyseHigh(PitchResult& result) {
	constexpr int32_t frameLength = kHighWindow + kHighMaxTau;
	uint32_t start = rawWritten_ - frameLength;
	for (int32_t i = 0; i < frameLength; i++) {
		highFrame_[i] = raw_[(start + i) & (kRawSize - 1)];
	}

	float running = 0.0f;
	highNormalised_[0] = 1.0f;
	for (int32_t tau = 1; tau <= kHighMaxTau; tau++) {
		float sum = 0.0f;
		for (int32_t j = 0; j < kHighWindow; j++) {
			float d = highFrame_[j] - highFrame_[j + tau];
			sum += d * d;
		}
		running += sum;
		highNormalised_[tau] = (running > 1.0e-12f) ? sum * tau / running : 1.0f;
	}

	int32_t best = -1;
	for (int32_t tau = kHighMinTau; tau <= kHighMaxTau; tau++) {
		if (highNormalised_[tau] < kYinThreshold) {
			while (tau + 1 <= kHighMaxTau && highNormalised_[tau + 1] < highNormalised_[tau]) {
				tau++;
			}
			best = tau;
			break;
		}
	}
	// no fallback to the global minimum here: a whistle is clean, and anything else should stay unvoiced
	if (best < 0 || highNormalised_[best] > kVoicedLimit) {
		return false;
	}
	float tauEstimate = static_cast<float>(best);
	if (best > kHighMinTau && best < kHighMaxTau) {
		float s0 = highNormalised_[best - 1];
		float s1 = highNormalised_[best];
		float s2 = highNormalised_[best + 1];
		float denominator = s0 - 2.0f * s1 + s2;
		if (denominator > 1.0e-9f) {
			tauEstimate += 0.5f * (s0 - s2) / denominator;
		}
	}
	float frequency = kInputSampleRate / tauEstimate;
	if (frequency < 700.0f) {
		return false;
	}
	result.frequency = frequency;
	result.confidence = 1.0f - highNormalised_[best];
	result.voiced = true;
	return true;
}

float frequencyToNote(float frequency) {
	if (frequency <= 1.0f) {
		return 0.0f;
	}
	return 69.0f + 12.0f * std::log2(frequency / 440.0f);
}

void NoteFollower::reset() {
	gate_ = false;
	note_ = 60;
	pitch_ = 60.0f;
	voicedRun_ = unvoicedRun_ = 0;
	candidate_ = 0;
	candidateRun_ = 0;
}

bool NoteFollower::update(const PitchResult& result) {
	if (!result.voiced) {
		voicedRun_ = 0;
		candidateRun_ = 0;
		unvoicedRun_++;
		if (gate_ && unvoicedRun_ >= releaseFrames_) {
			gate_ = false;
		}
		return false;
	}

	unvoicedRun_ = 0;
	voicedRun_++;
	float midi = frequencyToNote(result.frequency);
	int32_t nearest = static_cast<int32_t>(std::floor(midi + 0.5f));

	// the continuous pitch follows quickly while a note is sounding, and jumps to a fresh start on a new note
	if (!gate_ || voicedRun_ == 1) {
		pitch_ = midi;
	}
	else {
		pitch_ += 0.5f * (midi - pitch_);
	}

	bool newNote = false;
	if (!gate_) {
		// opening: the same note for attackFrames_ estimates in a row
		if (nearest == candidate_) {
			candidateRun_++;
		}
		else {
			candidate_ = nearest;
			candidateRun_ = 1;
		}
		if (candidateRun_ >= attackFrames_) {
			gate_ = true;
			note_ = candidate_;
			pitch_ = midi;
			newNote = true;
		}
		return newNote;
	}

	// sounding: stay on the note while the pitch is within 0.65 of a semitone of it, so vibrato doesn't re-trigger it
	if (std::fabs(midi - note_) <= 0.65f) {
		candidateRun_ = 0;
		return false;
	}
	if (nearest == candidate_) {
		candidateRun_++;
	}
	else {
		candidate_ = nearest;
		candidateRun_ = 1;
	}
	if (candidateRun_ >= attackFrames_) {
		note_ = candidate_;
		candidateRun_ = 0;
		newNote = true;
	}
	return newNote;
}

} // namespace deluge::voice
