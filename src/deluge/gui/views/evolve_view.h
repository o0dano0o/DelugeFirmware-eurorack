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

/// Lets the four Euclidean gate tracks change each other over time.
///
/// Each gate track gets a pair of pad rows (gate 1 at the top):
///   upper row   x0 off (this track is not influenced)       x1-4 which gate's hits drive it (its own number = drift)
///               x6 changes its number of hits   x7 changes its rotation
///               x9 up  x10 down  x11 bounce back and forth  x12 wander at random
///               x14 puts the track back to how it was when it was first set listening
///   lower row   tap a pad to change every N hits of the source (1 to 16)
///   mute column glows while everything is frozen
///
/// Encoders (the same as in the Euclidean view): how far each change moves, 1 to 4 steps.
/// Select encoder (turn or press): freeze or run all of it.
/// BACK returns to the Euclidean view. Entered with SHIFT + KEYBOARD from there.
///
/// Changing the hits respaces the pattern evenly (hits placed by hand on that track are lost); changing the rotation
/// turns the pattern as it is.
class EvolveView final : public ClipView {
public:
	EvolveView() = default;

	bool opened() override;
	void focusRegained() override;
	UIType getUIType() override { return UIType::EVOLVE; }
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
	enum class Change { SOURCE, TARGET, DIRECTION, EVERY, AMOUNT };
	/// Tells the player what a tap just did (a full description on OLED, only the change on the 7-segment display)
	void showRule(int32_t track, Change what);
	void changeAmount(int32_t track, int32_t offset);
	void refreshOLED();

	uint32_t lastChangeCount_ = 0;
};

extern EvolveView evolveView;
