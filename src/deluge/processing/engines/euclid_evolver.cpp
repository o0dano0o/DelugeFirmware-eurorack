/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "processing/engines/euclid_evolver.h"
#include "definitions_cxx.hpp"
#include "model/clip/instrument_clip.h"
#include "model/drum/gate_drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/cv_engine.h"
#include "util/functions.h"
#include <algorithm>

namespace deluge::euclid::evolver {

namespace {

EvolveRule rules_[kNumTracks];
uint32_t lastRise_[kNumTracks] = {};
int32_t counted_[kNumTracks] = {};
int8_t sign_[kNumTracks] = {1, 1, 1, 1};
bool frozen_ = false;
uint32_t changeCount_ = 0;
uint32_t randomState_ = 0x2545F491;

struct Baseline {
	bool valid = false;
	int32_t hits = 0;
	int32_t rotation = 0;
};
Baseline baseline_[kNumTracks];

uint32_t nextRandom() {
	randomState_ ^= randomState_ << 13;
	randomState_ ^= randomState_ >> 17;
	randomState_ ^= randomState_ << 5;
	return randomState_;
}

/// The Kit clip the gate tracks live in: the song's current clip, if it is a Kit with gate rows
InstrumentClip* findClip() {
	if (currentSong == nullptr) {
		return nullptr;
	}
	Clip* clip = currentSong->getCurrentClip();
	if (clip == nullptr || clip->type != ClipType::INSTRUMENT || clip->output == nullptr
	    || clip->output->type != OutputType::KIT) {
		return nullptr;
	}
	return static_cast<InstrumentClip*>(clip);
}

NoteRow* findRow(InstrumentClip* clip, int32_t track, int32_t& index) {
	for (int32_t i = 0; i < clip->noteRows.getNumElements(); i++) {
		NoteRow* row = clip->noteRows.getElement(i);
		Drum* drum = row->drum;
		if (drum && drum->type == DrumType::GATE && static_cast<GateDrum*>(drum)->channel == track) {
			index = i;
			return row;
		}
	}
	return nullptr;
}

void sortByPos(NoteVector& notes) {
	for (int32_t i = 1; i < notes.getNumElements(); i++) {
		int32_t j = i;
		while (j > 0 && notes.getElement(j - 1)->pos > notes.getElement(j)->pos) {
			std::swap(*notes.getElement(j - 1), *notes.getElement(j));
			j--;
		}
	}
}

struct TrackRef {
	InstrumentClip* clip = nullptr;
	NoteRow* row = nullptr;
	int32_t index = 0;
	uint32_t stepWidth = 1;
	int32_t length = 0;
	int32_t span = 1;
};

bool lookUp(int32_t track, TrackRef& ref, ModelStackWithNoteRow*& stack, char* memory) {
	ref.clip = findClip();
	if (!ref.clip) {
		return false;
	}
	ref.row = findRow(ref.clip, track, ref.index);
	if (!ref.row) {
		return false;
	}
	ModelStackWithTimelineCounter* withClip = currentSong->setupModelStackWithCurrentClip(memory);
	stack = withClip->addNoteRow(ref.clip->getNoteRowId(ref.row, ref.index), ref.row);
	ref.stepWidth = currentSong->xZoom[NAVIGATION_CLIP];
	ref.length = stack->getLoopLength();
	int32_t numSteps = (uint32_t)(ref.length - 1) / ref.stepWidth + 1;
	int32_t padding = std::min<int32_t>(ref.row->euclidPadding, std::max<int32_t>(numSteps - 1, 0));
	ref.span = std::max<int32_t>(numSteps - padding, 1);
	return true;
}

/// Replaces a track's hits with `hits` spread evenly over its pattern, turned by `rotation`
void respace(int32_t track, int32_t hits, int32_t rotation) {
	TrackRef ref;
	ModelStackWithNoteRow* stack = nullptr;
	char memory[MODEL_STACK_MAX_SIZE];
	if (!lookUp(track, ref, stack, memory)) {
		return;
	}
	hits = std::clamp<int32_t>(hits, 0, ref.span);
	rotation = ((rotation % ref.span) + ref.span) % ref.span;

	NoteVector newNotes;
	if (hits && newNotes.insertAtIndex(0, hits) != Error::NONE) {
		return;
	}
	for (int32_t i = 0; i < hits; i++) {
		Note* note = newNotes.getElement(i);
		int32_t step = (int32_t)((uint32_t)(i * ref.span) / (uint32_t)hits);
		step = (step + rotation) % ref.span;
		note->pos = step * ref.stepWidth;
		note->length = ref.stepWidth;
		note->probability = ref.row->getDefaultProbability();
		note->velocity = static_cast<Instrument*>(ref.clip->output)->defaultVelocity;
		note->lift = kDefaultLiftValue;
		note->iterance = ref.row->getDefaultIterance();
		note->fill = ref.row->getDefaultFill(stack);
	}
	sortByPos(newNotes);
	for (int32_t i = 0; i < hits; i++) {
		Note* note = newNotes.getElement(i);
		note->length = std::min<int32_t>(note->length, ref.length - note->pos);
	}
	ref.row->notes.swapStateWith(&newNotes);
	ref.row->euclidRotation = rotation;
	ref.clip->expectEvent();
	changeCount_++;
}

/// Turns a track's pattern by `shift` steps, keeping any hits that were edited by hand
void turn(int32_t track, int32_t shift) {
	TrackRef ref;
	ModelStackWithNoteRow* stack = nullptr;
	char memory[MODEL_STACK_MAX_SIZE];
	if (!lookUp(track, ref, stack, memory)) {
		return;
	}
	shift = ((shift % ref.span) + ref.span) % ref.span;
	if (shift == 0) {
		return;
	}
	int32_t count = ref.row->notes.getNumElements();
	NoteVector newNotes;
	if (count && newNotes.insertAtIndex(0, count) != Error::NONE) {
		return;
	}
	for (int32_t i = 0; i < count; i++) {
		Note* dest = newNotes.getElement(i);
		*dest = *ref.row->notes.getElement(i);
		int32_t step = (int32_t)((uint32_t)dest->pos / ref.stepWidth);
		if (step < ref.span) {
			step = (step + shift) % ref.span;
		}
		dest->pos = step * ref.stepWidth;
	}
	sortByPos(newNotes);
	for (int32_t i = 0; i < count; i++) {
		Note* note = newNotes.getElement(i);
		note->length = std::min<int32_t>(note->length, ref.length - note->pos);
	}
	ref.row->notes.swapStateWith(&newNotes);
	ref.row->euclidRotation = (ref.row->euclidRotation + shift) % ref.span;
	ref.clip->expectEvent();
	changeCount_++;
}

void captureBaseline(int32_t track) {
	int32_t hits, rotation, span;
	if (readTrack(track, hits, rotation, span)) {
		baseline_[track] = {true, hits, rotation};
	}
}

void apply(int32_t track) {
	const EvolveRule& r = rules_[track];
	int32_t hits, rotation, span;
	if (!readTrack(track, hits, rotation, span)) {
		return;
	}
	if (r.target == EvolveRule::HITS) {
		int32_t next = evolveValue(r, hits, 1, span, sign_[track], nextRandom());
		if (next != hits) {
			respace(track, next, rotation);
		}
	}
	else {
		int32_t next = evolveValue(r, rotation, 0, span - 1, sign_[track], nextRandom());
		if (next != rotation) {
			turn(track, next - rotation);
		}
	}
}

} // namespace

const EvolveRule& rule(int32_t track) {
	return rules_[std::clamp<int32_t>(track, 0, kNumTracks - 1)];
}

void setRule(int32_t track, const EvolveRule& rule) {
	if (track < 0 || track >= kNumTracks) {
		return;
	}
	bool wasActive = rules_[track].active();
	rules_[track] = rule;
	counted_[track] = 0;
	sign_[track] = 1;
	if (rule.active()) {
		lastRise_[track] = cvEngine.gateRiseCount(rule.source);
		if (!wasActive || !baseline_[track].valid) {
			captureBaseline(track);
		}
	}
}

void reset() {
	for (int32_t t = 0; t < kNumTracks; t++) {
		rules_[t] = EvolveRule();
		counted_[t] = 0;
		sign_[t] = 1;
		baseline_[t] = Baseline();
	}
	frozen_ = false;
}

bool isFrozen() {
	return frozen_;
}

void setFrozen(bool frozen) {
	frozen_ = frozen;
}

void restoreBaseline(int32_t track) {
	if (track < 0 || track >= kNumTracks || !baseline_[track].valid) {
		return;
	}
	respace(track, baseline_[track].hits, baseline_[track].rotation);
	counted_[track] = 0;
	sign_[track] = 1;
}

uint32_t changeCount() {
	return changeCount_;
}

bool readTrack(int32_t track, int32_t& hits, int32_t& rotation, int32_t& span) {
	TrackRef ref;
	ModelStackWithNoteRow* stack = nullptr;
	char memory[MODEL_STACK_MAX_SIZE];
	if (track < 0 || track >= kNumTracks || !lookUp(track, ref, stack, memory)) {
		return false;
	}
	hits = ref.row->notes.getNumElements();
	rotation = ref.row->euclidRotation;
	span = ref.span;
	return true;
}

void playbackBegun() {
	for (int32_t t = 0; t < kNumTracks; t++) {
		counted_[t] = 0;
		if (rules_[t].active()) {
			lastRise_[t] = cvEngine.gateRiseCount(rules_[t].source);
		}
	}
}

void tick() {
	bool running = playbackHandler.isEitherClockActive();
	for (int32_t t = 0; t < kNumTracks; t++) {
		const EvolveRule& r = rules_[t];
		if (!r.active()) {
			continue;
		}
		uint32_t rises = cvEngine.gateRiseCount(r.source);
		uint32_t delta = rises - lastRise_[t];
		lastRise_[t] = rises;
		if (!running || frozen_) {
			continue;
		}
		counted_[t] += std::min<uint32_t>(delta, 16);
		int32_t fired = 0;
		while (counted_[t] >= r.every && fired < 4) {
			counted_[t] -= r.every;
			apply(t);
			fired++;
		}
	}
}

} // namespace deluge::euclid::evolver
