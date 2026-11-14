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

/// Marbles-style random voltages and gates.
///
/// Main grid: the two looping random voltages as bars. The top four rows are X1, the bottom four are X2 (one column
///   per loop step, as many columns as the loop is long). A white cap marks the step playing now. Tap a pad in a bar
///   to set that step's height; it keeps that value for as long as the déjà vu amount keeps replaying it.
/// Sidebar: left column is the déjà vu amount (tap a pad to set it, tap the top lit pad to zero it). Right column
///   shows gates 1 to 4 from the top, lit while they pulse (dim if Marbles doesn't own that gate).
///
/// Controls:
///   upper black encoder          déjà vu: 0% always new numbers, 100% locked loop
///   lower black encoder          loop length of X1 (1 to 16);  SHIFT + it: loop length of X2
///   select encoder               page for the two gold knobs: Voltage / Shape / Clock / Gates
///   upper and lower gold knobs   the two settings on that page
///                                  Voltage: spread, bias        Shape: steps (glide to jump), range in volts
///                                  Clock:   rate, jitter        Gates: Bernoulli (gate 2 vs 3), gate 4 division
///   SHIFT + select encoder       free time or tempo sync
///   select encoder press         new random loop;  SHIFT + press: restart from the first step
///   CV button                    Marbles on (takes every free output) or off
///   BACK                         return to where you came from
///
/// Opened from the Eurorack overview by tapping the meter of an output that Marbles drives.
class MarblesView final : public ClipView {
public:
	MarblesView() = default;

	bool opened() override;
	void focusRegained() override;
	UIType getUIType() override { return UIType::MARBLES; }
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

extern MarblesView marblesView;
