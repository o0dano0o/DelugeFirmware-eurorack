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

/// Neurons: four integrate-and-fire neurons that excite and inhibit each other (see neurons_engine.h).
///
/// Main grid: one row per neuron, neuron 1 at the top. Pads 1 to 4 of a row are the weights feeding that neuron from
///   neurons 1 to 4 (green excites, red inhibits, dim is nothing; tap to step through them; the pad where a neuron
///   meets itself is unused). Pad 5 flashes white when the neuron fires. Pads 6 to 16 are its membrane value, filling
///   up until it fires. The two bottom rows show the voltage on CV 1 and CV 2.
/// Sidebar: left column is the noise amount (tap a pad to set it, tap the top lit pad to zero it). Right column shows
///   gates 1 to 4 from the top, lit while they pulse (dim if Neurons doesn't own that gate).
///
/// Controls:
///   upper black encoder          noise
///   lower black encoder          leak
///   select encoder               page for the two gold knobs: rates 1 and 2 / rates 3 and 4 / CV sources / range and
///                                weight preset
///   lower and upper gold knobs   the first and second setting on that page
///   SHIFT + select encoder       clockwise: tempo sync (time runs with the song tempo), anticlockwise: free time
///   select encoder press         restart;  SHIFT + press: random weights
///   CV button                    Neurons on (takes every output still on Euclid) or off
///   BACK                         return to where you came from
class NeuronsView final : public ClipView {
public:
	NeuronsView() = default;

	bool opened() override;
	void focusRegained() override;
	UIType getUIType() override { return UIType::NEURONS; }
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
	int32_t preset_ = 0;
	uint32_t lastSignature_ = 0xFFFFFFFF;
};

extern NeuronsView neuronsView;
