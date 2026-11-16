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

#include "gui/views/billiards_view.h"
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
#include "processing/engines/billiards_runner.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace deluge;
using namespace gui;
using namespace deluge::billiards;

BilliardsView billiardsView{};

namespace {

constexpr int32_t kPopupMs = 800;
constexpr int32_t kFlashMs = 120;
constexpr int32_t kNumPages = 3;
constexpr float kMinBounce = 0.3f;

const RGB kBallColour[kMaxBalls] = {RGB{255, 200, 40}, RGB{40, 220, 120}, RGB{255, 80, 160}, RGB{80, 200, 255}};
const RGB kFlashColour{255, 255, 255};
const RGB kBumperColour{40, 80, 255};
const RGB kBounceColour{255, 160, 40};
const RGB kGateColour{40, 160, 255};

char const* const kPageNames[kNumPages] = {"Balls and CV ball", "Bounce and range", "Gate mode and clear"};
char const* const kPageNamesShort[kNumPages] = {"bAL", "bnC", "GAtE"};

int32_t percent(float v) {
	return static_cast<int32_t>(v * 100.0f + 0.5f);
}

RGB scaled(RGB c, int32_t pct) {
	return c.transform([pct](uint8_t v) { return static_cast<uint8_t>(v * pct / 100); });
}

} // namespace

bool BilliardsView::opened() {
	focusRegained();
	return true;
}

void BilliardsView::focusRegained() {
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
		display->setScrollingText(runner::isActive() ? "BILLIARDS" : "BILLIARDS OFF  CV TO START");
	}
	uiNeedsRendering(this);
}

void BilliardsView::graphicsRoutine() {
	const BilliardsEngine& e = runner::engine();
	uint32_t now = AudioEngine::audioSampleTimer;
	uint32_t signature = 0;
	for (int32_t i = 0; i < e.params().numBalls; i++) {
		uint32_t hits = e.getHitCount(i);
		if (hits != lastHits_[i]) {
			lastHits_[i] = hits;
			flashUntil_[i] = now + kFlashMs * 44;
		}
		bool flashing = static_cast<int32_t>(flashUntil_[i] - now) > 0;
		signature = signature * 31 + static_cast<uint32_t>(e.getX(i)) * 8 + static_cast<uint32_t>(e.getY(i));
		signature = signature * 3 + (flashing ? 1 : 0);
	}
	for (int32_t g = 0; g < kNumGates; g++) {
		signature = signature * 3 + (runner::gateRecentlyHigh(g, kFlashMs) ? 1 : 0) + (runner::isGateBilliards(g) ? 1 : 0);
	}
	if (signature != lastSignature_) {
		lastSignature_ = signature;
		uiNeedsRendering(this);
	}
}

bool BilliardsView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	if (!image) {
		return true;
	}
	PadLEDs::renderingLock = true;

	const BilliardsEngine& e = runner::engine();
	uint32_t now = AudioEngine::audioSampleTimer;

	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (!(whichRows & (1 << y))) {
			continue;
		}
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			image[y][x] = e.hasBumper(x, y) ? kBumperColour : RGB::monochrome(2);
		}
		if (occupancyMask) {
			memset(occupancyMask[y], 64, kDisplayWidth);
		}
	}
	for (int32_t i = 0; i < e.params().numBalls; i++) {
		int32_t x = std::min(kWidth - 1, static_cast<int32_t>(e.getX(i)));
		int32_t y = std::min(kHeight - 1, static_cast<int32_t>(e.getY(i)));
		if (!(whichRows & (1 << y))) {
			continue;
		}
		bool flashing = static_cast<int32_t>(flashUntil_[i] - now) > 0;
		image[y][x] = flashing ? kFlashColour : kBallColour[i];
	}

	PadLEDs::renderingLock = false;
	return true;
}

bool BilliardsView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                  uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}
	float bounce = runner::engine().params().bounce;
	int32_t lit = static_cast<int32_t>((bounce - kMinBounce) / (1.0f - kMinBounce) * kDisplayHeight + 0.5f);
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (!(whichRows & (1 << y))) {
			continue;
		}
		image[y][kDisplayWidth] = (y < lit) ? kBounceColour : RGB::monochrome(8);
		if (kSideBarWidth > 1) {
			int32_t gate = kDisplayHeight - 1 - y;
			RGB c = colours::black;
			if (gate >= 0 && gate < kNumGates) {
				bool owned = runner::isGateBilliards(gate);
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

void BilliardsView::pageValueText(int32_t knob, char* out, size_t size) const {
	const Params& p = runner::engine().params();
	bool oled = display->haveOLED();
	switch (page_) {
	case 0:
		if (knob == 0) {
			snprintf(out, size, oled ? "Balls %d" : "bL%d", static_cast<int>(p.numBalls));
		}
		else {
			snprintf(out, size, oled ? "CV follows ball %d" : "cb%d", static_cast<int>(p.cvBall + 1));
		}
		break;
	case 1:
		if (knob == 0) {
			snprintf(out, size, oled ? "Bounce %d%%" : "b%d", static_cast<int>(percent(p.bounce)));
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
		break;
	default:
		if (knob == 0) {
			bool balls = p.gateMode == GateMode::BALLS;
			snprintf(out, size, "%s", oled ? (balls ? "Gate per ball" : "Gate per wall") : (balls ? "bALL" : "WALL"));
		}
		else {
			snprintf(out, size, "%s", oled ? "Turn to clear bumpers" : "CLr");
		}
		break;
	}
}

void BilliardsView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 6;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 1;
#endif
	canvas.drawStringCentred("Billiards", yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	const Params& p = runner::engine().params();
	char text[64];
	if (!runner::isActive()) {
		snprintf(text, sizeof(text), "off");
	}
	else {
		snprintf(text, sizeof(text), "Speed %d  Gravity %d", static_cast<int>(p.speed + 0.5f),
		         static_cast<int>(p.gravity + 0.5f));
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

void BilliardsView::refreshOLED() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	uiNeedsRendering(this);
}

void BilliardsView::showValuePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->displayPopup(text, 0, true);
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kPopupMs);
	}
}

void BilliardsView::showNamePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->setScrollingText(text);
	}
}

ActionResult BilliardsView::timerCallback() {
	display->cancelPopup();
	return ActionResult::DEALT_WITH;
}

void BilliardsView::showSpeed() {
	float v = runner::engine().params().speed;
	char text[24];
	if (display->haveOLED()) {
		snprintf(text, sizeof(text), "Speed %d.%d", static_cast<int>(v), static_cast<int>(v * 10.0f + 0.5f) % 10);
	}
	else if (v >= 9.95f) {
		snprintf(text, sizeof(text), "S%d", static_cast<int>(v + 0.5f));
	}
	else {
		int32_t tenths = static_cast<int32_t>(v * 10.0f + 0.5f);
		snprintf(text, sizeof(text), "S%d.%d", static_cast<int>(tenths / 10), static_cast<int>(tenths % 10));
	}
	showValuePopup(text);
}

void BilliardsView::showGravity() {
	char text[24];
	snprintf(text, sizeof(text), display->haveOLED() ? "Gravity %d" : "g%d",
	         static_cast<int>(runner::engine().params().gravity + 0.5f));
	showValuePopup(text);
}

ActionResult BilliardsView::horizontalEncoderAction(int32_t offset) {
	auto& e = runner::engine();
	e.setSpeed(e.params().speed * std::pow(1.05f, static_cast<float>(offset)));
	showSpeed();
	refreshOLED();
	return ActionResult::DEALT_WITH;
}

ActionResult BilliardsView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	auto& e = runner::engine();
	e.setGravity(e.params().gravity + 1.0f * (offset > 0 ? 1 : -1));
	showGravity();
	refreshOLED();
	return ActionResult::DEALT_WITH;
}

void BilliardsView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
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
		if (knob == 0) {
			e.setNumBalls(p.numBalls + dir);
		}
		else {
			e.setCvBall(std::max<int32_t>(0, std::min<int32_t>(p.numBalls - 1, p.cvBall + dir)));
		}
		break;
	case 1:
		if (knob == 0) {
			e.setBounce(p.bounce + 0.05f * dir);
		}
		else {
			e.setRange(p.rangeVolts + 0.25f * dir);
		}
		break;
	default:
		if (knob == 0) {
			e.setGateMode(p.gateMode == GateMode::WALLS ? GateMode::BALLS : GateMode::WALLS);
		}
		else {
			e.clearBumpers();
		}
		break;
	}
	char text[48];
	pageValueText(knob, text, sizeof(text));
	showValuePopup(text);
	refreshOLED();
}

void BilliardsView::selectEncoderAction(int8_t offset) {
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

ActionResult BilliardsView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
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
				runner::engine().relaunch();
				showNamePopup(display->haveOLED() ? "Relaunched" : "LAun");
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
			showNamePopup(!enable ? "Billiards off" : (runner::isActive() ? "Billiards on" : "No free outputs"));
			refreshOLED();
		}
		return ActionResult::DEALT_WITH;
	}

	return ClipView::buttonAction(b, on, inCardRoutine);
}

ActionResult BilliardsView::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (!velocity) {
		return ActionResult::DEALT_WITH;
	}
	auto& e = runner::engine();

	// the bounce column: the pad you press becomes the top of the bar, and tapping the top lit pad again sets the minimum
	if (x == kDisplayWidth && y >= 0 && y < kDisplayHeight) {
		int32_t lit = static_cast<int32_t>((e.params().bounce - kMinBounce) / (1.0f - kMinBounce) * kDisplayHeight + 0.5f);
		float v = (lit == y + 1) ? kMinBounce : kMinBounce + (1.0f - kMinBounce) * static_cast<float>(y + 1) / kDisplayHeight;
		e.setBounce(v);
		char text[24];
		snprintf(text, sizeof(text), display->haveOLED() ? "Bounce %d%%" : "b%d", static_cast<int>(percent(e.params().bounce)));
		showValuePopup(text);
		refreshOLED();
		return ActionResult::DEALT_WITH;
	}

	// a pad on the table puts a bumper there or takes it away
	if (x >= 0 && x < kWidth && y >= 0 && y < kHeight) {
		e.toggleBumper(x, y);
		uiNeedsRendering(this);
	}
	return ActionResult::DEALT_WITH;
}
