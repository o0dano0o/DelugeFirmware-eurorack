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

namespace deluge::voice {

/// One recorded change: the gate opening on a note, the note changing while the gate is open, or the gate closing
struct PhraseEvent {
	uint32_t time = 0; ///< in the loop's own time unit, from the start of the phrase
	uint8_t note = 0;  ///< MIDI note number
	bool gate = false; ///< true: the gate is high from here (with this note), false: the gate goes low
};

/// A recorded melodic phrase that can be replayed in a loop or once. It knows nothing about time units: the caller
/// passes "now" in whatever unit it likes (audio ticks for free timing, sixteenth-note steps for song-clock timing).
class PhraseLoop {
public:
	static constexpr int32_t kMaxEvents = 128;

	void clear();
	bool hasPhrase() const { return length_ > 0; }
	bool isRecording() const { return recording_; }
	uint32_t length() const { return length_; }
	int32_t numEvents() const { return numEvents_; }
	const PhraseEvent& event(int32_t i) const { return events_[i]; }

	/// Replaces the phrase with stored events (from a saved song). Returns false if they don't make a phrase.
	bool load(const PhraseEvent* events, int32_t count, uint32_t length);

	/// Starts a new recording (discarding any phrase). `now` is the time of the start.
	void beginRecording(uint32_t now);
	/// Records the gate and note as they are at `now`. Call it when the gate opens, closes or the note changes.
	void record(uint32_t now, bool gate, uint8_t note);
	/// Ends the recording at `now`. The phrase is `now - start` long (at least `minLength`). Returns false if nothing was
	/// played, in which case there is no phrase.
	bool endRecording(uint32_t now, uint32_t minLength = 1);

	/// Starts playback so that the phrase's time zero is `now`. A one-shot phrase plays once and stops.
	void start(uint32_t now, bool oneShot);
	void stop();
	bool isPlaying() const { return playing_; }

	/// Where playback is within the phrase (0 to length), for displays
	uint32_t position(uint32_t now) const;
	/// The note sounding at a position in the phrase, or -1 if the gate is low there
	int32_t noteAt(uint32_t position) const;

	/// Plays the events between the last call and `now`, calling emit(gate, note) for each. A one-shot that has run to
	/// its end stops and emits a final gate-off if the gate was open. Returns true while still playing.
	template <typename F>
	bool advance(uint32_t now, F&& emit) {
		if (!playing_) {
			return false;
		}
		int64_t elapsed = static_cast<int64_t>(static_cast<uint32_t>(now - startTime_));
		int64_t len = length_;
		int64_t first = lastElapsed_ + 1; // process (lastElapsed_, elapsed]
		lastElapsed_ = elapsed;
		for (int64_t iteration = (first < 0 ? 0 : first) / len; iteration * len <= elapsed; iteration++) {
			if (oneShot_ && iteration > 0) {
				break;
			}
			for (int32_t i = 0; i < numEvents_; i++) {
				int64_t t = iteration * len + events_[i].time;
				if (t >= first && t <= elapsed) {
					emit(events_[i].gate, events_[i].note);
					gateOpen_ = events_[i].gate;
				}
			}
		}
		if (oneShot_ && elapsed >= len) {
			playing_ = false;
			if (gateOpen_) {
				emit(false, 0);
				gateOpen_ = false;
			}
			return false;
		}
		return true;
	}

private:
	PhraseEvent events_[kMaxEvents];
	int32_t numEvents_ = 0;
	uint32_t length_ = 0;
	bool recording_ = false;
	uint32_t recordStart_ = 0;
	bool playing_ = false;
	bool oneShot_ = false;
	uint32_t startTime_ = 0;
	int64_t lastElapsed_ = -1;
	bool gateOpen_ = false;
};

} // namespace deluge::voice
