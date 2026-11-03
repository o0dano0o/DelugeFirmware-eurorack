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

#include "processing/engines/phrase_loop.h"

namespace deluge::voice {

void PhraseLoop::clear() {
	numEvents_ = 0;
	length_ = 0;
	recording_ = false;
	playing_ = false;
	gateOpen_ = false;
	lastElapsed_ = -1;
}

bool PhraseLoop::load(const PhraseEvent* events, int32_t count, uint32_t length) {
	clear();
	if (count <= 0 || length == 0) {
		return false;
	}
	numEvents_ = count > kMaxEvents ? kMaxEvents : count;
	for (int32_t i = 0; i < numEvents_; i++) {
		events_[i] = events[i];
	}
	length_ = length;
	return true;
}

void PhraseLoop::beginRecording(uint32_t now) {
	clear();
	recording_ = true;
	recordStart_ = now;
}

void PhraseLoop::record(uint32_t now, bool gate, uint8_t note) {
	if (!recording_ || numEvents_ >= kMaxEvents) {
		return;
	}
	uint32_t t = now - recordStart_;
	// a gate that closes and reopens within the same time slot is one legato note
	if (gate && numEvents_ > 0 && !events_[numEvents_ - 1].gate && events_[numEvents_ - 1].time == t) {
		numEvents_--;
	}
	// ignore changes that don't change anything
	if (numEvents_ > 0 && events_[numEvents_ - 1].gate == gate && (!gate || events_[numEvents_ - 1].note == note)) {
		return;
	}
	if (numEvents_ == 0 && !gate) {
		return; // nothing to close
	}
	events_[numEvents_] = PhraseEvent{t, note, gate};
	numEvents_++;
}

bool PhraseLoop::endRecording(uint32_t now, uint32_t minLength) {
	if (!recording_) {
		return false;
	}
	recording_ = false;
	uint32_t len = now - recordStart_;
	if (len < minLength) {
		len = minLength;
	}
	if (numEvents_ == 0) {
		length_ = 0;
		return false;
	}
	// a gate still open at the end closes when the loop wraps; events that fall at or after the end are dropped
	while (numEvents_ > 0 && events_[numEvents_ - 1].time >= len) {
		numEvents_--;
	}
	if (numEvents_ == 0) {
		length_ = 0;
		return false;
	}
	length_ = len;
	// a note still sounding at the end carries over the wrap, which is only right if the phrase starts on a note too
	if (events_[numEvents_ - 1].gate && events_[0].time > 0 && numEvents_ < kMaxEvents
	    && len > events_[numEvents_ - 1].time + 1) {
		events_[numEvents_] = PhraseEvent{len - 1, events_[numEvents_ - 1].note, false};
		numEvents_++;
	}
	return true;
}

void PhraseLoop::start(uint32_t now, bool oneShot) {
	if (!hasPhrase()) {
		return;
	}
	playing_ = true;
	oneShot_ = oneShot;
	startTime_ = now;
	lastElapsed_ = -1;
	gateOpen_ = false;
}

void PhraseLoop::stop() {
	playing_ = false;
}

uint32_t PhraseLoop::position(uint32_t now) const {
	if (length_ == 0) {
		return 0;
	}
	uint32_t elapsed = now - startTime_;
	if (oneShot_ && elapsed >= length_) {
		return length_;
	}
	return elapsed % length_;
}

int32_t PhraseLoop::noteAt(uint32_t position) const {
	int32_t result = -1;
	for (int32_t i = 0; i < numEvents_; i++) {
		if (events_[i].time > position) {
			break;
		}
		result = events_[i].gate ? events_[i].note : -1;
	}
	return result;
}

} // namespace deluge::voice
