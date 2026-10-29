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
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
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
constexpr int32_t kChannelForModEncoder[2] = {0, 1}; // upper gold knob, lower gold knob
constexpr int32_t kChannelForHorizontalEncoder = 2;
constexpr int32_t kChannelForVerticalEncoder = 3;

// ---- Layout ----------------------------------------------------------------------------------------------------
/// Each track uses two pad rows. The pattern sits on the upper one and the track's playhead on the one below.
/// Track 0 is at the top of the grid.
constexpr int32_t patternRowForChannel(int32_t channel) {
	return kDisplayHeight - 1 - 2 * channel;
}
constexpr int32_t playheadRowForChannel(int32_t channel) {
	return patternRowForChannel(channel) - 1;
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
	focusRegained();
	return true;
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
	uiNeedsRendering(this, 1 << patternRowForChannel(channel), 0);
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
	// Pads aren't used for editing yet; edit the pattern itself in the Kit clip view
	return ActionResult::DEALT_WITH;
}

ActionResult EuclideanView::horizontalEncoderAction(int32_t offset) {
	editTrackLength(kChannelForHorizontalEncoder, offset);
	return ActionResult::DEALT_WITH;
}

ActionResult EuclideanView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	editTrackLength(kChannelForVerticalEncoder, offset);
	return ActionResult::DEALT_WITH;
}

void EuclideanView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (whichModEncoder >= 0 && whichModEncoder < 2) {
		editTrackLength(kChannelForModEncoder[whichModEncoder], offset);
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
		int32_t patternRow = patternRowForChannel(channel);
		int32_t playheadRow = playheadRowForChannel(channel);
		if (whichRows & (1 << patternRow)) {
			renderTrackRow(image[patternRow], channel, true);
		}
		if (whichRows & (1 << playheadRow)) {
			renderTrackRow(image[playheadRow], channel, false);
		}
	}

	PadLEDs::renderingLock = false;
	return true;
}

void EuclideanView::renderTrackRow(RGB* image, int32_t channel, bool patternRow) {
	InstrumentClip* clip = getCurrentInstrumentClip();

	int32_t noteRowIndex;
	NoteRow* noteRow = findGateRow(clip, channel, noteRowIndex);
	if (!noteRow) {
		return; // Row stays dark: this gate isn't set up as a Kit row
	}

	RGB colour = colourForChannel(channel);

	if (!patternRow) {
		int32_t step = getPlayheadStep(clip, channel);
		if (step >= 0) {
			image[step] = colours::white;
		}
		return;
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

	RGB empty = colour.dim(3);
	for (int32_t x = 0; x < numSteps; x++) {
		image[x] = (hits & (1u << x)) ? colour : empty;
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

	// Redraw only the playhead rows whose step has moved
	uint32_t rowsToRedraw = 0;
	for (int32_t channel = 0; channel < kNumGateTracks; channel++) {
		int32_t step = getPlayheadStep(clip, channel);
		if (step != lastDrawnPlayheadStep_[channel]) {
			lastDrawnPlayheadStep_[channel] = step;
			rowsToRedraw |= 1u << playheadRowForChannel(channel);
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
