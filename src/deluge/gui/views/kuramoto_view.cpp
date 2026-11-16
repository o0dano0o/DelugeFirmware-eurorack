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

#include "gui/views/kuramoto_view.h"
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
#include "processing/engines/kuramoto_runner.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace deluge;
using namespace gui;
using namespace deluge::kuramoto;

KuramotoView kuramotoView{};

namespace {

constexpr int32_t kPopupMs = 800;
constexpr int32_t kFlashMs = 120;
constexpr int32_t kNumPages = 3;
constexpr int32_t kTopRow = kDisplayHeight - 1; ///< oscillator 1's row
constexpr int32_t kOrderRow = kDisplayHeight - 1 - kNumOscillators;
constexpr int32_t kMeanRow = kOrderRow - 1;

const RGB kDotColour{240, 230, 50};
const RGB kFlashColour{255, 255, 255};
const RGB kOrderColour{255, 140, 40};
const RGB kMeanColour{40, 220, 200};
const RGB kCouplingColour{255, 200, 40};
const RGB kGateColour{40, 160, 255};

char const* const kPageNames[kNumPages] = {"Rates 1 and 2", "Rates 3 and 4", "Range and preset"};
char const* const kPageNamesShort[kNumPages] = {"r12", "r34", "rP"};
char const kLetters[kNumOscillators] = {'A', 'b', 'C', 'd'};

float clamp01(float v) {
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

int32_t percent(float v) {
	return static_cast<int32_t>(v * 100.0f + 0.5f);
}

RGB scaled(RGB c, int32_t percentage) {
	return c.transform([percentage](uint8_t v) { return static_cast<uint8_t>(v * percentage / 100); });
}

} // namespace

bool KuramotoView::opened() {
	focusRegained();
	return true;
}

void KuramotoView::focusRegained() {
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
		display->setScrollingText(runner::isActive() ? "KURAMOTO" : "KURAMOTO OFF  CV TO START");
	}
	uiNeedsRendering(this);
}

void KuramotoView::graphicsRoutine() {
	const KuramotoEngine& e = runner::engine();
	uint32_t signature = 0;
	for (int32_t i = 0; i < kNumOscillators; i++) {
		signature = signature * 31 + static_cast<uint32_t>(e.getPhase(i) * kDisplayWidth);
		signature = signature * 3 + (runner::gateRecentlyHigh(i, kFlashMs) ? 1 : 0) + (runner::isGateKuramoto(i) ? 1 : 0);
	}
	signature = signature * 31 + static_cast<uint32_t>(e.getOrder() * kDisplayWidth);
	signature = signature * 31 + static_cast<uint32_t>(e.getMeanPhase() * kDisplayWidth);
	if (signature != lastSignature_) {
		lastSignature_ = signature;
		uiNeedsRendering(this);
	}
}

bool KuramotoView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
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

	const KuramotoEngine& e = runner::engine();

	for (int32_t i = 0; i < kNumOscillators; i++) {
		int32_t y = kTopRow - i;
		if (!(whichRows & (1 << y))) {
			continue;
		}
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			image[y][x] = RGB::monochrome(3);
		}
		int32_t head = std::min(kDisplayWidth - 1, static_cast<int32_t>(e.getPhase(i) * kDisplayWidth));
		bool fired = runner::gateRecentlyHigh(i, kFlashMs);
		// a short trail behind the dot, wrapping round the row
		for (int32_t t = 2; t >= 1; t--) {
			int32_t x = (head - t + kDisplayWidth) % kDisplayWidth;
			image[y][x] = scaled(kDotColour, t == 1 ? 35 : 15);
		}
		image[y][head] = fired ? kFlashColour : kDotColour;
	}

	if (whichRows & (1 << kOrderRow)) {
		int32_t lit = static_cast<int32_t>(clamp01(e.getOrder()) * kDisplayWidth + 0.5f);
		RGB dim = scaled(kOrderColour, 8);
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			image[kOrderRow][x] = (x < lit) ? kOrderColour : dim;
		}
	}
	if (whichRows & (1 << kMeanRow)) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			image[kMeanRow][x] = scaled(kMeanColour, 8);
		}
		int32_t head = std::min(kDisplayWidth - 1, static_cast<int32_t>(e.getMeanPhase() * kDisplayWidth));
		image[kMeanRow][head] = kMeanColour;
	}

	PadLEDs::renderingLock = false;
	return true;
}

bool KuramotoView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                 uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}
	int32_t lit = static_cast<int32_t>(runner::engine().params().coupling * kDisplayHeight + 0.5f);
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (!(whichRows & (1 << y))) {
			continue;
		}
		image[y][kDisplayWidth] = (y < lit) ? kCouplingColour : RGB::monochrome(8);
		if (kSideBarWidth > 1) {
			// gates 1 to 4 from the top
			int32_t gate = kDisplayHeight - 1 - y;
			RGB c = colours::black;
			if (gate >= 0 && gate < kNumGates) {
				bool owned = runner::isGateKuramoto(gate);
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

void KuramotoView::pageValueText(int32_t knob, char* out, size_t size) const {
	const Params& p = runner::engine().params();
	// The 7-segment display shows four characters, so every value there is a short tag plus the number:
	// A, b, C, d an oscillator's rate in Hz, r the range in volts, P the rate preset
	bool oled = display->haveOLED();
	auto rateText = [&](int32_t n) {
		int32_t tenths = static_cast<int32_t>(p.rate[n] * 10.0f + 0.5f);
		if (oled) {
			snprintf(out, size, "Oscillator %d rate %d.%d Hz", static_cast<int>(n + 1), static_cast<int>(tenths / 10),
			         static_cast<int>(tenths % 10));
		}
		else if (tenths >= 100) {
			snprintf(out, size, "%c%2d", kLetters[n], static_cast<int>(tenths / 10));
		}
		else {
			snprintf(out, size, "%c%d.%d", kLetters[n], static_cast<int>(tenths / 10), static_cast<int>(tenths % 10));
		}
	};
	switch (page_) {
	case 0:
		rateText(knob);
		break;
	case 1:
		rateText(2 + knob);
		break;
	default:
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
		else if (oled) {
			snprintf(out, size, "Rates: %s", KuramotoEngine::presetName(preset_));
		}
		else {
			snprintf(out, size, "P%d", static_cast<int>(preset_ + 1));
		}
		break;
	}
}

void KuramotoView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 6;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 1;
#endif
	canvas.drawStringCentred("Kuramoto", yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	const Params& p = runner::engine().params();
	char text[64];
	if (!runner::isActive()) {
		snprintf(text, sizeof(text), "off");
	}
	else {
		snprintf(text, sizeof(text), "Coupling %d%%  Noise %d%%", static_cast<int>(percent(p.coupling)),
		         static_cast<int>(percent(p.noise)));
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

void KuramotoView::refreshOLED() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	uiNeedsRendering(this);
}

void KuramotoView::showValuePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->displayPopup(text, 0, true);
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kPopupMs);
	}
}

void KuramotoView::showNamePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->setScrollingText(text);
	}
}

ActionResult KuramotoView::timerCallback() {
	display->cancelPopup();
	return ActionResult::DEALT_WITH;
}

void KuramotoView::showCoupling() {
	char text[24];
	snprintf(text, sizeof(text), display->haveOLED() ? "Coupling %d%%" : "c%d",
	         static_cast<int>(percent(runner::engine().params().coupling)));
	showValuePopup(text);
}

void KuramotoView::showNoise() {
	char text[24];
	snprintf(text, sizeof(text), display->haveOLED() ? "Noise %d%%" : "n%d",
	         static_cast<int>(percent(runner::engine().params().noise)));
	showValuePopup(text);
}

ActionResult KuramotoView::horizontalEncoderAction(int32_t offset) {
	auto& e = runner::engine();
	e.setCoupling(e.params().coupling + 0.01f * offset);
	showCoupling();
	refreshOLED();
	return ActionResult::DEALT_WITH;
}

ActionResult KuramotoView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	auto& e = runner::engine();
	e.setNoise(e.params().noise + 0.02f * (offset > 0 ? 1 : -1));
	showNoise();
	refreshOLED();
	return ActionResult::DEALT_WITH;
}

void KuramotoView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (whichModEncoder < 0 || whichModEncoder > 1) {
		return;
	}
	auto& e = runner::engine();
	const Params& p = e.params();
	int32_t dir = offset > 0 ? 1 : -1;
	// the lower gold knob is the first setting on a page, the upper gold knob the second
	int32_t knob = (whichModEncoder == 1) ? 1 : 0;
	switch (page_) {
	case 0:
	case 1: {
		int32_t n = page_ * 2 + knob;
		e.setRate(n, p.rate[n] * std::pow(1.06f, static_cast<float>(dir)));
		break;
	}
	default:
		if (knob == 0) {
			e.setRange(p.rangeVolts + 0.25f * dir);
		}
		else {
			preset_ = (preset_ + dir + kNumPresets) % kNumPresets;
			e.applyPreset(preset_);
		}
		break;
	}
	char text[48];
	pageValueText(knob, text, sizeof(text));
	showValuePopup(text);
	refreshOLED();
}

void KuramotoView::selectEncoderAction(int8_t offset) {
	if (Buttons::isShiftButtonPressed()) {
		// clockwise is tempo sync, anticlockwise is free time, so extra clicks cannot flip it back
		bool tempo = offset > 0;
		runner::setTempoSync(tempo);
		showNamePopup(tempo ? (display->haveOLED() ? "Tempo sync" : "SYNC") : (display->haveOLED() ? "Free time" : "FREE"));
		refreshOLED();
		return;
	}
	page_ = (page_ + (offset > 0 ? 1 : kNumPages - 1)) % kNumPages;
	char text[32];
	snprintf(text, sizeof(text), "%s", display->haveOLED() ? kPageNames[page_] : kPageNamesShort[page_]);
	showNamePopup(text);
	refreshOLED();
}

ActionResult KuramotoView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
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
				runner::engine().randomiseRates();
				showNamePopup(display->haveOLED() ? "Random rates" : "rAnd");
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
			showNamePopup(!enable ? "Kuramoto off" : (runner::isActive() ? "Kuramoto on" : "No free outputs"));
			refreshOLED();
		}
		return ActionResult::DEALT_WITH;
	}

	return ClipView::buttonAction(b, on, inCardRoutine);
}

ActionResult KuramotoView::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (!velocity) {
		return ActionResult::DEALT_WITH;
	}
	auto& e = runner::engine();

	// the coupling column: the pad you press becomes the top of the bar, and tapping the top lit pad again zeroes it
	if (x == kDisplayWidth && y >= 0 && y < kDisplayHeight) {
		int32_t lit = static_cast<int32_t>(e.params().coupling * kDisplayHeight + 0.5f);
		e.setCoupling((lit == y + 1) ? 0.0f : static_cast<float>(y + 1) / kDisplayHeight);
		showCoupling();
		refreshOLED();
	}
	return ActionResult::DEALT_WITH;
}
