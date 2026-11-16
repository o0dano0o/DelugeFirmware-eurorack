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

/// Cross-modulation: up to four routes, each one lets a number from one generator move a setting of another.
/// See crossmod_engine.h for the maths.
///
/// Main grid: the top four rows are the routes' sources (route 1 at the top), the bottom four rows the same routes'
///   targets (route 1 at the top of that half). A row's pads are the choices, coloured by which generator they belong
///   to: tap one to pick it, tap it again to clear it. The chosen source pad brightens with the live value. The route
///   being edited is shown brighter than the others.
/// Sidebar: left column is the depth of the route being edited (green adds, red subtracts; tap a pad to set it, tap
///   the top lit pad to zero it). Right column, top four rows: how far each route is pushing its target right now.
///
/// Controls:
///   select encoder               which route to edit
///   upper black encoder          depth, 5% a click
///   lower black encoder          depth, 25% a click
///   lower gold knob              source (turn past the end for none)
///   upper gold knob              target (turn past the end for none)
///   select encoder press         clear the route being edited
///   BACK                         return to where you came from
class CrossmodView final : public ClipView {
public:
	CrossmodView() = default;

	bool opened() override;
	void focusRegained() override;
	UIType getUIType() override { return UIType::CROSSMOD; }
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
	void changeDepth(int32_t steps);
	void showDepth();
	void showSource();
	void showTarget();

	int32_t route_ = 0;
	uint32_t lastSignature_ = 0xFFFFFFFF;
};

extern CrossmodView crossmodView;
