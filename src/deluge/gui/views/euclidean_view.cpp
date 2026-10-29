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

	int32_t pos = modelStack->getLastProcessedPos();
	if (pos < 0) {
		return -1;
	}

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
	int32_t effectiveLength = modelStack->getLoopLength();
	int32_t numSteps = (uint32_t)(effectiveLength - 1) / stepWidth + 1; // Round up

	int32_t oldHits = noteRow->notes.getNumElements();
	int32_t newHits = std::clamp<int32_t>(oldHits + offset, 0, numSteps);
	if (newHits == oldHits) {
		showHitsPopup(channel, oldHits, std::min(numSteps, kMaxSteps));
		return;
	}

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
		note->pos = (uint32_t)(i * numSteps) / (uint32_t)newHits * stepWidth;
		note->length = stepWidth;
		note->probability = noteRow->getDefaultProbability();
		note->velocity = static_cast<Instrument*>(clip->output)->defaultVelocity;
		note->lift = kDefaultLiftValue;
		note->iterance = noteRow->getDefaultIterance();
		note->fill = noteRow->getDefaultFill(modelStack);
	}

	// The last step can be shorter than a full step if the track length isn't a multiple of it
	if (newHits) {
		Note* last = newNotes.getElement(newHits - 1);
		int32_t maxLength = effectiveLength - last->pos;
		if (last->length > maxLength) {
			last->length = maxLength;
		}
	}

	// Record for undo (this steals the old note data into the undo history), then swap the new notes in
	Action* action = actionLogger.getNewAction(ActionType::EUCLIDEAN_NUM_EVENTS_EDIT, ActionAddition::NOT_ALLOWED);
	if (action) {
		action->recordNoteArrayChangeDefinitely(clip, modelStack->noteRowId, &noteRow->notes, true);
	}
	noteRow->notes.swapStateWith(&newNotes);

	clip->expectEvent();

	showHitsPopup(channel, newHits, std::min(numSteps, kMaxSteps));
	uiNeedsRendering(this, rowsForChannel(channel), 0);
}

void EuclideanView::encoderTurned(int32_t channel, int32_t offset) {
	if (Buttons::isShiftButtonPressed()) {
		editTrackLength(channel, offset);
	}
	else {
		editTrackHits(channel, offset);
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

	// BACK, or SHIFT+KIT again, returns to the normal Kit clip view
	if (b == BACK || (b == KIT && Buttons::isShiftButtonPressed())) {
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
	// Only act on presses on the step pads (not the sidebar)
	if (!velocity || x >= kDisplayWidth) {
		return ActionResult::DEALT_WITH;
	}

	// Work out which track and step this pad is: the upper row holds steps 1-16, the lower row 17-32
	int32_t channel = -1;
	int32_t step = 0;
	for (int32_t c = 0; c < kNumGateTracks; c++) {
		if (y == topRowForChannel(c)) {
			channel = c;
			step = x;
			break;
		}
		if (y == bottomRowForChannel(c)) {
			channel = c;
			step = kDisplayWidth + x;
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

	uint32_t stepWidth = currentSong->xZoom[NAVIGATION_CLIP];
	if (step >= getNumSteps(modelStack, stepWidth)) {
		return ActionResult::DEALT_WITH; // Beyond this track's length
	}

	int32_t stepStart = step * stepWidth;

	// Is there already a gate event on this step?
	bool hasHit = false;
	for (int32_t i = 0; i < noteRow->notes.getNumElements(); i++) {
		if ((uint32_t)noteRow->notes.getElement(i)->pos / stepWidth == (uint32_t)step) {
			hasHit = true;
			break;
		}
	}

	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);

	if (hasHit) {
		noteRow->clearArea(stepStart, stepWidth, modelStack, action, clip->getWrapEditLevel());
	}
	else {
		noteRow->attemptNoteAdd(stepStart, stepWidth, static_cast<Instrument*>(clip->output)->defaultVelocity,
		                        noteRow->getDefaultProbability(), noteRow->getDefaultIterance(),
		                        noteRow->getDefaultFill(modelStack), modelStack, action);
	}

	clip->expectEvent();
	uiNeedsRendering(this, 1 << y, 0);
	return ActionResult::DEALT_WITH;
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
	uint32_t hits = 0;
	for (int32_t i = 0; i < noteRow->notes.getNumElements(); i++) {
		uint32_t step = (uint32_t)noteRow->notes.getElement(i)->pos / stepWidth;
		if (step < (uint32_t)kMaxSteps) {
			hits |= (1u << step);
		}
	}

	RGB colour = colourForChannel(channel);
	RGB empty = colour.dim(3);
	int32_t playheadStep = getPlayheadStep(clip, channel);

	for (int32_t step = 0; step < numSteps; step++) {
		int32_t y = (step < kDisplayWidth) ? topRowForChannel(channel) : bottomRowForChannel(channel);
		if (!(whichRows & (1u << y))) {
			continue;
		}

		bool isHit = hits & (1u << step);
		RGB pad = isHit ? colour : empty;
		if (step == playheadStep) {
			pad = isHit ? colours::white : RGB::monochrome(60);
		}
		image[y][step % kDisplayWidth] = pad;
	}
}

bool EuclideanView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                  uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}

	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (whichRows & (1 << y)) {
			std::fill(image[y] + kDisplayWidth, image[y] + kDisplayWidth + kSideBarWidth, colours::black);
			if (occupancyMask) {
				memset(occupancyMask[y] + kDisplayWidth, 0, kSideBarWidth);
			}
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
