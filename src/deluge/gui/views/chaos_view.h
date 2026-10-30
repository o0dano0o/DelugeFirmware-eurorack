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

/// Pad view of the chaos engine: the three coupled "sloths" drawn as fading trails on the main grid, with the two CV
/// outputs as level bars in the sidebar.
///
/// Main grid (16 x 8): each sloth's x value picks the column and its z value the row, so a sloth traces its
/// attractor. The pad under the sloth is lit white; the trail it leaves fades over a fraction of that sloth's loop, so
/// slow sloths keep longer trails. Sloth A is red, B green, C blue.
/// Sidebar: left column is CV 1, right column CV 2 (8 levels, 0 to 10 V).
///
/// Controls:
///   tap any main pad     -> cycle the view: all sloths, A only, B only, C only
///   select encoder       -> chaos model (Lorenz, Rossler, Thomas)
///   upper black encoder  -> sloth A period      lower black encoder -> sloth B period
///   lower gold knob      -> sloth C period      upper gold knob     -> coupling
///   BACK                 -> return to the Euclidean view
///
/// Entered with the CV button from the Euclidean view. Opening it switches CV 1 and CV 2 to chaos if nothing was using chaos.
class ChaosView final : public ClipView {
public:
	ChaosView() = default;

	bool opened() override;
	void focusRegained() override;
	UIType getUIType() override { return UIType::CHAOS; }
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
	static constexpr int32_t kNumViews = 4; ///< all, A, B, C

	void changePeriod(int32_t sloth, int32_t offset);
	void changeCoupling(int32_t offset);
	void showPopup(char const* text, bool scrollOn7Seg = false);
	/// Seconds one loop of this sloth takes right now (periods can be in beats)
	float periodSeconds(int32_t sloth) const;

	/// Brightness (0 to 1) of each pad for each sloth; stamped from the engine and faded each frame
	float trail_[3][kDisplayHeight][kDisplayWidth] = {};
	int32_t headRow_[3] = {0, 0, 0};
	int32_t headCol_[3] = {0, 0, 0};
	int32_t view_ = 0;
	uint32_t lastFrameTimer_ = 0;
};

extern ChaosView chaosView;
