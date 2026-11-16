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

/// Kuramoto: four coupled phase oscillators (see kuramoto_engine.h).
///
/// Main grid: the top four rows are the oscillators, oscillator 1 at the top. A bright pad travels left to right once
///   per cycle with a short fading trail, and flashes white when the oscillator wraps (its gate fires). Row 4 from the
///   top is how bunched up they are (the order parameter, a bar), the row below is where the middle of the group sits
///   in the cycle (one pad).
/// Sidebar: left column is the coupling (tap a pad to set it, tap the top lit pad to zero it). Right column shows
///   gates 1 to 4 from the top, lit while they pulse (dim if Kuramoto doesn't own that gate).
///
/// Controls:
///   upper black encoder          coupling
///   lower black encoder          noise
///   select encoder               page for the two gold knobs: rates 1 and 2 / rates 3 and 4 / range and rate preset
///   lower and upper gold knobs   the first and second value on that page
///   SHIFT + select encoder       clockwise: tempo sync, anticlockwise: free time
///   select encoder press         restart;  SHIFT + press: random rates
///   CV button                    Kuramoto on (takes every output still on Euclid) or off
///   BACK                         return to where you came from
class KuramotoView final : public ClipView {
public:
	KuramotoView() = default;

	bool opened() override;
	void focusRegained() override;
	UIType getUIType() override { return UIType::KURAMOTO; }
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
	void showCoupling();
	void showNoise();

	int32_t page_ = 0;
	int32_t preset_ = 0;
	uint32_t lastSignature_ = 0xFFFFFFFF;
};

extern KuramotoView kuramotoView;
