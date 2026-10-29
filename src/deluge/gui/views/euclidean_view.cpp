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

#include "gui/views/euclidean_view.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/menu_item/colour.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/pad_leds.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_note_row_length.h"
#include "model/drum/gate_drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/chaos_runner.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include <algorithm>
#include <cstring>
#include <new>

using namespace deluge;
using namespace gui;

EuclideanView euclideanView{};

namespace {

// ---- Control mapping -------------------------------------------------------------------------------------------
// Which gate track (0-3, i.e. gate outputs 1-4) each physical encoder edits the length of. Change these to remap.
constexpr int32_t kChannelForModEncoder[2] = {3, 2}; // lower gold knob (mod encoder 0), upper gold knob (mod encoder 1)
constexpr int32_t kChannelForHorizontalEncoder = 0;  // upper black encoder
constexpr int32_t kChannelForVerticalEncoder = 1;    // lower black encoder

// ---- Layout ----------------------------------------------------------------------------------------------------
/// Each track uses two pad rows, giving up to 32 steps: steps 1-16 on the upper row, steps 17-32 on the lower row.
/// Track 0 is at the top of the grid. The playhead is drawn as a white step on whichever row it is on.
constexpr int32_t topRowForChannel(int32_t channel) {
	return kDisplayHeight - 1 - 2 * channel;
}
constexpr int32_t bottomRowForChannel(int32_t channel) {
	return topRowForChannel(channel) - 1;
}
constexpr uint32_t rowsForChannel(int32_t channel) {
	return (1u << topRowForChannel(channel)) | (1u << bottomRowForChannel(channel));
}

/// Spread the four tracks around the colour wheel so they're easy to tell apart
RGB colourForChannel(int32_t channel) {
	return RGB::fromHue(channel * 48);
}

/// Writes a small non-negative number as decimal text, returning the new end of the string
char* appendNumber(char* out, int32_t value) {
	if (value >= 10) {
		*out++ = '0' + (value / 10) % 10;
	}
	*out++ = '0' + value % 10;
	return out;
}

/// Insertion sort of a (small) note vector by position
void sortNotesByPos(NoteVector& notes) {
	for (int32_t i = 1; i < notes.getNumElements(); i++) {
		int32_t j = i;
		while (j > 0 && notes.getElement(j - 1)->pos > notes.getElement(j)->pos) {
			std::swap(*notes.getElement(j - 1), *notes.getElement(j));
			j--;
		}
	}
}

} // namespace

bool EuclideanView::opened() {
	ensureGateRows(getCurrentInstrumentClip());
	focusRegained();
	return true;
}

void EuclideanView::ensureGateRows(InstrumentClip* clip) {
	if (!clip || clip->type != ClipType::INSTRUMENT || clip->output->type != OutputType::KIT) {
		return;
	}

	Kit* kit = static_cast<Kit*>(clip->output);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	bool madeChanges = false;

	for (int32_t channel = 0; channel < kNumGateTracks; channel++) {
		int32_t existingIndex;
		if (findGateRow(clip, channel, existingIndex)) {
			continue;
		}

		// Adding rows restructures the Kit, which can't be undone by the normal undo history
		if (!madeChanges) {
			actionLogger.deleteAllLogs();
			madeChanges = true;
		}

		// Reuse a gate drum for this output if the Kit already has one that no row in this clip is using
		Drum* drum = kit->getGateDrumForChannel(channel);
		if (!drum) {
			drum = StorageManager::createNewDrum(DrumType::GATE);
			if (!drum) {
				display->displayError(Error::INSUFFICIENT_RAM);
				break;
			}
			static_cast<GateDrum*>(drum)->channel = channel;
			kit->addDrum(drum);
		}

		int32_t noteRowIndex;
		NoteRow* noteRow = clip->createNewNoteRowForKit(modelStack, false, &noteRowIndex);
		if (!noteRow) {
			display->displayError(Error::INSUFFICIENT_RAM);
			break;
		}

		ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowIndex, noteRow);
		ParamManager paramManager;
		noteRow->setDrum(drum, kit, modelStackWithNoteRow, nullptr, &paramManager);
	}

	if (madeChanges) {
		kit->beenEdited();
	}
}

void EuclideanView::focusRegained() {
	ClipView::focusRegained();

	Clip* clip = getCurrentClip();
	if (clip) {
		view.focusRegained();
		view.setActiveModControllableTimelineCounter(clip);
	}

	for (int32_t& step : lastDrawnPlayheadStep_) {
		step = -1;
	}

	// The clip view's playhead is a column of LEDs beside the pads that only that view updates. This view draws its
	// own playheads in the rows, so switch the column off rather than leave it frozen where the clip view left it.
	PadLEDs::clearTickSquares();

	for (bool& held : encoderHeld_) {
		held = false;
	}
	heldPad_ = HeldPad{};

	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		display->setScrollingText("EUCLID");
	}

	uiNeedsRendering(this);
}

NoteRow* EuclideanView::findGateRow(InstrumentClip* clip, int32_t channel, int32_t& noteRowIndex) {
	if (!clip || clip->type != ClipType::INSTRUMENT || clip->output->type != OutputType::KIT) {
		return nullptr;
	}

	for (int32_t i = 0; i < clip->noteRows.getNumElements(); i++) {
		NoteRow* noteRow = clip->noteRows.getElement(i);
		Drum* drum = noteRow->drum;
		if (drum && drum->type == DrumType::GATE && static_cast<GateDrum*>(drum)->channel == channel) {
			noteRowIndex = i;
			return noteRow;
		}
	}
	return nullptr;
}

int32_t EuclideanView::getNumSteps(ModelStackWithNoteRow* modelStack, uint32_t stepWidth) {
	int32_t length = modelStack->getLoopLength();
	int32_t numSteps = (uint32_t)(length - 1) / stepWidth + 1; // Round up
	return std::min(numSteps, kMaxSteps);
}

int32_t EuclideanView::getPlayheadStep(InstrumentClip* clip, int32_t channel) {
	if (!playbackHandler.isEitherClockActive() || !currentSong->isClipActive(clip)) {
		return -1;
	}

	int32_t noteRowIndex;
	NoteRow* noteRow = findGateRow(clip, channel, noteRowIndex);
	if (!noteRow) {
		return -1;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithClip = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStack =
	    modelStackWithClip->addNoteRow(clip->getNoteRowId(noteRow, noteRowIndex), noteRow);

	// The live position is the one the normal clip view draws its playheads from: it includes the ticks elapsed
	// since the last processed tick, so the dot moves smoothly and doesn't lag or skip steps between ticks.
	int32_t length = modelStack->getLoopLength();
	if (length <= 0) {
		return -1;
	}
	int32_t pos = (int32_t)(noteRow->getLivePos(modelStack) % (uint32_t)length);

	uint32_t stepWidth = currentSong->xZoom[NAVIGATION_CLIP];
	int32_t numSteps = getNumSteps(modelStack, stepWidth);
	return std::min((int32_t)((uint32_t)pos / stepWidth), numSteps - 1);
}

// ---- Editing ---------------------------------------------------------------------------------------------------

void EuclideanView::editTrackLength(int32_t channel, int32_t offset) {
	InstrumentClip* clip = getCurrentInstrumentClip();

	int32_t noteRowIndex;
	NoteRow* noteRow = findGateRow(clip, channel, noteRowIndex);
	if (!noteRow) {
		// Nothing to edit for this gate - say so rather than silently ignoring the knob
		char text[24];
		char* end = text;
		memcpy(end, "No gate row ", 12);
		end += 12;
		end = appendNumber(end, channel + 1);
		*end = '\0';
		display->popupTextTemporary(text);
		return;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithClip = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStack =
	    modelStackWithClip->addNoteRow(clip->getNoteRowId(noteRow, noteRowIndex), noteRow);

	uint32_t stepWidth = currentSong->xZoom[NAVIGATION_CLIP];
	int32_t oldNumSteps = getNumSteps(modelStack, stepWidth);
	int32_t newNumSteps = oldNumSteps + offset;

	// Can't go below one step, or past what fits on the pads
	if (newNumSteps < 1 || newNumSteps > kMaxSteps) {
		showLengthPopup(channel, oldNumSteps);
		return;
	}

	int32_t newLength = newNumSteps * stepWidth;
	if (newLength > kMaxSequenceLength) {
		return;
	}

	int32_t oldPos = modelStack->getLastProcessedPos(); // Must be grabbed before the change
	bool hadIndependentPlayPosBefore = noteRow->hasIndependentPlayPos();

	// Same mechanism the Kit clip view uses for per-row length, so it plays correctly and can be undone
	Action* action = actionLogger.getNewAction(ActionType::NOTEROW_LENGTH_EDIT, ActionAddition::NOT_ALLOWED);
	if (!action) {
		display->displayError(Error::INSUFFICIENT_RAM);
		return;
	}

	void* consequenceMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceNoteRowLength));
	if (!consequenceMemory) {
		display->displayError(Error::INSUFFICIENT_RAM);
		return;
	}

	auto* consequence = new (consequenceMemory) ConsequenceNoteRowLength(modelStack->noteRowId, newLength);
	action->addConsequence(consequence);
	consequence->performChange(modelStack, action, oldPos, hadIndependentPlayPosBefore);

	clip->expectEvent();

	showLengthPopup(channel, newNumSteps);
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	uiNeedsRendering(this, rowsForChannel(channel), 0);
}

int32_t EuclideanView::rowForStep(int32_t channel, int32_t step) const {
	int32_t local = step - page_ * kStepsPerPage;
	if (local < 0 || local >= kStepsPerPage) {
		return -1;
	}
	return (local < kDisplayWidth) ? topRowForChannel(channel) : bottomRowForChannel(channel);
}

void EuclideanView::showPagePopup() {
	char text[24];
	char* end = text;
	if (display->haveOLED()) {
		memcpy(end, "Steps ", 6);
		end += 6;
		end = appendNumber(end, page_ * kStepsPerPage + 1);
		*end++ = '-';
		end = appendNumber(end, (page_ + 1) * kStepsPerPage);
		*end = '\0';
		display->popupTextTemporary(text);
	}
	else {
		*end++ = 'P';
		end = appendNumber(end, page_ + 1);
		*end = '\0';
		display->displayPopup(text);
	}
}

int32_t EuclideanView::getPatternSpan(NoteRow* noteRow, int32_t numSteps) {
	int32_t padding = std::min<int32_t>(noteRow->euclidPadding, std::max<int32_t>(numSteps - 1, 0));
	return std::max<int32_t>(numSteps - padding, 1);
}

void EuclideanView::editTrackHits(int32_t channel, int32_t offset) {
	InstrumentClip* clip = getCurrentInstrumentClip();

	int32_t noteRowIndex;
	NoteRow* noteRow = findGateRow(clip, channel, noteRowIndex);
	if (!noteRow) {
		return;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithClip = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStack =
	    modelStackWithClip->addNoteRow(clip->getNoteRowId(noteRow, noteRowIndex), noteRow);

	uint32_t stepWidth = currentSong->xZoom[NAVIGATION_CLIP];
	int32_t numSteps = (uint32_t)(modelStack->getLoopLength() - 1) / stepWidth + 1; // Round up
	int32_t span = getPatternSpan(noteRow, numSteps);

	int32_t oldHits = noteRow->notes.getNumElements();
	int32_t newHits = std::clamp<int32_t>(oldHits + offset, 0, span);
	if (newHits == oldHits) {
		showHitsPopup(channel, oldHits, std::min(span, kMaxSteps));
		return;
	}

	applyPattern(channel, newHits);
	showHitsPopup(channel, newHits, std::min(span, kMaxSteps));
}

void EuclideanView::applyPattern(int32_t channel, int32_t newHits) {
	InstrumentClip* clip = getCurrentInstrumentClip();

	int32_t noteRowIndex;
	NoteRow* noteRow = findGateRow(clip, channel, noteRowIndex);
	if (!noteRow) {
		return;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithClip = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStack =
	    modelStackWithClip->addNoteRow(clip->getNoteRowId(noteRow, noteRowIndex), noteRow);

	uint32_t stepWidth = currentSong->xZoom[NAVIGATION_CLIP];
	int32_t effectiveLength = modelStack->getLoopLength();
	int32_t numSteps = (uint32_t)(effectiveLength - 1) / stepWidth + 1; // Round up
	int32_t span = getPatternSpan(noteRow, numSteps);
	newHits = std::clamp<int32_t>(newHits, 0, span);

	// Build the new, evenly spread set of gate events up front so nothing can fail halfway through
	NoteVector newNotes;
	if (newHits) {
		Error error = newNotes.insertAtIndex(0, newHits);
		if (error != Error::NONE) {
			display->displayError(error);
			return;
		}
	}

	for (int32_t i = 0; i < newHits; i++) {
		Note* note = newNotes.getElement(i);
		int32_t step = (int32_t)((uint32_t)(i * span) / (uint32_t)newHits);
		step = (step + noteRow->euclidRotation) % span; // Keep any rotation the user has dialled in
		note->pos = step * stepWidth;
		note->length = stepWidth;
		note->probability = noteRow->getDefaultProbability();
		note->velocity = static_cast<Instrument*>(clip->output)->defaultVelocity;
		note->lift = kDefaultLiftValue;
		note->iterance = noteRow->getDefaultIterance();
		note->fill = noteRow->getDefaultFill(modelStack);
	}

	// A rotation can leave the events out of order, and notes must be sorted by position
	sortNotesByPos(newNotes);

	// A step can be shorter than a full step if the track length isn't a multiple of it
	for (int32_t i = 0; i < newHits; i++) {
		Note* note = newNotes.getElement(i);
		int32_t maxLength = effectiveLength - note->pos;
		if (note->length > maxLength) {
			note->length = maxLength;
		}
	}

	// Record for undo (this steals the old note data into the undo history), then swap the new notes in
	Action* action = actionLogger.getNewAction(ActionType::EUCLIDEAN_NUM_EVENTS_EDIT, ActionAddition::NOT_ALLOWED);
	if (action) {
		action->recordNoteArrayChangeDefinitely(clip, modelStack->noteRowId, &noteRow->notes, true);
	}
	noteRow->notes.swapStateWith(&newNotes);

	clip->expectEvent();
	uiNeedsRendering(this, rowsForChannel(channel), 0);
}

void EuclideanView::editTrackPadding(int32_t channel, int32_t offset) {
	InstrumentClip* clip = getCurrentInstrumentClip();

	int32_t noteRowIndex;
	NoteRow* noteRow = findGateRow(clip, channel, noteRowIndex);
	if (!noteRow) {
		return;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithClip = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStack =
	    modelStackWithClip->addNoteRow(clip->getNoteRowId(noteRow, noteRowIndex), noteRow);

	uint32_t stepWidth = currentSong->xZoom[NAVIGATION_CLIP];
	int32_t numSteps = (uint32_t)(modelStack->getLoopLength() - 1) / stepWidth + 1; // Round up

	// At least one step must be left for the pattern itself
	int32_t oldPadding = std::min<int32_t>(noteRow->euclidPadding, std::max<int32_t>(numSteps - 1, 0));
	int32_t newPadding = std::clamp<int32_t>(oldPadding + offset, 0, std::max<int32_t>(numSteps - 1, 0));
	if (newPadding == oldPadding) {
		showPaddingPopup(channel, oldPadding);
		return;
	}

	noteRow->euclidPadding = newPadding;
	int32_t span = getPatternSpan(noteRow, numSteps);
	noteRow->euclidRotation = noteRow->euclidRotation % span;

	// Respace the same number of hits over the shorter (or longer) pattern span
	applyPattern(channel, std::min<int32_t>(noteRow->notes.getNumElements(), span));
	showPaddingPopup(channel, newPadding);
	uiNeedsRendering(this, rowsForChannel(channel), 0);
}

void EuclideanView::showPaddingPopup(int32_t channel, int32_t padding) {
	if (display->haveOLED()) {
		char text[32];
		char* end = text;
		memcpy(end, "Gate ", 5);
		end += 5;
		end = appendNumber(end, channel + 1);
		memcpy(end, ": padding ", 10);
		end += 10;
		end = appendNumber(end, padding);
		*end = '\0';
		display->popupTextTemporary(text);
	}
	else {
		char text[12];
		char* end = appendNumber(text, padding);
		*end = '\0';
		display->displayPopup(text, 0, true);
	}
}

void EuclideanView::encoderTurned(int32_t channel, int32_t offset) {
	if (encoderHeld_[channel]) {
		// Encoder pressed in: turn to rotate the pattern, or hold SHIFT as well to change its padding
		if (Buttons::isShiftButtonPressed()) {
			editTrackPadding(channel, offset);
		}
		else {
			rotateTrack(channel, offset);
		}
	}
	else if (Buttons::isShiftButtonPressed()) {
		editTrackLength(channel, offset);
	}
	else {
		editTrackHits(channel, offset);
	}
}

void EuclideanView::rotateTrack(int32_t channel, int32_t offset) {
	InstrumentClip* clip = getCurrentInstrumentClip();

	int32_t noteRowIndex;
	NoteRow* noteRow = findGateRow(clip, channel, noteRowIndex);
	if (!noteRow) {
		return;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithClip = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStack =
	    modelStackWithClip->addNoteRow(clip->getNoteRowId(noteRow, noteRowIndex), noteRow);

	uint32_t stepWidth = currentSong->xZoom[NAVIGATION_CLIP];
	int32_t effectiveLength = modelStack->getLoopLength();
	int32_t numSteps = (uint32_t)(effectiveLength - 1) / stepWidth + 1; // Round up

	// Only the pattern itself rotates; any padding at the end stays put
	int32_t span = getPatternSpan(noteRow, numSteps);

	int32_t numNotes = noteRow->notes.getNumElements();
	int32_t newRotation = (((noteRow->euclidRotation + offset) % span) + span) % span;
	int32_t shift = (((offset % span) + span) % span); // 0 .. span-1, always forwards

	if (numNotes == 0 || shift == 0) {
		noteRow->euclidRotation = newRotation;
		showRotationPopup(channel, newRotation);
		uiNeedsRendering(this, rowsForChannel(channel), 0); // The start marker moves even if there are no hits
		return;
	}

	// Build the rotated copy up front so nothing can fail halfway through
	NoteVector newNotes;
	Error error = newNotes.insertAtIndex(0, numNotes);
	if (error != Error::NONE) {
		display->displayError(error);
		return;
	}

	for (int32_t i = 0; i < numNotes; i++) {
		Note* dest = newNotes.getElement(i);
		*dest = *noteRow->notes.getElement(i);
		int32_t step = (int32_t)((uint32_t)dest->pos / stepWidth);
		if (step < span) {
			step = (step + shift) % span;
		}
		dest->pos = step * stepWidth;
	}
	sortNotesByPos(newNotes);

	for (int32_t i = 0; i < numNotes; i++) {
		Note* note = newNotes.getElement(i);
		int32_t maxLength = effectiveLength - note->pos;
		if (note->length > maxLength) {
			note->length = maxLength;
		}
	}

	Action* action = actionLogger.getNewAction(ActionType::EUCLIDEAN_NUM_EVENTS_EDIT, ActionAddition::NOT_ALLOWED);
	if (action) {
		action->recordNoteArrayChangeDefinitely(clip, modelStack->noteRowId, &noteRow->notes, true);
	}
	noteRow->notes.swapStateWith(&newNotes);

	noteRow->euclidRotation = newRotation;
	clip->expectEvent();

	showRotationPopup(channel, newRotation);
	uiNeedsRendering(this, rowsForChannel(channel), 0);
}

void EuclideanView::showRotationPopup(int32_t channel, int32_t rotation) {
	if (display->haveOLED()) {
		char text[32];
		char* end = text;
		memcpy(end, "Gate ", 5);
		end += 5;
		end = appendNumber(end, channel + 1);
		memcpy(end, ": rotate ", 9);
		end += 9;
		end = appendNumber(end, rotation);
		*end = '\0';
		display->popupTextTemporary(text);
	}
	else {
		char text[12];
		char* end = appendNumber(text, rotation);
		*end = '\0';
		display->displayPopup(text, 0, true);
	}
}

void EuclideanView::showHitsPopup(int32_t channel, int32_t numHits, int32_t numSteps) {
	if (display->haveOLED()) {
		char text[32];
		char* end = text;
		memcpy(end, "Gate ", 5);
		end += 5;
		end = appendNumber(end, channel + 1);
		memcpy(end, ": ", 2);
		end += 2;
		end = appendNumber(end, numHits);
		*end++ = '/';
		end = appendNumber(end, numSteps);
		memcpy(end, " hits", 5);
		end += 5;
		*end = '\0';
		display->popupTextTemporary(text);
	}
	else {
		char text[12];
		char* end = appendNumber(text, numHits);
		*end = '\0';
		display->displayPopup(text, 0, true);
	}
}

void EuclideanView::showLengthPopup(int32_t channel, int32_t numSteps) {
	if (display->haveOLED()) {
		char text[24];
		char* end = text;
		memcpy(end, "Gate ", 5);
		end += 5;
		end = appendNumber(end, channel + 1);
		memcpy(end, ": ", 2);
		end += 2;
		end = appendNumber(end, numSteps);
		memcpy(end, " steps", 6);
		end += 6;
		*end = '\0';
		display->popupTextTemporary(text);
	}
	else {
		char text[12];
		char* end = appendNumber(text, numSteps);
		*end = '\0';
		display->displayPopup(text, 0, true);
	}
}

// ---- Input -----------------------------------------------------------------------------------------------------

ActionResult EuclideanView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// On 7-segment displays the length/hits readouts are persistent popups, so clear any left showing. This also
	// covers leaving the view, so the display it returns to isn't left with a stuck number.
	if (on) {
		display->cancelPopup();
	}

	// Pressing an encoder in makes turning it rotate that track's pattern
	{
		int32_t heldChannel = -1;
		if (b == X_ENC) {
			heldChannel = kChannelForHorizontalEncoder;
		}
		else if (b == Y_ENC) {
			heldChannel = kChannelForVerticalEncoder;
		}
		else if (b == MOD_ENCODER_0) {
			heldChannel = kChannelForModEncoder[0];
		}
		else if (b == MOD_ENCODER_1) {
			heldChannel = kChannelForModEncoder[1];
		}
		if (heldChannel >= 0) {
			encoderHeld_[heldChannel] = on;
			if (!on) {
				display->cancelPopup();
			}
			return ActionResult::DEALT_WITH;
		}
	}

	// Temporary milestone-1 switch: pressing the select encoder toggles chaos control of CV 1 and CV 2
	if (b == SELECT_ENC && on) {
		bool enable = !deluge::chaos::runner::isEnabled();
		deluge::chaos::runner::setEnabled(enable);
		if (display->haveOLED()) {
			display->popupTextTemporary(enable ? "Chaos CV on" : "Chaos CV off");
		}
		else {
			display->displayPopup(enable ? "CH ON" : "CH OFF");
		}
		return ActionResult::DEALT_WITH;
	}

	// BACK, or SHIFT+CV again, returns to the normal Kit clip view
	if (b == BACK || (b == CV && Buttons::isShiftButtonPressed())) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			changeRootUI(&instrumentClipView);
		}
		return ActionResult::DEALT_WITH;
	}

	return ClipView::buttonAction(b, on, inCardRoutine);
}

ActionResult EuclideanView::padAction(int32_t x, int32_t y, int32_t velocity) {
	// The audition column (right of the mute column) picks the page of steps: each track's upper pad shows steps 1-32,
	// its lower pad steps 33-64.
	if (x > kDisplayWidth) {
		if (velocity) {
			for (int32_t c = 0; c < kNumGateTracks; c++) {
				int32_t newPage = (y == topRowForChannel(c)) ? 0 : (y == bottomRowForChannel(c)) ? 1 : -1;
				if (newPage >= 0 && newPage < kNumPages) {
					if (newPage != page_) {
						page_ = newPage;
						heldPad_ = HeldPad{};
						refreshAll();
					}
					showPagePopup();
					break;
				}
			}
		}
		return ActionResult::DEALT_WITH;
	}

	// Letting go of a step pad. A hit that was only tapped (not turned for probability) is cleared now, so holding
	// a hit to set its probability doesn't also delete it.
	if (!velocity) {
		if (x < kDisplayWidth && heldPad_.channel >= 0) {
			HeldPad released = heldPad_;
			heldPad_ = HeldPad{};
			display->cancelPopup();
			if (released.hadHit && !released.edited) {
				if (sdRoutineLock) {
					heldPad_ = released; // Try again outside the card routine
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}
				toggleStep(released.channel, released.step, true);
			}
		}
		return ActionResult::DEALT_WITH;
	}

	// Work out which track and step this pad is: the upper row holds steps 1-16, the lower row 17-32
	int32_t channel = -1;
	int32_t step = 0;
	for (int32_t c = 0; c < kNumGateTracks; c++) {
		if (y == topRowForChannel(c)) {
			channel = c;
			step = page_ * kStepsPerPage + x;
			break;
		}
		if (y == bottomRowForChannel(c)) {
			channel = c;
			step = page_ * kStepsPerPage + kDisplayWidth + x;
			break;
		}
	}
	if (channel < 0) {
		return ActionResult::DEALT_WITH;
	}

	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	InstrumentClip* clip = getCurrentInstrumentClip();
	int32_t noteRowIndex;
	NoteRow* noteRow = findGateRow(clip, channel, noteRowIndex);
	if (!noteRow) {
		return ActionResult::DEALT_WITH;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithClip = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStack =
	    modelStackWithClip->addNoteRow(clip->getNoteRowId(noteRow, noteRowIndex), noteRow);

	// Mute column: either row of a track toggles that track's mute
	if (x == kDisplayWidth) {
		clip->toggleNoteRowMute(modelStack);
		uiNeedsRendering(this, 0, rowsForChannel(channel));
		return ActionResult::DEALT_WITH;
	}

	uint32_t stepWidth = currentSong->xZoom[NAVIGATION_CLIP];
	if (step >= getNumSteps(modelStack, stepWidth)) {
		return ActionResult::DEALT_WITH; // Beyond this track's length
	}

	// Is there already a gate event on this step?
	bool hasHit = false;
	for (int32_t i = 0; i < noteRow->notes.getNumElements(); i++) {
		if ((uint32_t)noteRow->notes.getElement(i)->pos / stepWidth == (uint32_t)step) {
			hasHit = true;
			break;
		}
	}

	heldPad_.channel = channel;
	heldPad_.step = step;
	heldPad_.hadHit = hasHit;
	heldPad_.edited = false;

	// An empty step gets its hit straight away. An existing hit is left alone until the pad is released, so it can be
	// held while turning the select knob to set its probability.
	if (!hasHit) {
		toggleStep(channel, step, false);
	}
	return ActionResult::DEALT_WITH;
}

void EuclideanView::toggleStep(int32_t channel, int32_t step, bool clear) {
	InstrumentClip* clip = getCurrentInstrumentClip();
	int32_t noteRowIndex;
	NoteRow* noteRow = findGateRow(clip, channel, noteRowIndex);
	if (!noteRow) {
		return;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithClip = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStack =
	    modelStackWithClip->addNoteRow(clip->getNoteRowId(noteRow, noteRowIndex), noteRow);

	uint32_t stepWidth = currentSong->xZoom[NAVIGATION_CLIP];
	int32_t stepStart = step * stepWidth;

	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);

	if (clear) {
		noteRow->clearArea(stepStart, stepWidth, modelStack, action, clip->getWrapEditLevel());
	}
	else {
		noteRow->attemptNoteAdd(stepStart, stepWidth, static_cast<Instrument*>(clip->output)->defaultVelocity,
		                        noteRow->getDefaultProbability(), noteRow->getDefaultIterance(),
		                        noteRow->getDefaultFill(modelStack), modelStack, action);
	}

	clip->expectEvent();
	int32_t row = rowForStep(channel, step);
	if (row >= 0) {
		uiNeedsRendering(this, 1 << row, 0);
	}
}

void EuclideanView::selectEncoderAction(int8_t offset) {
	// Only does something while a hit's pad is held: it then sets that hit's probability, as in the normal clip view
	if (heldPad_.channel < 0) {
		return;
	}

	InstrumentClip* clip = getCurrentInstrumentClip();
	int32_t noteRowIndex;
	NoteRow* noteRow = findGateRow(clip, heldPad_.channel, noteRowIndex);
	if (!noteRow) {
		return;
	}

	uint32_t stepWidth = currentSong->xZoom[NAVIGATION_CLIP];
	Note* note = nullptr;
	for (int32_t i = 0; i < noteRow->notes.getNumElements(); i++) {
		Note* candidate = noteRow->notes.getElement(i);
		if ((uint32_t)candidate->pos / stepWidth == (uint32_t)heldPad_.step) {
			note = candidate;
			break;
		}
	}
	if (!note) {
		return; // Nothing on that step to give a probability to
	}

	// Probability is stored in 5% units (1-20). Anything above that is a "based on previous" variant, which this view
	// doesn't offer, so it's treated as its plain percentage.
	int32_t current = note->getProbability() & 127;
	if (current > kNumProbabilityValues) {
		current = kNumProbabilityValues;
	}
	int32_t updated = std::clamp<int32_t>(current + offset, 1, kNumProbabilityValues);

	if (updated != current) {
		Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
		if (action) {
			action->recordNoteArrayChangeIfNotAlreadySnapshotted(clip, clip->getNoteRowId(noteRow, noteRowIndex),
			                                                     &noteRow->notes, false);
		}
		// The array may have been snapshotted (and its data moved), so look the note up again
		for (int32_t i = 0; i < noteRow->notes.getNumElements(); i++) {
			Note* candidate = noteRow->notes.getElement(i);
			if ((uint32_t)candidate->pos / stepWidth == (uint32_t)heldPad_.step) {
				candidate->setProbability(updated);
				break;
			}
		}
		clip->expectEvent();
		int32_t row = rowForStep(heldPad_.channel, heldPad_.step);
		if (row >= 0) {
			uiNeedsRendering(this, 1 << row, 0);
		}
	}

	heldPad_.edited = true;
	showProbabilityPopup(updated);
}

void EuclideanView::showProbabilityPopup(int32_t probability) {
	char text[32];
	if (display->haveOLED()) {
		char* end = text;
		memcpy(end, "Probability ", 12);
		end += 12;
		end = appendNumber(end, probability * 5);
		*end++ = '%';
		*end = '\0';
		display->popupText(text, PopupType::PROBABILITY);
	}
	else {
		char* end = appendNumber(text, probability * 5);
		*end = '\0';
		display->displayPopup(text, 0, true, 255, 1, PopupType::PROBABILITY);
	}
}

ActionResult EuclideanView::horizontalEncoderAction(int32_t offset) {
	encoderTurned(kChannelForHorizontalEncoder, offset);
	return ActionResult::DEALT_WITH;
}

ActionResult EuclideanView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	encoderTurned(kChannelForVerticalEncoder, offset);
	return ActionResult::DEALT_WITH;
}

void EuclideanView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (whichModEncoder >= 0 && whichModEncoder < 2) {
		encoderTurned(kChannelForModEncoder[whichModEncoder], offset);
	}
}

// ---- Rendering -------------------------------------------------------------------------------------------------

bool EuclideanView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	if (!image) {
		return true;
	}

	PadLEDs::renderingLock = true;

	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (whichRows & (1 << y)) {
			std::fill(image[y], image[y] + kDisplayWidth, colours::black);
			if (occupancyMask) {
				memset(occupancyMask[y], 64, kDisplayWidth);
			}
		}
	}

	for (int32_t channel = 0; channel < kNumGateTracks; channel++) {
		renderTrack(image, channel, whichRows);
	}

	PadLEDs::renderingLock = false;
	return true;
}

void EuclideanView::renderTrack(RGB image[][kDisplayWidth + kSideBarWidth], int32_t channel, uint32_t whichRows) {
	InstrumentClip* clip = getCurrentInstrumentClip();

	int32_t noteRowIndex;
	NoteRow* noteRow = findGateRow(clip, channel, noteRowIndex);
	if (!noteRow) {
		return; // Rows stay dark: this gate isn't set up as a Kit row
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackWithClip = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ModelStackWithNoteRow* modelStack =
	    modelStackWithClip->addNoteRow(clip->getNoteRowId(noteRow, noteRowIndex), noteRow);

	uint32_t stepWidth = currentSong->xZoom[NAVIGATION_CLIP];
	int32_t numSteps = getNumSteps(modelStack, stepWidth);

	// Which steps hold a gate event
	uint64_t hits = 0;
	int32_t probabilities[kMaxSteps];
	for (int32_t& probability : probabilities) {
		probability = kNumProbabilityValues;
	}
	for (int32_t i = 0; i < noteRow->notes.getNumElements(); i++) {
		Note* note = noteRow->notes.getElement(i);
		uint32_t step = (uint32_t)note->pos / stepWidth;
		if (step < (uint32_t)kMaxSteps) {
			hits |= (1ull << step);
			probabilities[step] = std::min<int32_t>(note->getProbability() & 127, kNumProbabilityValues);
		}
	}

	RGB colour = colourForChannel(channel);
	RGB empty = colour.dim(3);
	// The pattern's start point is tinted a different shade. It moves with the rotation (it's where the un-rotated
	// step 1 has been carried to), so you can see where the rhythm begins. Its empty state is a touch brighter than
	// other empty steps so it shows even when there's no hit on it.
	int32_t span = getPatternSpan(noteRow, numSteps);
	int32_t startStep = noteRow->euclidRotation % span;
	RGB startColour = RGB::fromHue(channel * 48 + 24);
	RGB startEmpty = startColour.dim(2);
	int32_t playheadStep = getPlayheadStep(clip, channel);

	for (int32_t local = 0; local < kStepsPerPage; local++) {
		int32_t step = page_ * kStepsPerPage + local;
		if (step >= numSteps) {
			break;
		}
		int32_t y = (local < kDisplayWidth) ? topRowForChannel(channel) : bottomRowForChannel(channel);
		if (!(whichRows & (1u << y))) {
			continue;
		}

		bool isHit = hits & (1ull << step);
		RGB pad = isHit ? colour : empty;
		if (step >= span && !isHit) {
			pad = RGB::monochrome(18); // Padding: part of the track, but outside the Euclidean pattern
		}
		if (step == startStep) {
			pad = isHit ? startColour : startEmpty;
		}
		// A hit that won't always fire is shown dimmer, in proportion to its chance
		if (isHit && probabilities[step] < kNumProbabilityValues) {
			RGB faint = pad.dim(2);
			pad = RGB::blend(faint, pad, (uint16_t)(probabilities[step] * 65535 / kNumProbabilityValues));
		}
		if (step == playheadStep) {
			pad = isHit ? colours::white : RGB::monochrome(60);
		}
		image[y][local % kDisplayWidth] = pad;
	}
}

bool EuclideanView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                  uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	InstrumentClip* clip = getCurrentInstrumentClip();

	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (!(whichRows & (1 << y))) {
			continue;
		}
		std::fill(image[y] + kDisplayWidth, image[y] + kDisplayWidth + kSideBarWidth, colours::black);
		if (occupancyMask) {
			memset(occupancyMask[y] + kDisplayWidth, 0, kSideBarWidth);
		}

		// Mute pad (first sidebar column) for both rows of each track
		for (int32_t c = 0; c < kNumGateTracks; c++) {
			if (y != topRowForChannel(c) && y != bottomRowForChannel(c)) {
				continue;
			}
			int32_t noteRowIndex;
			NoteRow* noteRow = clip ? findGateRow(clip, c, noteRowIndex) : nullptr;
			if (noteRow && noteRow->muted) {
				image[y][kDisplayWidth] = menu_item::mutedColourMenu.getRGB();
				if (occupancyMask) {
					occupancyMask[y][kDisplayWidth] = 64;
				}
			}
			else {
				image[y][kDisplayWidth] = menu_item::activeColourMenu.getRGB();
			}

			// Page pad (second sidebar column): upper pad = steps 1-32, lower pad = steps 33-64, lit for the page
			// showing
			int32_t padPage = (y == topRowForChannel(c)) ? 0 : 1;
			image[y][kDisplayWidth + 1] = (padPage == page_) ? RGB::monochrome(80) : RGB::monochrome(10);
		}
	}
	return true;
}

void EuclideanView::graphicsRoutine() {
	InstrumentClip* clip = getCurrentInstrumentClip();
	if (!clip || clip->type != ClipType::INSTRUMENT) {
		return;
	}

	// Redraw only the tracks whose playhead has moved
	uint32_t rowsToRedraw = 0;
	for (int32_t channel = 0; channel < kNumGateTracks; channel++) {
		int32_t step = getPlayheadStep(clip, channel);
		if (step != lastDrawnPlayheadStep_[channel]) {
			lastDrawnPlayheadStep_[channel] = step;
			rowsToRedraw |= rowsForChannel(channel);
		}
	}

	if (rowsToRedraw) {
		uiNeedsRendering(this, rowsToRedraw, 0);
	}
}

void EuclideanView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	InstrumentClip* clip = getCurrentInstrumentClip();

#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 6;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 1;
#endif

	canvas.drawStringCentred("Euclidean", yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	// Two lines of "n: steps", gate tracks 1-2 then 3-4
	for (int32_t pair = 0; pair < 2; pair++) {
		char text[32];
		char* end = text;

		for (int32_t channel = pair * 2; channel < pair * 2 + 2; channel++) {
			if (channel != pair * 2) {
				memcpy(end, "   ", 3);
				end += 3;
			}
			end = appendNumber(end, channel + 1);
			memcpy(end, ": ", 2);
			end += 2;

			int32_t noteRowIndex;
			NoteRow* noteRow = findGateRow(clip, channel, noteRowIndex);
			if (!noteRow) {
				*end++ = '-';
				continue;
			}

			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStackWithClip =
			    currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			ModelStackWithNoteRow* modelStack =
			    modelStackWithClip->addNoteRow(clip->getNoteRowId(noteRow, noteRowIndex), noteRow);
			end = appendNumber(end, getNumSteps(modelStack, currentSong->xZoom[NAVIGATION_CLIP]));
		}

		*end = '\0';
		canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
		yPos += 12;
	}
}
