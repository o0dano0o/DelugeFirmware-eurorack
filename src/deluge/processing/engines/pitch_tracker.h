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

#pragma once

#include <cstdint>

/// Live pitch tracking for a single voice (singing or humming), in two pure-computation pieces:
///   PitchTracker  audio in, a pitch estimate every ~12 ms out
///   NoteFollower  turns those estimates into a steady note number and a gate, ignoring vibrato and glitches
namespace deluge::voice {

constexpr float kInputSampleRate = 44100.0f;
constexpr int32_t kDecimation = 4;
constexpr float kAnalysisRate = kInputSampleRate / kDecimation; ///< 11025 Hz
constexpr int32_t kMinTau = 11;                                  ///< highest pitch about 1000 Hz
constexpr int32_t kMaxTau = 150;                                 ///< lowest pitch about 73 Hz
constexpr int32_t kWindow = 384;                                 ///< samples the difference function is summed over
constexpr int32_t kHop = 128;                                    ///< analysis every 128 analysis samples, ~11.6 ms

/// One pitch estimate
struct PitchResult {
	float frequency = 0.0f;  ///< Hz, only meaningful when voiced
	float confidence = 0.0f; ///< 0 to 1, how periodic the sound is
	float level = 0.0f;      ///< RMS amplitude, 0 to 1
	bool voiced = false;     ///< a clear pitch and loud enough
};

/// YIN pitch detector (de Cheveigné and Kawahara, 2002) on audio decimated to 11 kHz.
///
/// Feed it mono audio at 44.1 kHz in any block size. Every 128 decimated samples it produces a new estimate. The cost
/// is about 55 thousand multiply-adds per estimate, roughly 5 million per second.
class PitchTracker {
public:
	/// Quietest RMS level (0 to 1) that counts as singing
	void setLevelThreshold(float level) { levelThreshold_ = level; }
	float getLevelThreshold() const { return levelThreshold_; }

	void reset();

	/// Adds audio (-1 to 1). Returns how many new estimates this produced; read them with latest().
	int32_t feed(const float* samples, int32_t count);

	const PitchResult& latest() const { return latest_; }

private:
	void pushDecimated(float sample);
	void analyse();

	// two cascaded [1 2 1] / 4 low-pass-and-halve stages
	float stage1_[2] = {0, 0};
	float stage2_[2] = {0, 0};
	bool stage1Phase_ = false;
	bool stage2Phase_ = false;

	static constexpr int32_t kBufferSize = 1024; ///< power of two, holds more than kWindow + kMaxTau
	float buffer_[kBufferSize] = {};
	uint32_t written_ = 0;
	int32_t sinceAnalysis_ = 0;

	float frame_[kWindow + kMaxTau + 1] = {};
	float difference_[kMaxTau + 1] = {};
	float normalised_[kMaxTau + 1] = {};

	float levelThreshold_ = 0.01f;
	PitchResult latest_;
};

/// Converts frequency to a (fractional) MIDI note number, A4 = 440 Hz = 69
float frequencyToNote(float frequency);

/// Steadies the pitch estimates into notes. A new note needs a few consistent estimates in a row, small wobbles inside a
/// note (vibrato) are ignored, and short dropouts don't close the gate.
class NoteFollower {
public:
	/// Estimates in a row needed to open the gate or change note (each is ~11.6 ms)
	void setAttackFrames(int32_t frames) { attackFrames_ = frames < 1 ? 1 : frames; }
	/// Unvoiced estimates in a row before the gate closes
	void setReleaseFrames(int32_t frames) { releaseFrames_ = frames < 1 ? 1 : frames; }

	void reset();
	/// Feed each new estimate. Returns true if the held note changed or the gate opened (a new note started).
	bool update(const PitchResult& result);

	bool gate() const { return gate_; }
	/// The steady note (MIDI note number); stays at the last note while the gate is closed
	int32_t note() const { return note_; }
	/// Smoothed continuous pitch (fractional MIDI note) for output without quantising
	float pitch() const { return pitch_; }

private:
	int32_t attackFrames_ = 2;
	int32_t releaseFrames_ = 8;
	bool gate_ = false;
	int32_t note_ = 60;
	float pitch_ = 60.0f;
	int32_t voicedRun_ = 0;
	int32_t unvoicedRun_ = 0;
	int32_t candidate_ = 0;
	int32_t candidateRun_ = 0;
};

} // namespace deluge::voice
