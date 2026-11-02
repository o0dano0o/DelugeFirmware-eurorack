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
constexpr float kYinThreshold = 0.15f;  ///< first dip of the normalised difference below this is taken as the period
constexpr float kVoicedLimit = 0.30f;   ///< above this, nothing periodic enough to call a pitch
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
	latest_ = result;
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
