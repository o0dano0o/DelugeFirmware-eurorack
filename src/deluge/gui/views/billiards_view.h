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

/// Billiards: balls bouncing round the pad grid (see billiards_engine.h).
///
/// Main grid: each ball is a coloured pad that flashes white when it bounces; the floor is the bottom row. Tap any pad
///   to put a bumper there (dim blue) or take it away.
/// Sidebar: the left column is the bounce (how much speed a wall or bumper leaves a ball; tap to set it). The right
///   column shows gates 1 to 4 from the top, lit while they pulse.
///
/// Controls:
///   upper black encoder          launch speed
///   lower black encoder          gravity
///   select encoder               page for the two gold knobs: balls and CV ball / bounce and range / gate mode and clear
///   lower and upper gold knobs   the first and second value on that page
///   SHIFT + select encoder turn  clockwise: tempo sync, anticlockwise: free time
///   select encoder press         restart;  SHIFT + press: launch every ball in a new direction
///   CV button                    Billiards on (takes every output still on Euclid) or off
///   BACK                         return to where you came from
class BilliardsView final : public ClipView {
public:
	BilliardsView() = default;

	bool opened() override;
	void focusRegained() override;
	UIType getUIType() override { return UIType::BILLIARDS; }
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
	void showSpeed();
	void showGravity();

	int32_t page_ = 0;
	uint32_t lastSignature_ = 0xFFFFFFFF;
	uint32_t lastHits_[4] = {};
	uint32_t flashUntil_[4] = {};
};

extern BilliardsView billiardsView;
