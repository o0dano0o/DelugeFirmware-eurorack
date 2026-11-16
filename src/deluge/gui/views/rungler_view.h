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

/// Rungler: two oscillators and an 8-bit shift register (see rungler_engine.h).
///
/// Main grid, from the top: row 1 the register as eight pads (leftmost is the oldest bit, rightmost the newest;
///   lit = 1, tap a pad to flip it); rows 3 and 4 the phase of oscillators A and B as a growing bar; rows 6 and 7
///   the voltage on CV 1 and CV 2 as bars across the grid.
/// Sidebar: left column is the chance (tap a pad to set it, tap the top lit pad to zero it). Right column shows
///   gates 1 to 4 from the top, lit while they are high (dim if Rungler doesn't own that gate).
///
/// Controls:
///   upper black encoder          chance that oscillator A disturbs a new bit: 0% repeats every 8 shifts
///   lower black encoder          mod: how far the register bends the two oscillator rates
///   select encoder               page for the two gold knobs: Osc (rate A, rate B) / Out (slew, range)
///   lower and upper gold knobs   the first and second setting on that page
///   SHIFT + select encoder       clockwise: tempo sync, anticlockwise: free time
///   select encoder press         new random register;  SHIFT + press: restart from the first state
///   CV button                    Rungler on (takes every free output) or off
///   BACK                         return to where you came from
class RunglerView final : public ClipView {
public:
	RunglerView() = default;

	bool opened() override;
	void focusRegained() override;
	UIType getUIType() override { return UIType::RUNGLER; }
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
	void pageValueText(int32_t knob, char* out, size_t size) const;

	int32_t page_ = 0;
	uint32_t lastSignature_ = 0xFFFFFFFF;
};

extern RunglerView runglerView;
