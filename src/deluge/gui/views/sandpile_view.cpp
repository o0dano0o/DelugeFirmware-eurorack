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

#include "gui/views/sandpile_view.h"
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
#include "processing/engines/audio_engine.h"
#include "processing/engines/sandpile_runner.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace deluge;
using namespace gui;
using namespace deluge::sandpile;

SandpileView sandpileView{};

namespace {

constexpr int32_t kPopupMs = 800;
constexpr int32_t kFlashMs = 120;
constexpr int32_t kNumPages = 3;
constexpr float kMinRate = 0.25f;
constexpr float kMaxRate = 20.0f;

const RGB kCellColour[kLimit] = {RGB{0, 0, 0}, RGB{10, 40, 130}, RGB{20, 170, 90}, RGB{230, 180, 20}};
const RGB kFlashColour{255, 255, 255};
const RGB kRateColour{225, 185, 105};
const RGB kGateColour{40, 160, 255};

char const* const kPageNames[kNumPages] = {"Drop mode and topple speed", "Range and big size", "CV meters"};
char const* const kPageNamesShort[kNumPages] = {"drP", "rnG", "CUr"};

RGB scaled(RGB c, int32_t pct) {
	return c.transform([pct](uint8_t v) { return static_cast<uint8_t>(v * pct / 100); });
}

/// The rate bar is on a log scale so each pad is the same ratio
int32_t litForRate(float rate) {
	float f = std::log(rate / kMinRate) / std::log(kMaxRate / kMinRate);
	return static_cast<int32_t>(f * kDisplayHeight + 0.5f);
}

} // namespace

bool SandpileView::opened() {
	focusRegained();
	return true;
}

void SandpileView::focusRegained() {
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
		display->setScrollingText(runner::isActive() ? "SANDPILE" : "SANDPILE OFF  CV TO START");
	}
	uiNeedsRendering(this);
}

void SandpileView::graphicsRoutine() {
	const SandpileEngine& e = runner::engine();
	uint32_t signature = e.getTotalGrains();
	signature = signature * 31 + static_cast<uint32_t>(e.getVolts(0) * 100.0f) + page_;
	signature = signature * 31 + e.getTotalTopples();
	signature = signature * 31 + e.getTotalDropped();
	for (int32_t g = 0; g < kNumGates; g++) {
		signature = signature * 3 + (runner::gateRecentlyHigh(g, kFlashMs) ? 1 : 0) + (runner::isGateSandpile(g) ? 1 : 0);
	}
	if (signature != lastSignature_) {
		lastSignature_ = signature;
		uiNeedsRendering(this);
	}
}

bool SandpileView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                  uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	if (!image) {
		return true;
	}
	PadLEDs::renderingLock = true;

	const SandpileEngine& e = runner::engine();
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (!(whichRows & (1 << y))) {
			continue;
		}
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			int32_t n = std::min<int32_t>(e.getCell(x, y), kLimit - 1);
			RGB c = kCellColour[n];
			if (e.getFlash(x, y) > 0.0f || e.getCell(x, y) >= kLimit) {
				c = kFlashColour;
			}
			image[y][x] = c;
		}
		if (occupancyMask) {
			memset(occupancyMask[y], 64, kDisplayWidth);
		}
	}

	// On the CV page the bottom two rows become meters: CV 1 (avalanche size) above CV 2 (fill)
	if (page_ == 2) {
		const float range = e.params().rangeVolts;
		for (int32_t cv = 0; cv < kNumCV; cv++) {
			int32_t y = 1 - cv;
			if (!(whichRows & (1 << y))) {
				continue;
			}
			int32_t lit = static_cast<int32_t>(std::min(1.0f, e.getVolts(cv) / range) * kDisplayWidth + 0.5f);
			for (int32_t x = 0; x < kDisplayWidth; x++) {
				image[y][x] = (x < lit) ? (cv == 0 ? RGB{255, 140, 40} : RGB{40, 220, 200}) : RGB::monochrome(6);
			}
		}
	}

	PadLEDs::renderingLock = false;
	return true;
}

bool SandpileView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                 uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}
	int32_t lit = litForRate(runner::engine().params().dropRate);
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (!(whichRows & (1 << y))) {
			continue;
		}
		image[y][kDisplayWidth] = (y < lit) ? kRateColour : RGB::monochrome(8);
		if (kSideBarWidth > 1) {
			int32_t gate = kDisplayHeight - 1 - y;
			RGB c = colours::black;
			if (gate >= 0 && gate < kNumGates) {
				bool owned = runner::isGateSandpile(gate);
				if (runner::gateRecentlyHigh(gate, kFlashMs)) {
					c = owned ? kGateColour : scaled(kGateColour, 17);
				}
				else {
					c = owned ? scaled(kGateColour, 10) : RGB::monochrome(4);
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

void SandpileView::pageValueText(int32_t knob, char* out, size_t size) const {
	const Params& p = runner::engine().params();
	bool oled = display->haveOLED();
	if (page_ == 2) {
		snprintf(out, size, "%s", oled ? "CV meters on the bottom rows" : "CUr");
		return;
	}
	if (page_ == 0) {
		if (knob == 0) {
			bool centre = p.dropMode == DropMode::CENTRE;
			snprintf(out, size, "%s", oled ? (centre ? "Drop in the centre" : "Drop anywhere") : (centre ? "CEnt" : "rAnd"));
		}
		else {
			snprintf(out, size, oled ? "Topple speed %d" : "tP%d", static_cast<int>(p.toppleRate + 0.5f));
		}
		return;
	}
	if (knob == 0) {
		int32_t tenths = static_cast<int32_t>(p.rangeVolts * 10.0f + 0.5f);
		if (oled) {
			snprintf(out, size, "Range %d.%d V", static_cast<int>(tenths / 10), static_cast<int>(tenths % 10));
		}
		else if (tenths >= 100) {
			snprintf(out, size, "r 10");
		}
		else {
			snprintf(out, size, "r%d.%d", static_cast<int>(tenths / 10), static_cast<int>(tenths % 10));
		}
	}
	else {
		snprintf(out, size, oled ? "Big avalanche %d" : "bg%d", static_cast<int>(p.bigSize));
	}
}

void SandpileView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 6;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 1;
#endif
	canvas.drawStringCentred("Sandpile", yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	const Params& p = runner::engine().params();
	char text[64];
	if (!runner::isActive()) {
		snprintf(text, sizeof(text), "off");
	}
	else {
		int32_t tenths = static_cast<int32_t>(p.dropRate * 10.0f + 0.5f);
		snprintf(text, sizeof(text), "Drops %d.%d/s  Big %d", static_cast<int>(tenths / 10), static_cast<int>(tenths % 10),
		         static_cast<int>(p.bigSize));
	}
	canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	char a[40], b[40];
	pageValueText(0, a, sizeof(a));
	pageValueText(1, b, sizeof(b));
	canvas.drawStringCentred(a, yPos, kTextSpacingX, kTextSpacingY);
	yPos += 10;
	canvas.drawStringCentred(b, yPos, kTextSpacingX, kTextSpacingY);
}

void SandpileView::refreshOLED() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	uiNeedsRendering(this);
}

void SandpileView::showValuePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->displayPopup(text, 0, true);
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kPopupMs);
	}
}

void SandpileView::showNamePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->setScrollingText(text);
	}
}

ActionResult SandpileView::timerCallback() {
	display->cancelPopup();
	return ActionResult::DEALT_WITH;
}

void SandpileView::showRate() {
	float v = runner::engine().params().dropRate;
	char text[24];
	int32_t tenths = static_cast<int32_t>(v * 10.0f + 0.5f);
	if (display->haveOLED()) {
		snprintf(text, sizeof(text), "Drops %d.%d/s", static_cast<int>(tenths / 10), static_cast<int>(tenths % 10));
	}
	else if (tenths >= 100) {
		snprintf(text, sizeof(text), "d%d", static_cast<int>(tenths / 10));
	}
	else {
		snprintf(text, sizeof(text), "d%d.%d", static_cast<int>(tenths / 10), static_cast<int>(tenths % 10));
	}
	showValuePopup(text);
}

void SandpileView::showBig() {
	char text[24];
	snprintf(text, sizeof(text), display->haveOLED() ? "Big avalanche %d" : "bg%d",
	         static_cast<int>(runner::engine().params().bigSize));
	showValuePopup(text);
}

ActionResult SandpileView::horizontalEncoderAction(int32_t offset) {
	auto& e = runner::engine();
	e.setDropRate(e.params().dropRate * std::pow(1.08f, static_cast<float>(offset)));
	showRate();
	refreshOLED();
	return ActionResult::DEALT_WITH;
}

ActionResult SandpileView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	auto& e = runner::engine();
	e.setBigSize(e.params().bigSize + (offset > 0 ? 1 : -1));
	showBig();
	refreshOLED();
	return ActionResult::DEALT_WITH;
}

void SandpileView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (whichModEncoder < 0 || whichModEncoder > 1) {
		return;
	}
	auto& e = runner::engine();
	const Params& p = e.params();
	int32_t dir = offset > 0 ? 1 : -1;
	// the lower gold knob is the first setting on a page, the upper gold knob the second
	int32_t knob = (whichModEncoder == 1) ? 1 : 0;
	if (page_ == 0) {
		if (knob == 0) {
			e.setDropMode(p.dropMode == DropMode::RANDOM ? DropMode::CENTRE : DropMode::RANDOM);
		}
		else {
			e.setToppleRate(p.toppleRate + 5.0f * dir);
		}
	}
	else {
		if (knob == 0) {
			e.setRange(p.rangeVolts + 0.25f * dir);
		}
		else {
			e.setBigSize(p.bigSize + dir);
		}
	}
	char text[48];
	pageValueText(knob, text, sizeof(text));
	showValuePopup(text);
	refreshOLED();
}

void SandpileView::selectEncoderAction(int8_t offset) {
	if (Buttons::isShiftButtonPressed()) {
		// clockwise is tempo sync, anticlockwise is free time, so extra clicks cannot flip it back
		bool tempo = offset > 0;
		runner::setTempoSync(tempo);
		showNamePopup(tempo ? (display->haveOLED() ? "Tempo sync" : "SYNC") : (display->haveOLED() ? "Free time" : "FREE"));
		refreshOLED();
		return;
	}
	page_ = (page_ + (offset > 0 ? 1 : kNumPages - 1)) % kNumPages;
	showNamePopup(display->haveOLED() ? kPageNames[page_] : kPageNamesShort[page_]);
	refreshOLED();
}

ActionResult SandpileView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
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
			if (Buttons::isShiftButtonPressed()) {
				runner::engine().clear();
				showNamePopup(display->haveOLED() ? "Table emptied" : "CLr");
			}
			else {
				runner::engine().reset();
				showNamePopup(display->haveOLED() ? "Restarted" : "rSt");
			}
			refreshOLED();
		}
		return ActionResult::DEALT_WITH;
	}

	if (b == CV) {
		if (on) {
			bool enable = !runner::isActive();
			runner::setAll(enable);
			showNamePopup(!enable ? "Sandpile off" : (runner::isActive() ? "Sandpile on" : "No free outputs"));
			refreshOLED();
		}
		return ActionResult::DEALT_WITH;
	}

	return ClipView::buttonAction(b, on, inCardRoutine);
}

ActionResult SandpileView::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (!velocity) {
		return ActionResult::DEALT_WITH;
	}
	auto& e = runner::engine();

	// the drop rate column: the pad you press becomes the top of the bar
	if (x == kDisplayWidth && y >= 0 && y < kDisplayHeight) {
		float rate = kMinRate * std::pow(kMaxRate / kMinRate, static_cast<float>(y + 1) / kDisplayHeight);
		e.setDropRate(rate);
		showRate();
		refreshOLED();
		return ActionResult::DEALT_WITH;
	}

	// a pad on the table adds a grain, or with SHIFT takes one off
	if (x >= 0 && x < kWidth && y >= 0 && y < kHeight) {
		if (Buttons::isShiftButtonPressed()) {
			e.removeGrain(x, y);
		}
		else {
			e.addGrain(x, y);
		}
		uiNeedsRendering(this);
	}
	return ActionResult::DEALT_WITH;
}
