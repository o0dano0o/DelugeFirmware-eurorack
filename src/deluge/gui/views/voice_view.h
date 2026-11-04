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

#include "definitions_cxx.hpp"
#include "gui/views/clip_view.h"
#include "hid/button.h"

/// Live voice-to-CV: sing or hum into the mic (or feed the line input) and the pitch comes out on a CV output with a
/// gate on the matching gate output.
///
/// Main grid:
///   top row        input level meter
///   second row     the 12 note names (C to B) with the note you are singing lit; columns 12 to 15 show the octave
///   pads below     the last four seconds of notes as a dot plot, newest on the right, higher notes higher up
///                  (zoomed to the notes in view: the lowest on the bottom row, the highest on the top, never less
///                  than an octave apart; the slot in progress is white)
/// Controls:
///   select encoder       sensitivity (how quiet a voice is still picked up)
///   select press         snap to semitones / follow the pitch continuously
///   lower gold knob      octave shift, -2 to +2
///   CV button            channel: off, CV/gate 1, CV/gate 2
///
/// Phrase looper (record what you sing, then replay it on the same CV and gate):
///   RECORD               start recording; press again to stop, and the phrase plays at once. SHIFT + RECORD clears it
///   a pad in the plot    play the phrase from the top (when it is a one-shot it plays once). SHIFT + pad stops it
///   horizontal encoder   loop / one-shot.   SHIFT + it: timing, free or snapped to the song clock (clears the phrase)
///   upper gold knob      restart the phrase on triggers from gate 1 to 4 (or off)
/// While a phrase plays the plot shows it, with the playhead as a lit column.
///   BACK                 return to the Euclidean view
///
/// Entered with the MIDI button from the Euclidean view. It starts off: press the CV button to switch it on at CV/gate 1, 2 (and off again).
class VoiceView final : public ClipView {
public:
	VoiceView() = default;

	bool opened() override;
	void focusRegained() override;
	UIType getUIType() override { return UIType::VOICE; }
	UIModControllableContext getUIModControllableContext() override { return UIModControllableContext::CLIP; }

	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true) override;
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;
	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;
	void graphicsRoutine() override;
	ActionResult timerCallback() override;

	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	ActionResult horizontalEncoderAction(int32_t offset) override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	void modEncoderAction(int32_t whichModEncoder, int32_t offset) override;
	void selectEncoderAction(int8_t offset) override;

private:
	void showValuePopup(char const* text);
	void showNamePopup(char const* text);
	void refreshOLED();

	int32_t lastDrawnNote_ = -2;
	int32_t lastDrawnLevel_ = -1;
	uint32_t lastDrawnHistory_ = 0;
	bool lastDrawnGate_ = false;
	int32_t lastDrawnLoopColumn_ = -1;
	int32_t lastDrawnLoopState_ = -1;
};

extern VoiceView voiceView;
