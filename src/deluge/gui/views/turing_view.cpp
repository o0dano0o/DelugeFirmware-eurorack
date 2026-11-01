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

#include "gui/views/turing_view.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/euclidean_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/pad_leds.h"
#include "model/clip/clip.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/turing_runner.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace deluge;
using namespace gui;
using namespace deluge::turing;

TuringView turingView{};

namespace {

constexpr int32_t kPopupMs = 800;
constexpr int32_t kStepFlashMs = 120;
constexpr int32_t kRegisterRow = kDisplayHeight - 1;
constexpr int32_t kPlotRows = kDisplayHeight - 2; ///< rows 0 to 5 hold the dot plot

const RGB kBitOn{255, 170, 40};
const RGB kBitOff{18, 12, 4};
const RGB kPlotColour{60, 200, 255};
const RGB kProbabilityColour{255, 90, 200};
const RGB kNoteColour{0, 210, 225};

char const* const kClockNames[] = {"Song clock", "Chaos lobe", "Chaos loop"};
char const* const kClockShort[] = {"Song", "Lobe", "Loop"};

} // namespace

bool TuringView::opened() {
	if (!runner::isActive()) {
		runner::setChannel(0);
	}
	focusRegained();
	return true;
}

void TuringView::focusRegained() {
	ClipView::focusRegained();

	Clip* clip = getCurrentClip();
	if (clip) {
		view.focusRegained();
		view.setActiveModControllableTimelineCounter(clip);
	}
	PadLEDs::clearTickSquares();
	lastDrawnStep_ = 0xFFFFFFFF;
	lastFrameTimer_ = AudioEngine::audioSampleTimer;

	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		display->setScrollingText("TURING");
	}
	uiNeedsRendering(this);
}

void TuringView::graphicsRoutine() {
	bool flash = runner::stepRecently(kStepFlashMs);
	uint32_t step = runner::getStepCount();
	if (step != lastDrawnStep_ || flash != lastDrawnFlash_) {
		lastDrawnStep_ = step;
		lastDrawnFlash_ = flash;
		uiNeedsRendering(this);
	}
}

bool TuringView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
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

	// shift register, with the bit about to fall off the end (last in the loop) a little brighter
	if (whichRows & (1 << kRegisterRow)) {
		int32_t length = runner::engine().getLength();
		uint16_t reg = runner::engine().getRegister();
		bool flash = runner::stepRecently(kStepFlashMs);
		for (int32_t bit = 0; bit < length; bit++) {
			bool on = (reg >> bit) & 1;
			RGB c = on ? kBitOn : kBitOff;
			if (bit == 0 && flash) {
				c = on ? RGB{255, 255, 255} : RGB{90, 60, 20};
			}
			image[kRegisterRow][bit] = c;
		}
	}

	// dot plot of the last 16 notes, oldest on the left
	for (int32_t col = 0; col < runner::kHistoryLength && col < kDisplayWidth; col++) {
		int32_t age = runner::kHistoryLength - 1 - col;
		if (static_cast<uint32_t>(age) >= runner::getStepCount()) {
			continue;
		}
		int32_t value = runner::getHistoryValue(age);
		int32_t row = std::min(kPlotRows - 1, value * kPlotRows / 256);
		if (whichRows & (1 << row)) {
			image[row][col] = (age == 0) ? RGB{255, 255, 255} : kPlotColour;
		}
	}

	PadLEDs::renderingLock = false;
	return true;
}

bool TuringView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                               uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}
	float probability = runner::engine().getFlipProbability();
	int32_t probLit = static_cast<int32_t>(probability * kDisplayHeight + 0.5f);
	int32_t noteLit = (runner::engine().getValue() * kDisplayHeight) / 256 + 1;
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (!(whichRows & (1 << y))) {
			continue;
		}
		image[y][kDisplayWidth] = (y < probLit) ? kProbabilityColour : RGB::monochrome(8);
		if (kSideBarWidth > 1) {
			image[y][kDisplayWidth + 1] = (y < noteLit) ? kNoteColour : RGB::monochrome(8);
		}
		if (occupancyMask) {
			memset(occupancyMask[y] + kDisplayWidth, 0, kSideBarWidth);
		}
	}
	return true;
}

void TuringView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 6;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 1;
#endif
	canvas.drawStringCentred("Turing", yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	char text[40];
	int32_t channel = runner::getChannel();
	if (channel < 0) {
		snprintf(text, sizeof(text), "off");
	}
	else {
		snprintf(text, sizeof(text), "CV/gate %d  %d steps  %d%%", static_cast<int>(channel + 1),
		         static_cast<int>(runner::engine().getLength()),
		         static_cast<int>(runner::engine().getFlipProbability() * 100.0f + 0.5f));
	}
	canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	int32_t clock = static_cast<int32_t>(runner::getClockSource());
	snprintf(text, sizeof(text), "%s %s  %d oct  %s", kClockShort[clock],
	         clock == 0 ? runner::divisionName(runner::getDivision()) : "", static_cast<int>(runner::getOctaves()),
	         runner::getRandomSource() == runner::RandomSource::CHAOS ? "chaos" : "rng");
	canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
}

void TuringView::refreshOLED() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	uiNeedsRendering(this);
}

void TuringView::showValuePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->displayPopup(text, 0, true);
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kPopupMs);
	}
}

void TuringView::showNamePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->setScrollingText(text);
	}
}

ActionResult TuringView::timerCallback() {
	display->cancelPopup();
	return ActionResult::DEALT_WITH;
}

ActionResult TuringView::horizontalEncoderAction(int32_t offset) {
	float p = runner::engine().getFlipProbability() + 0.01f * offset;
	runner::setFlipProbability(std::max(0.0f, std::min(p, 1.0f)));
	char text[24];
	int32_t percent = static_cast<int32_t>(runner::engine().getFlipProbability() * 100.0f + 0.5f);
	if (display->haveOLED()) {
		snprintf(text, sizeof(text), "Flip %d%%", static_cast<int>(percent));
	}
	else {
		snprintf(text, sizeof(text), "P%d", static_cast<int>(percent));
	}
	showValuePopup(text);
	refreshOLED();
	return ActionResult::DEALT_WITH;
}

ActionResult TuringView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	runner::setLength(runner::engine().getLength() + offset);
	char text[24];
	snprintf(text, sizeof(text), display->haveOLED() ? "Length %d" : "L%d", static_cast<int>(runner::engine().getLength()));
	showValuePopup(text);
	refreshOLED();
	return ActionResult::DEALT_WITH;
}

void TuringView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (whichModEncoder == 0) {
		runner::setDivision(runner::getDivision() + (offset > 0 ? 1 : -1));
		char text[24];
		snprintf(text, sizeof(text), display->haveOLED() ? "Division %s" : "%s", runner::divisionName(runner::getDivision()));
		showValuePopup(text);
	}
	else if (whichModEncoder == 1) {
		runner::setOctaves(runner::getOctaves() + (offset > 0 ? 1 : -1));
		char text[24];
		snprintf(text, sizeof(text), display->haveOLED() ? "Range %d oct" : "O%d", static_cast<int>(runner::getOctaves()));
		showValuePopup(text);
	}
	refreshOLED();
}

void TuringView::selectEncoderAction(int8_t offset) {
	constexpr int32_t n = static_cast<int32_t>(runner::ClockSource::kNumSources);
	int32_t next = (static_cast<int32_t>(runner::getClockSource()) + (offset > 0 ? 1 : n - 1)) % n;
	runner::setClockSource(static_cast<runner::ClockSource>(next));
	showNamePopup(kClockNames[next]);
	refreshOLED();
}

ActionResult TuringView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	if (b == BACK) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			display->cancelPopup();
			changeRootUI(&euclideanView);
		}
		return ActionResult::DEALT_WITH;
	}

	if (b == SELECT_ENC) {
		if (on) {
			if (Buttons::isShiftButtonPressed()) {
				bool chaos = runner::getRandomSource() != runner::RandomSource::CHAOS;
				runner::setRandomSource(chaos ? runner::RandomSource::CHAOS : runner::RandomSource::RNG);
				showNamePopup(chaos ? "Flips from chaos" : "Flips from RNG");
			}
			else {
				runner::randomiseRegister();
				showNamePopup("New pattern");
			}
			refreshOLED();
		}
		return ActionResult::DEALT_WITH;
	}

	if (b == CV) {
		if (on) {
			int32_t next = runner::getChannel() + 1;
			runner::setChannel(next > 1 ? -1 : next);
			char text[24];
			if (runner::getChannel() < 0) {
				snprintf(text, sizeof(text), "Turing off");
			}
			else {
				snprintf(text, sizeof(text), "CV/gate %d", static_cast<int>(runner::getChannel() + 1));
			}
			showNamePopup(text);
			refreshOLED();
		}
		return ActionResult::DEALT_WITH;
	}

	return ClipView::buttonAction(b, on, inCardRoutine);
}

ActionResult TuringView::padAction(int32_t x, int32_t y, int32_t velocity) {
	// tap a bit of the register to flip it
	if (velocity && y == kRegisterRow && x < runner::engine().getLength()) {
		auto& e = runner::engine();
		e.setRegister(e.getRegister() ^ (1u << x));
		refreshOLED();
	}
	return ActionResult::DEALT_WITH;
}
