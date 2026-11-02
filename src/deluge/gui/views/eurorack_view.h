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
#include "gui/ui/ui.h"
#include "hid/button.h"

/// Eurorack overview: which feature drives each of the six outputs, and what they are doing right now.
///
/// Rows, top to bottom: gate 1, gate 2, gate 3, gate 4, CV 1, CV 2.
///   pads 1-4 (left)   who drives the output: Euclid (white), Chaos (purple), Turing machine (green), voice tracker
///                     (pink). The current owner is bright. Press a pad to hand the output to that feature. Turing and
///                     voice use a CV and gate pair, so they only exist on channel 1 and 2.
///   pads 6-16         a live meter: for a gate it lights fully while the gate is high (and half for a moment after);
///                     for a CV it is a bar showing the voltage, 0 to 10 V
///   bottom row        key to the four owner colours
///
/// Entered with SHIFT + CV from the Kit/clip view, the session view (so it works from a blank song) or the Euclidean
/// view. BACK or SHIFT + CV returns to where it was opened from. It needs no clip. From here KIT opens the Euclidean
/// view (turning the current clip into a Kit if needed), and, when there is a clip, CV opens the chaos view,
/// KEYBOARD the Turing machine and MIDI the voice tracker.
class EurorackView final : public UI {
public:
	EurorackView() = default;

	/// Where BACK goes. Set this just before changeRootUI(&eurorackView).
	void setReturnTo(UI* ui) { returnTo_ = ui; }

	bool opened() override;
	void focusRegained() override;
	UIType getUIType() override { return UIType::EURORACK; }

	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true) override;
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;
	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;
	void graphicsRoutine() override;

	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;

private:
	void showNotice(char const* text);
	uint32_t lastSignature_ = 0xFFFFFFFF;
	UI* returnTo_ = nullptr;
};

extern EurorackView eurorackView;
