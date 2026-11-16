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

#include "gui/views/rungler_view.h"
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
#include "processing/engines/rungler_runner.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace deluge;
using namespace gui;
using namespace deluge::rungler;

RunglerView runglerView{};

namespace {

constexpr int32_t kPopupMs = 800;
constexpr int32_t kFlashMs = 120;
constexpr int32_t kNumPages = 2;

// rows, with y = 7 the top row of the grid
constexpr int32_t kRegisterRow = 7;
constexpr int32_t kPhaseARow = 5;
constexpr int32_t kPhaseBRow = 4;
constexpr int32_t kCV1Row = 2;
constexpr int32_t kCV2Row = 1;

const RGB kRegisterOn{40, 220, 90};
const RGB kPhaseAColour{40, 160, 255};
const RGB kPhaseBColour{255, 140, 40};
const RGB kCV1Colour{200, 60, 255};
const RGB kCV2Colour{255, 60, 120};
const RGB kChanceColour{255, 200, 40};
const RGB kGateColour{40, 220, 90};

char const* const kPageNames[kNumPages] = {"Oscillators", "Outputs"};
char const* const kPageNamesShort[kNumPages] = {"OSC", "OUt"};
char const* const kRateShort[marbles::kNumRates] = {"1", "2", "4", "8", "8t", "16", "16t", "32"};

float clamp01(float v) {
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

int32_t percent(float v) {
	return static_cast<int32_t>(v * 100.0f + 0.5f);
}

} // namespace

bool RunglerView::opened() {
	focusRegained();
	return true;
}

void RunglerView::focusRegained() {
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
		display->setScrollingText(runner::isActive() ? "RUNGLER" : "RUNGLER OFF  CV TO START");
	}
	uiNeedsRendering(this);
}

void RunglerView::graphicsRoutine() {
	const RunglerEngine& e = runner::engine();
	uint32_t signature = e.getRegister();
	signature = signature * 31 + static_cast<uint32_t>(e.getPhaseA() * 16.0f);
	signature = signature * 31 + static_cast<uint32_t>(e.getPhaseB() * 16.0f);
	signature = signature * 31 + static_cast<uint32_t>(e.getVolts(0) * 4.0f);
	signature = signature * 31 + static_cast<uint32_t>(e.getVolts(1) * 4.0f);
	for (int32_t g = 0; g < kNumGates; g++) {
		signature = signature * 3 + (runner::gateRecentlyHigh(g, kFlashMs) ? 1 : 0) + (runner::isGateRungler(g) ? 1 : 0);
	}
	if (signature != lastSignature_) {
		lastSignature_ = signature;
		uiNeedsRendering(this);
	}
}

bool RunglerView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
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

	const RunglerEngine& e = runner::engine();
	const float range = e.params().rangeVolts;

	// the register: leftmost pad is bit 7 (the oldest), rightmost is bit 0 (the newest)
	if (whichRows & (1 << kRegisterRow)) {
		for (int32_t x = 0; x < 8; x++) {
			bool bit = e.getRegister() & (1 << (7 - x));
			image[kRegisterRow][x] = bit ? kRegisterOn : kRegisterOn.transform([](uint8_t v) { return v / 14; });
		}
	}

	auto bar = [&](int32_t row, float fraction, RGB colour) {
		if (!(whichRows & (1 << row))) {
			return;
		}
		int32_t lit = static_cast<int32_t>(clamp01(fraction) * kDisplayWidth + 0.5f);
		RGB dim = colour.transform([](uint8_t v) { return v / 12; });
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			image[row][x] = (x < lit) ? colour : dim;
		}
	};
	bar(kPhaseARow, e.getPhaseA(), kPhaseAColour);
	bar(kPhaseBRow, e.getPhaseB(), kPhaseBColour);
	bar(kCV1Row, e.getVolts(0) / range, kCV1Colour);
	bar(kCV2Row, e.getVolts(1) / range, kCV2Colour);

	PadLEDs::renderingLock = false;
	return true;
}

bool RunglerView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}
	int32_t lit = static_cast<int32_t>(runner::engine().params().chance * kDisplayHeight + 0.5f);
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (!(whichRows & (1 << y))) {
			continue;
		}
		image[y][kDisplayWidth] = (y < lit) ? kChanceColour : RGB::monochrome(8);
		if (kSideBarWidth > 1) {
			// gates 1 to 4 from the top
			int32_t gate = kDisplayHeight - 1 - y;
			RGB c = colours::black;
			if (gate >= 0 && gate < kNumGates) {
				bool owned = runner::isGateRungler(gate);
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

void RunglerView::pageValueText(int32_t knob, char* out, size_t size) const {
	const Params& p = runner::engine().params();
	// The 7-segment display shows four characters, so every value there is a one-letter tag plus the number:
	// A and B oscillator rates (a note division in tempo sync, Hz in free time), S slew, r range in volts
	bool oled = display->haveOLED();
	bool tempo = p.timeMode == TimeMode::TEMPO;
	if (page_ == 0) {
		bool isA = (knob == 0);
		if (tempo) {
			int32_t r = isA ? p.rateA : p.rateB;
			r = r < 0 ? 0 : (r >= marbles::kNumRates ? marbles::kNumRates - 1 : r);
			if (oled) {
				snprintf(out, size, "Rate %c %s (tempo)", isA ? 'A' : 'B', marbles::rateName(r));
			}
			else {
				snprintf(out, size, "%c%s", isA ? 'A' : 'b', kRateShort[r]);
			}
		}
		else {
			float hz = isA ? p.freqA : p.freqB;
			int32_t tenths = static_cast<int32_t>(hz * 10.0f + 0.5f);
			if (oled) {
				snprintf(out, size, "Rate %c %d.%d Hz (free)", isA ? 'A' : 'B', static_cast<int>(tenths / 10),
				         static_cast<int>(tenths % 10));
			}
			else if (tenths >= 100) {
				snprintf(out, size, "%c%2d", isA ? 'A' : 'b', static_cast<int>(tenths / 10));
			}
			else {
				snprintf(out, size, "%c%d.%d", isA ? 'A' : 'b', static_cast<int>(tenths / 10), static_cast<int>(tenths % 10));
			}
		}
	}
	else if (knob == 0) {
		snprintf(out, size, oled ? "Slew %d%%" : "S%d", static_cast<int>(percent(p.slew)));
	}
	else {
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
}

void RunglerView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 6;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 1;
#endif
	canvas.drawStringCentred("Rungler", yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	const Params& p = runner::engine().params();
	char text[48];
	if (!runner::isActive()) {
		snprintf(text, sizeof(text), "off");
	}
	else {
		snprintf(text, sizeof(text), "Chance %d%%  Mod %d%%", static_cast<int>(percent(p.chance)),
		         static_cast<int>(percent(p.modAmount)));
	}
	canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	char a[32], b[32];
	pageValueText(0, a, sizeof(a));
	pageValueText(1, b, sizeof(b));
	snprintf(text, sizeof(text), "%s: %s, %s", kPageNames[page_], a, b);
	canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
}

void RunglerView::refreshOLED() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	uiNeedsRendering(this);
}

void RunglerView::showValuePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->displayPopup(text, 0, true);
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kPopupMs);
	}
}

void RunglerView::showNamePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->setScrollingText(text);
	}
}

ActionResult RunglerView::timerCallback() {
	display->cancelPopup();
	return ActionResult::DEALT_WITH;
}

ActionResult RunglerView::horizontalEncoderAction(int32_t offset) {
	auto& e = runner::engine();
	e.setChance(e.params().chance + 0.01f * offset);
	char text[24];
	snprintf(text, sizeof(text), display->haveOLED() ? "Chance %d%%" : "c%d", static_cast<int>(percent(e.params().chance)));
	showValuePopup(text);
	refreshOLED();
	return ActionResult::DEALT_WITH;
}

ActionResult RunglerView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	auto& e = runner::engine();
	e.setModAmount(e.params().modAmount + 0.02f * (offset > 0 ? 1 : -1));
	char text[24];
	snprintf(text, sizeof(text), display->haveOLED() ? "Mod %d%%" : "m%d", static_cast<int>(percent(e.params().modAmount)));
	showValuePopup(text);
	refreshOLED();
	return ActionResult::DEALT_WITH;
}

void RunglerView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (whichModEncoder < 0 || whichModEncoder > 1) {
		return;
	}
	auto& e = runner::engine();
	const Params& p = e.params();
	int32_t dir = offset > 0 ? 1 : -1;
	// the lower gold knob is the first setting on a page, the upper gold knob the second
	int32_t knob = (whichModEncoder == 1) ? 1 : 0;
	if (page_ == 0) {
		if (p.timeMode == TimeMode::TEMPO) {
			if (knob == 0) {
				e.setRateA(p.rateA + dir);
			}
			else {
				e.setRateB(p.rateB + dir);
			}
		}
		else {
			float factor = std::pow(1.06f, static_cast<float>(dir));
			if (knob == 0) {
				e.setFreqA(p.freqA * factor);
			}
			else {
				e.setFreqB(p.freqB * factor);
			}
		}
	}
	else if (knob == 0) {
		e.setSlew(p.slew + 0.02f * dir);
	}
	else {
		e.setRange(p.rangeVolts + 0.25f * dir);
	}
	char text[32];
	pageValueText(knob, text, sizeof(text));
	showValuePopup(text);
	refreshOLED();
}

void RunglerView::selectEncoderAction(int8_t offset) {
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

ActionResult RunglerView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
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
			runner::engine().reset();
			if (Buttons::isShiftButtonPressed()) {
				showNamePopup("Restarted");
			}
			else {
				// a different register: step the generator on a few random shifts' worth
				auto& e = runner::engine();
				uint32_t v = AudioEngine::audioSampleTimer * 2654435761u;
				e.setRegister(static_cast<uint8_t>((v >> 13) & 0xFF) | 1);
				showNamePopup("New register");
			}
			refreshOLED();
		}
		return ActionResult::DEALT_WITH;
	}

	if (b == CV) {
		if (on) {
			bool enable = !runner::isActive();
			runner::setAll(enable);
			showNamePopup(!enable ? "Rungler off" : (runner::isActive() ? "Rungler on" : "No free outputs"));
			refreshOLED();
		}
		return ActionResult::DEALT_WITH;
	}

	return ClipView::buttonAction(b, on, inCardRoutine);
}

ActionResult RunglerView::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (!velocity) {
		return ActionResult::DEALT_WITH;
	}
	auto& e = runner::engine();

	// the chance column: the pad you press becomes the top of the bar, and tapping the top lit pad again zeroes it
	if (x == kDisplayWidth && y >= 0 && y < kDisplayHeight) {
		int32_t lit = static_cast<int32_t>(e.params().chance * kDisplayHeight + 0.5f);
		e.setChance((lit == y + 1) ? 0.0f : static_cast<float>(y + 1) / kDisplayHeight);
		char text[24];
		snprintf(text, sizeof(text), display->haveOLED() ? "Chance %d%%" : "c%d", static_cast<int>(percent(e.params().chance)));
		showValuePopup(text);
		refreshOLED();
		return ActionResult::DEALT_WITH;
	}

	// tap a register pad to flip that bit
	if (y == kRegisterRow && x >= 0 && x < 8) {
		e.toggleBit(7 - x);
		uiNeedsRendering(this);
	}
	return ActionResult::DEALT_WITH;
}
