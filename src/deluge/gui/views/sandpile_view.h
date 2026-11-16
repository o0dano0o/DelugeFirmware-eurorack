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

/// Sandpile: grains dropping on the pad grid and sliding away in avalanches (see sandpile_engine.h).
///
/// Main grid: each pad is a cell, dark with no grains, then dim blue, green and gold for one, two and three grains. A
///   cell flashes white for a moment when it topples. Tap a pad to add a grain, SHIFT + tap to take one off.
/// Sidebar: the left column is the drop rate (tap to set it). The right column shows gates 1 to 4 from the top.
///
/// Controls:
///   upper black encoder          drop rate
///   lower black encoder          the size an avalanche has to reach to fire gate 4
///   select encoder               page for the two gold knobs: drop mode and topple speed / range
///   lower and upper gold knobs   the first and second value on that page
///   SHIFT + select encoder turn  clockwise: tempo sync, anticlockwise: free time
///   select encoder press         restart with a filled pile;  SHIFT + press: empty the table
///   CV button                    Sandpile on (takes every output still on Euclid) or off
///   BACK                         return to where you came from
class SandpileView final : public ClipView {
public:
	SandpileView() = default;

	bool opened() override;
	void focusRegained() override;
	UIType getUIType() override { return UIType::SANDPILE; }
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
	void showRate();
	void showBig();

	int32_t page_ = 0;
	uint32_t lastSignature_ = 0xFFFFFFFF;
};

extern SandpileView sandpileView;
