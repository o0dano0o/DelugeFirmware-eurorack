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

#include "gui/views/crossmod_view.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/euclidean_view.h"
#include "gui/views/eurorack_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "model/clip/clip.h"
#include "model/song/song.h"
#include "processing/engines/crossmod_runner.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace deluge;
using namespace gui;
using namespace deluge::crossmod;

CrossmodView crossmodView{};

namespace {

constexpr int32_t kPopupMs = 800;
constexpr int32_t kTopRow = kDisplayHeight - 1; ///< route 1's source row
constexpr int32_t kTargetTopRow = kNumRoutes - 1; ///< route 1's target row

const RGB kFamilyColour[5] = {
    RGB{180, 60, 255}, // chaos
    RGB{40, 160, 255}, // Marbles
    RGB{255, 140, 40}, // Rungler
    RGB{40, 220, 200}, // Neurons
    RGB{40, 220, 70},  // Turing
};
const RGB kAdd{40, 220, 70};
const RGB kSubtract{255, 50, 50};

float clamp01(float v) {
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

RGB scaled(RGB c, int32_t percent) {
	return c.transform([percent](uint8_t v) { return static_cast<uint8_t>(v * percent / 100); });
}

} // namespace

bool CrossmodView::opened() {
	focusRegained();
	return true;
}

void CrossmodView::focusRegained() {
	ClipView::focusRegained();

	// The save and load browsers blink these; whoever they return to has to switch them off
	indicator_leds::setLedState(IndicatorLED::SAVE, false);
	indicator_leds::setLedState(IndicatorLED::LOAD, false);
	indicator_leds::setLedState(IndicatorLED::BACK, false);

	Clip* clip = getCurrentClip();
	if (clip) {
		view.focusRegained();
		view.setActiveModControllableTimelineCounter(clip);
	}
	PadLEDs::clearTickSquares();
	lastSignature_ = 0xFFFFFFFF;

	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		display->setScrollingText("CROSS MOD");
	}
	uiNeedsRendering(this);
}

void CrossmodView::graphicsRoutine() {
	auto& e = runner::engine();
	uint32_t signature = static_cast<uint32_t>(route_);
	for (int32_t i = 0; i < kNumRoutes; i++) {
		const Route& r = e.route(i);
		signature = signature * 31 + static_cast<uint32_t>(r.source + 1);
		signature = signature * 31 + static_cast<uint32_t>(r.target + 1);
		signature = signature * 41 + static_cast<uint32_t>(r.depth + kMaxDepthSteps);
		if (r.source >= 0) {
			signature = signature * 31 + static_cast<uint32_t>(runner::sourceValue(r.source) * 16.0f);
		}
	}
	if (signature != lastSignature_) {
		lastSignature_ = signature;
		uiNeedsRendering(this);
	}
}

bool CrossmodView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                  uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	if (!image) {
		return true;
	}
	PadLEDs::renderingLock = true;

	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (!(whichRows & (1 << y))) {
			continue;
		}
		std::fill(image[y], image[y] + kDisplayWidth, colours::black);
		if (occupancyMask) {
			memset(occupancyMask[y], 64, kDisplayWidth);
		}
	}

	auto& e = runner::engine();
	for (int32_t i = 0; i < kNumRoutes; i++) {
		const Route& r = e.route(i);
		bool selected = (i == route_);
		int32_t dimPercent = selected ? 14 : 6;

		int32_t sourceY = kTopRow - i;
		if (whichRows & (1 << sourceY)) {
			for (int32_t x = 0; x < runner::kNumSources; x++) {
				RGB c = scaled(kFamilyColour[runner::sourceFamily(x)], dimPercent);
				if (x == r.source) {
					int32_t live = static_cast<int32_t>(30.0f + 70.0f * clamp01(runner::sourceValue(x)));
					c = scaled(kFamilyColour[runner::sourceFamily(x)], selected ? live : live / 2);
				}
				image[sourceY][x] = c;
			}
		}
		int32_t targetY = kTargetTopRow - i;
		if (whichRows & (1 << targetY)) {
			for (int32_t x = 0; x < runner::kNumTargets; x++) {
				RGB c = scaled(kFamilyColour[runner::targetFamily(x)], dimPercent);
				if (x == r.target) {
					c = scaled(kFamilyColour[runner::targetFamily(x)], selected ? 100 : 50);
				}
				image[targetY][x] = c;
			}
		}
	}

	PadLEDs::renderingLock = false;
	return true;
}

bool CrossmodView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                 uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}
	auto& e = runner::engine();
	int32_t depth = e.route(route_).depth;
	int32_t lit = (std::abs(depth) * kDisplayHeight + kMaxDepthSteps / 2) / kMaxDepthSteps;
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (!(whichRows & (1 << y))) {
			continue;
		}
		image[y][kDisplayWidth] = (y < lit) ? (depth > 0 ? kAdd : kSubtract) : RGB::monochrome(8);
		if (kSideBarWidth > 1) {
			RGB c = colours::black;
			int32_t i = kTopRow - y;
			if (i >= 0 && i < kNumRoutes) {
				float shift = e.lastShift(i);
				int32_t strength = static_cast<int32_t>(std::min(1.0f, std::fabs(shift)) * 90.0f);
				if (e.route(i).source >= 0 && e.route(i).target >= 0 && e.route(i).depth != 0) {
					c = scaled(shift >= 0.0f ? kAdd : kSubtract, 8 + strength);
				}
				else {
					c = RGB::monochrome(3);
				}
			}
			image[y][kDisplayWidth + 1] = c;
		}
		if (occupancyMask) {
			memset(occupancyMask[y] + kDisplayWidth, 0, kSideBarWidth);
		}
	}
	return true;
}

void CrossmodView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 6;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 1;
#endif
	char text[64];
	snprintf(text, sizeof(text), "Cross-mod route %d", static_cast<int>(route_ + 1));
	canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	const Route& r = runner::engine().route(route_);
	canvas.drawStringCentred(runner::sourceName(r.source), yPos, kTextSpacingX, kTextSpacingY);
	yPos += 10;
	canvas.drawStringCentred(runner::targetName(r.target), yPos, kTextSpacingX, kTextSpacingY);
	yPos += 10;
	snprintf(text, sizeof(text), "Depth %d%%", static_cast<int>(r.depth * 5));
	canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
}

void CrossmodView::refreshOLED() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	uiNeedsRendering(this);
}

void CrossmodView::showValuePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->displayPopup(text, 0, true);
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kPopupMs);
	}
}

void CrossmodView::showNamePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->setScrollingText(text);
	}
}

ActionResult CrossmodView::timerCallback() {
	display->cancelPopup();
	return ActionResult::DEALT_WITH;
}

void CrossmodView::showDepth() {
	int32_t percent = runner::engine().route(route_).depth * 5;
	char text[24];
	if (display->haveOLED()) {
		snprintf(text, sizeof(text), "Depth %d%%", static_cast<int>(percent));
	}
	else if (percent >= 100) {
		snprintf(text, sizeof(text), "d100");
	}
	else if (percent <= -100) {
		snprintf(text, sizeof(text), "-100");
	}
	else {
		snprintf(text, sizeof(text), "d%+d", static_cast<int>(percent));
	}
	showValuePopup(text);
}

void CrossmodView::showSource() {
	const Route& r = runner::engine().route(route_);
	char text[48];
	if (display->haveOLED()) {
		snprintf(text, sizeof(text), "Route %d from %s", static_cast<int>(route_ + 1), runner::sourceName(r.source));
	}
	else {
		snprintf(text, sizeof(text), "%s", runner::sourceTag(r.source));
	}
	showValuePopup(text);
}

void CrossmodView::showTarget() {
	const Route& r = runner::engine().route(route_);
	char text[48];
	if (display->haveOLED()) {
		snprintf(text, sizeof(text), "Route %d to %s", static_cast<int>(route_ + 1), runner::targetName(r.target));
	}
	else {
		snprintf(text, sizeof(text), "%s", runner::targetTag(r.target));
	}
	showValuePopup(text);
}

void CrossmodView::changeDepth(int32_t steps) {
	auto& e = runner::engine();
	e.setDepth(route_, e.route(route_).depth + steps);
	showDepth();
	refreshOLED();
}

ActionResult CrossmodView::horizontalEncoderAction(int32_t offset) {
	changeDepth(offset > 0 ? 1 : -1);
	return ActionResult::DEALT_WITH;
}

ActionResult CrossmodView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	changeDepth(offset > 0 ? 5 : -5);
	return ActionResult::DEALT_WITH;
}

void CrossmodView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (whichModEncoder < 0 || whichModEncoder > 1) {
		return;
	}
	auto& e = runner::engine();
	int32_t dir = offset > 0 ? 1 : -1;
	const Route& r = e.route(route_);
	if (whichModEncoder == 0) {
		// lower gold knob: source, with "none" one step past either end
		int32_t next = r.source + dir;
		if (next >= runner::kNumSources) {
			next = -1;
		}
		else if (next < -1) {
			next = runner::kNumSources - 1;
		}
		e.setSource(route_, next);
		showSource();
	}
	else {
		int32_t next = r.target + dir;
		if (next >= runner::kNumTargets) {
			next = -1;
		}
		else if (next < -1) {
			next = runner::kNumTargets - 1;
		}
		e.setTarget(route_, next);
		showTarget();
	}
	refreshOLED();
}

void CrossmodView::selectEncoderAction(int8_t offset) {
	route_ = (route_ + (offset > 0 ? 1 : kNumRoutes - 1)) % kNumRoutes;
	char text[24];
	snprintf(text, sizeof(text), display->haveOLED() ? "Route %d" : "r%d", static_cast<int>(route_ + 1));
	showNamePopup(text);
	refreshOLED();
}

ActionResult CrossmodView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	if (b == BACK) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			display->cancelPopup();
			changeRootUI(eurorackView.takeBackTarget(&euclideanView));
		}
		return ActionResult::DEALT_WITH;
	}

	if (b == SELECT_ENC) {
		if (on) {
			runner::engine().clearRoute(route_);
			showNamePopup(display->haveOLED() ? "Route cleared" : "CLr");
			refreshOLED();
		}
		return ActionResult::DEALT_WITH;
	}

	return ClipView::buttonAction(b, on, inCardRoutine);
}

ActionResult CrossmodView::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (!velocity) {
		return ActionResult::DEALT_WITH;
	}
	auto& e = runner::engine();

	// the depth column: the pad you press sets the size, tapping the top lit pad zeroes it; the sign stays
	if (x == kDisplayWidth && y >= 0 && y < kDisplayHeight) {
		int32_t depth = e.route(route_).depth;
		int32_t lit = (std::abs(depth) * kDisplayHeight + kMaxDepthSteps / 2) / kMaxDepthSteps;
		int32_t magnitude = ((y + 1) * kMaxDepthSteps) / kDisplayHeight;
		int32_t sign = depth < 0 ? -1 : 1;
		e.setDepth(route_, (lit == y + 1) ? 0 : sign * magnitude);
		showDepth();
		refreshOLED();
		return ActionResult::DEALT_WITH;
	}

	// the right-hand column is a read-out
	if (x < 0 || x >= kDisplayWidth) {
		return ActionResult::DEALT_WITH;
	}

	if (y >= kDisplayHeight - kNumRoutes) {
		int32_t i = kTopRow - y;
		if (x < runner::kNumSources) {
			route_ = i;
			e.setSource(i, e.route(i).source == x ? -1 : x);
			showSource();
			refreshOLED();
		}
	}
	else if (y < kNumRoutes) {
		int32_t i = kTargetTopRow - y;
		if (x < runner::kNumTargets) {
			route_ = i;
			e.setTarget(i, e.route(i).target == x ? -1 : x);
			showTarget();
			refreshOLED();
		}
	}
	return ActionResult::DEALT_WITH;
}
