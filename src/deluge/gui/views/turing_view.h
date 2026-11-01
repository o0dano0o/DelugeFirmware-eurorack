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

/// Turing machine pitch generator: a looping random sequence that you can lock, let mutate, or randomise.
///
/// Main grid:
///   top row      the shift register, one pad per bit (bright = 1, dim = 0, dark beyond the loop length). New bits
///                enter at the left and move right as the clock steps. Tap a pad to flip that bit.
///   lower rows   the last 16 notes as a dot plot, newest on the right, higher notes higher up
/// Sidebar: left column is the flip probability (empty = locked loop, half = fully random, full = always flips),
///   right column is the current note.
///
/// Controls:
///   upper black encoder  flip probability       lower black encoder  loop length (2 to 16)
///   lower gold knob      clock division         upper gold knob      octaves of range
///   select encoder       clock: song / chaos lobe / chaos loop
///   select encoder press randomise the register     SHIFT + press   random source: RNG / chaos
///   CV button            channel: off, CV/gate 1, CV/gate 2
///   BACK                 return to the Euclidean view
///
/// Entered with the KEYBOARD button from the Euclidean view. Opening it switches the Turing machine on (CV/gate 1) if
/// it was off.
class TuringView final : public ClipView {
public:
	TuringView() = default;

	bool opened() override;
	void focusRegained() override;
	UIType getUIType() override { return UIType::TURING; }
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

	uint32_t lastFrameTimer_ = 0;
	uint32_t lastDrawnStep_ = 0xFFFFFFFF;
	bool lastDrawnFlash_ = false;
};

extern TuringView turingView;
