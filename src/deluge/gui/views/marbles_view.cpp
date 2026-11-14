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

#include "gui/views/marbles_view.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/eurorack_view.h"
#include "gui/views/euclidean_view.h"
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
#include "processing/engines/marbles_runner.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace deluge;
using namespace gui;
using namespace deluge::marbles;

MarblesView marblesView{};

namespace {

constexpr int32_t kPopupMs = 800;
constexpr int32_t kFlashMs = 120;
constexpr int32_t kBandRows = kDisplayHeight / 2; ///< each voltage gets four rows
constexpr int32_t kNumPages = 4;
constexpr int32_t kBarLevels = kBandRows;         ///< a bar is 1 to 4 pads tall

const RGB kX1Colour{40, 160, 255};
const RGB kX2Colour{255, 140, 40};
const RGB kCapColour{255, 255, 255};
const RGB kDejaVuColour{180, 120, 255};
const RGB kGateColour{40, 160, 255};

char const* const kPageNames[kNumPages] = {"Voltage", "Shape", "Clock", "Gates"};

float clamp01(float v) {
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

int32_t percent(float v) {
	return static_cast<int32_t>(v * 100.0f + 0.5f);
}

/// 1 to 4 pads for a value of 0 to 1
int32_t barHeight(float value) {
	return 1 + static_cast<int32_t>(clamp01(value) * (kBarLevels - 1) + 0.5f);
}

} // namespace

bool MarblesView::opened() {
	focusRegained();
	return true;
}

void MarblesView::focusRegained() {
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
		display->setScrollingText(runner::isActive() ? "MARBLES" : "MARBLES OFF  CV TO START");
	}
	uiNeedsRendering(this);
}

void MarblesView::graphicsRoutine() {
	uint32_t signature = runner::getStepCount();
	for (int32_t g = 0; g < kNumGates; g++) {
		signature = signature * 3 + (runner::gateRecentlyHigh(g, kFlashMs) ? 1 : 0) + (runner::isGateMarbles(g) ? 1 : 0);
	}
	if (signature != lastSignature_) {
		lastSignature_ = signature;
		uiNeedsRendering(this);
	}
}

bool MarblesView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
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

	const MarblesEngine& e = runner::engine();
	bool started = e.getClockCount() > 0;
	for (int32_t cv = 0; cv < kNumCV; cv++) {
		int32_t base = (cv == 0) ? kBandRows : 0; // X1 in the top band, X2 in the bottom
		RGB colour = (cv == 0) ? kX1Colour : kX2Colour;
		RGB dim = colour.transform([](uint8_t v) { return v / 4; });
		int32_t length = e.params().length[cv];
		int32_t pos = e.getPosition(cv);
		for (int32_t x = 0; x < length && x < kDisplayWidth; x++) {
			int32_t height = barHeight(e.getLoopValue(cv, x));
			bool playing = started && x == pos;
			for (int32_t k = 0; k < height; k++) {
				int32_t y = base + k;
				if (!(whichRows & (1 << y))) {
					continue;
				}
				bool cap = (k == height - 1);
				if (playing && cap) {
					image[y][x] = kCapColour;
				}
				else {
					image[y][x] = (playing || cap) ? colour : dim;
				}
			}
		}
	}

	PadLEDs::renderingLock = false;
	return true;
}

bool MarblesView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}
	int32_t lit = static_cast<int32_t>(runner::engine().params().dejaVu * kDisplayHeight + 0.5f);
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (!(whichRows & (1 << y))) {
			continue;
		}
		image[y][kDisplayWidth] = (y < lit) ? kDejaVuColour : RGB::monochrome(8);
		if (kSideBarWidth > 1) {
			// gates 1 to 4 from the top
			int32_t gate = kDisplayHeight - 1 - y;
			RGB c = colours::black;
			if (gate >= 0 && gate < kNumGates) {
				bool owned = runner::isGateMarbles(gate);
				if (runner::gateRecentlyHigh(gate, kFlashMs)) {
					c = owned ? kGateColour : kGateColour.transform([](uint8_t v) { return v / 6; });
				}
				else {
					c = owned ? kGateColour.transform([](uint8_t v) { return v / 10; }) : RGB::monochrome(4);
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

void MarblesView::pageValueText(int32_t knob, char* out, size_t size) const {
	const Params& p = runner::engine().params();
	switch (page_) {
	case 0:
		if (knob == 0) {
			snprintf(out, size, "Spread %d%%", static_cast<int>(percent(p.spread)));
		}
		else {
			snprintf(out, size, "Bias %d%%", static_cast<int>(percent(p.bias)));
		}
		break;
	case 1:
		if (knob == 0) {
			snprintf(out, size, "Steps %d%%", static_cast<int>(percent(p.steps)));
		}
		else {
			snprintf(out, size, "Range %d.%d V", static_cast<int>(p.rangeVolts), static_cast<int>(p.rangeVolts * 10.0f) % 10);
		}
		break;
	case 2:
		if (knob == 0) {
			if (p.timeMode == TimeMode::TEMPO) {
				snprintf(out, size, "Rate %s", rateName(p.rate));
			}
			else {
				int32_t tenthsHz = static_cast<int32_t>(10.0f / p.freePeriod + 0.5f);
				snprintf(out, size, "Rate %d.%d Hz", static_cast<int>(tenthsHz / 10), static_cast<int>(tenthsHz % 10));
			}
		}
		else {
			snprintf(out, size, "Jitter %d%%", static_cast<int>(percent(p.jitter)));
		}
		break;
	default:
		if (knob == 0) {
			snprintf(out, size, "Bernoulli %d%%", static_cast<int>(percent(p.bernoulli)));
		}
		else {
			snprintf(out, size, "Gate 4 every %d", static_cast<int>(p.gateDivision));
		}
		break;
	}
}

void MarblesView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 6;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 1;
#endif
	canvas.drawStringCentred("Marbles", yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	const Params& p = runner::engine().params();
	char text[48];
	if (!runner::isActive()) {
		snprintf(text, sizeof(text), "off");
	}
	else {
		snprintf(text, sizeof(text), "Deja vu %d%%  X1 %d  X2 %d", static_cast<int>(percent(p.dejaVu)),
		         static_cast<int>(p.length[0]), static_cast<int>(p.length[1]));
	}
	canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	char a[24], b[24];
	pageValueText(0, a, sizeof(a));
	pageValueText(1, b, sizeof(b));
	snprintf(text, sizeof(text), "%s: %s, %s", kPageNames[page_], a, b);
	canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
}

void MarblesView::refreshOLED() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	uiNeedsRendering(this);
}

void MarblesView::showValuePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->displayPopup(text, 0, true);
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kPopupMs);
	}
}

void MarblesView::showNamePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->setScrollingText(text);
	}
}

ActionResult MarblesView::timerCallback() {
	display->cancelPopup();
	return ActionResult::DEALT_WITH;
}

ActionResult MarblesView::horizontalEncoderAction(int32_t offset) {
	auto& e = runner::engine();
	e.setDejaVu(e.params().dejaVu + 0.01f * offset);
	char text[24];
	snprintf(text, sizeof(text), display->haveOLED() ? "Deja vu %d%%" : "DV%d", static_cast<int>(percent(e.params().dejaVu)));
	showValuePopup(text);
	refreshOLED();
	return ActionResult::DEALT_WITH;
}

ActionResult MarblesView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	auto& e = runner::engine();
	int32_t cv = Buttons::isShiftButtonPressed() ? 1 : 0;
	e.setLength(cv, e.params().length[cv] + (offset > 0 ? 1 : -1));
	char text[24];
	snprintf(text, sizeof(text), display->haveOLED() ? "X%d length %d" : "X%dL%d", static_cast<int>(cv + 1),
	         static_cast<int>(e.params().length[cv]));
	showValuePopup(text);
	refreshOLED();
	return ActionResult::DEALT_WITH;
}

void MarblesView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (whichModEncoder < 0 || whichModEncoder > 1) {
		return;
	}
	auto& e = runner::engine();
	const Params& p = e.params();
	int32_t dir = offset > 0 ? 1 : -1;
	bool upper = (whichModEncoder == 1); // the upper gold knob is the second setting on a page
	// knob 0 of a page is the lower gold knob, knob 1 the upper
	int32_t knob = upper ? 1 : 0;
	switch (page_) {
	case 0:
		if (knob == 0) {
			e.setSpread(p.spread + 0.02f * dir);
		}
		else {
			e.setBias(p.bias + 0.02f * dir);
		}
		break;
	case 1:
		if (knob == 0) {
			e.setSteps(p.steps + 0.02f * dir);
		}
		else {
			e.setRange(p.rangeVolts + 0.25f * dir);
		}
		break;
	case 2:
		if (knob == 0) {
			if (p.timeMode == TimeMode::TEMPO) {
				e.setRate(p.rate + dir);
			}
			else {
				// clockwise is faster: a shorter period, 6% per click
				e.setFreePeriod(p.freePeriod * std::pow(1.06f, static_cast<float>(-dir)));
			}
		}
		else {
			e.setJitter(p.jitter + 0.02f * dir);
		}
		break;
	default:
		if (knob == 0) {
			e.setBernoulli(p.bernoulli + 0.02f * dir);
		}
		else {
			e.setGateDivision(p.gateDivision + dir);
		}
		break;
	}
	char text[32];
	pageValueText(knob, text, sizeof(text));
	showValuePopup(text);
	refreshOLED();
}

void MarblesView::selectEncoderAction(int8_t offset) {
	if (Buttons::isShiftButtonPressed()) {
		bool tempo = !runner::isTempoSync();
		runner::setTempoSync(tempo);
		showNamePopup(tempo ? (display->haveOLED() ? "Tempo sync" : "SYNC") : (display->haveOLED() ? "Free time" : "FREE"));
		refreshOLED();
		return;
	}
	page_ = (page_ + (offset > 0 ? 1 : kNumPages - 1)) % kNumPages;
	char text[32];
	snprintf(text, sizeof(text), display->haveOLED() ? "Page: %s" : "%s", kPageNames[page_]);
	showNamePopup(text);
	refreshOLED();
}

ActionResult MarblesView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
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
				runner::engine().reset();
				showNamePopup("Restarted");
			}
			else {
				runner::engine().randomise();
				showNamePopup("New loop");
			}
			refreshOLED();
		}
		return ActionResult::DEALT_WITH;
	}

	if (b == CV) {
		if (on) {
			bool enable = !runner::isActive();
			runner::setAll(enable);
			showNamePopup(!enable ? "Marbles off" : (runner::isActive() ? "Marbles on" : "No free outputs"));
			refreshOLED();
		}
		return ActionResult::DEALT_WITH;
	}

	return ClipView::buttonAction(b, on, inCardRoutine);
}

ActionResult MarblesView::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (!velocity) {
		return ActionResult::DEALT_WITH;
	}
	auto& e = runner::engine();

	// the déjà vu column: the pad you press becomes the top of the bar, and tapping the top lit pad again zeroes it
	if (x == kDisplayWidth && y >= 0 && y < kDisplayHeight) {
		int32_t lit = static_cast<int32_t>(e.params().dejaVu * kDisplayHeight + 0.5f);
		e.setDejaVu((lit == y + 1) ? 0.0f : static_cast<float>(y + 1) / kDisplayHeight);
		char text[24];
		snprintf(text, sizeof(text), display->haveOLED() ? "Deja vu %d%%" : "DV%d", static_cast<int>(percent(e.params().dejaVu)));
		showValuePopup(text);
		refreshOLED();
		return ActionResult::DEALT_WITH;
	}

	// tap a bar to set that loop step's height
	if (x >= 0 && x < kDisplayWidth && y >= 0 && y < kDisplayHeight) {
		int32_t cv = (y >= kBandRows) ? 0 : 1;
		int32_t level = (y % kBandRows); // 0 to 3 from the bottom of the band
		if (x < e.params().length[cv]) {
			e.setLoopValue(cv, x, static_cast<float>(level) / (kBarLevels - 1));
			uiNeedsRendering(this);
		}
	}
	return ActionResult::DEALT_WITH;
}
