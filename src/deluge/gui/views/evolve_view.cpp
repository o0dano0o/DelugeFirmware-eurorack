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

#include "gui/views/evolve_view.h"
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
#include "hid/led/pad_leds.h"
#include "model/clip/clip.h"
#include "model/song/song.h"
#include "processing/engines/euclid_evolver.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace deluge;
using namespace gui;
using namespace deluge::euclid;

EvolveView evolveView{};

namespace {

constexpr int32_t kPopupMs = 800;
constexpr int32_t kNumTracks = evolver::kNumTracks;

// Which gate track each physical encoder sets the amount of (the same mapping the Euclidean view uses)
constexpr int32_t kTrackForHorizontalEncoder = 0;
constexpr int32_t kTrackForVerticalEncoder = 1;
constexpr int32_t kTrackForModEncoder[2] = {3, 2};

// Pad columns in the upper row of each track's pair of rows
constexpr int32_t kOffX = 0;
constexpr int32_t kSourceX = 1; // 1 to 4
constexpr int32_t kHitsX = 6;
constexpr int32_t kRotateX = 7;
constexpr int32_t kDirectionX = 9; // 9 to 12
constexpr int32_t kRestoreX = 14;

constexpr int32_t upperRow(int32_t track) {
	return kDisplayHeight - 1 - 2 * track;
}
constexpr int32_t lowerRow(int32_t track) {
	return upperRow(track) - 1;
}

const RGB kTrackColour[kNumTracks] = {{255, 70, 70}, {255, 170, 40}, {70, 220, 90}, {70, 150, 255}};
const RGB kHitsColour{0, 210, 225};
const RGB kRotateColour{230, 80, 220};
const RGB kDirectionColour{255, 230, 90};
const RGB kRestoreColour{200, 200, 200};
const RGB kFrozenColour{255, 140, 0};

RGB dim(RGB c, int32_t divisor) {
	return RGB{static_cast<uint8_t>(c.r / divisor), static_cast<uint8_t>(c.g / divisor),
	           static_cast<uint8_t>(c.b / divisor)};
}

RGB pad(RGB c, bool bright) {
	return bright ? c : dim(c, 10);
}

char const* const kDirectionNames[] = {"up", "down", "bounce", "random"};
char const* const kTargetNames[] = {"hits", "rotation"};

} // namespace

bool EvolveView::opened() {
	focusRegained();
	return true;
}

void EvolveView::focusRegained() {
	ClipView::focusRegained();
	Clip* clip = getCurrentClip();
	if (clip) {
		view.focusRegained();
		view.setActiveModControllableTimelineCounter(clip);
	}
	PadLEDs::clearTickSquares();
	lastChangeCount_ = evolver::changeCount();
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		display->setScrollingText(evolver::isFrozen() ? "EVOLVE FROZEN" : "EVOLVE");
	}
	uiNeedsRendering(this);
}

void EvolveView::graphicsRoutine() {
	if (evolver::changeCount() != lastChangeCount_) {
		lastChangeCount_ = evolver::changeCount();
		refreshOLED();
	}
}

void EvolveView::refreshOLED() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	uiNeedsRendering(this);
}

void EvolveView::showValuePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->displayPopup(text, 0, true);
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kPopupMs);
	}
}

ActionResult EvolveView::timerCallback() {
	display->cancelPopup();
	return ActionResult::DEALT_WITH;
}

bool EvolveView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
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

	for (int32_t t = 0; t < kNumTracks; t++) {
		const EvolveRule& r = evolver::rule(t);
		int32_t top = upperRow(t);
		int32_t low = lowerRow(t);
		RGB self = kTrackColour[t];

		if (whichRows & (1 << top)) {
			image[top][kOffX] = pad(RGB{200, 200, 200}, !r.active());
			for (int32_t s = 0; s < kNumTracks; s++) {
				image[top][kSourceX + s] = pad(kTrackColour[s], r.source == s);
			}
			if (r.active()) {
				image[top][kHitsX] = pad(kHitsColour, r.target == EvolveRule::HITS);
				image[top][kRotateX] = pad(kRotateColour, r.target == EvolveRule::ROTATE);
				for (int32_t d = 0; d < 4; d++) {
					image[top][kDirectionX + d] = pad(kDirectionColour, r.direction == d);
				}
				image[top][kRestoreX] = dim(kRestoreColour, 6);
			}
		}
		if ((whichRows & (1 << low)) && r.active()) {
			for (int32_t x = 0; x < 16; x++) {
				image[low][x] = (x < r.every) ? self : dim(self, 14);
			}
		}
	}
	PadLEDs::renderingLock = false;
	return true;
}

bool EvolveView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                               uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (!(whichRows & (1 << y))) {
			continue;
		}
		// the whole mute column glows while everything is frozen
		image[y][kDisplayWidth] = evolver::isFrozen() ? kFrozenColour : colours::black;
		if (occupancyMask) {
			memset(occupancyMask[y] + kDisplayWidth, 0, kSideBarWidth);
		}
	}
	return true;
}

void EvolveView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 2;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 1;
#endif
	canvas.drawStringCentred(evolver::isFrozen() ? "Evolve (frozen)" : "Evolve", yPos, kTextSpacingX, kTextSpacingY);
	yPos += 10;
	for (int32_t t = 0; t < kNumTracks; t++) {
		const EvolveRule& r = evolver::rule(t);
		char text[40];
		if (!r.active()) {
			snprintf(text, sizeof(text), "G%d  -", static_cast<int>(t + 1));
		}
		else {
			snprintf(text, sizeof(text), "G%d<G%d /%d %s %s %d", static_cast<int>(t + 1),
			         static_cast<int>(r.source + 1), static_cast<int>(r.every), r.target == EvolveRule::HITS ? "hit" : "rot",
			         kDirectionNames[r.direction], static_cast<int>(r.amount));
		}
		canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
		yPos += 9;
	}
}

void EvolveView::showRule(int32_t track) {
	const EvolveRule& r = evolver::rule(track);
	char text[40];
	if (!r.active()) {
		snprintf(text, sizeof(text), display->haveOLED() ? "Gate %d: off" : "G%d OFF", static_cast<int>(track + 1));
	}
	else if (display->haveOLED()) {
		snprintf(text, sizeof(text), "G%d: every %d hits of G%d, %s %s %d", static_cast<int>(track + 1),
		         static_cast<int>(r.every), static_cast<int>(r.source + 1), kTargetNames[r.target],
		         kDirectionNames[r.direction], static_cast<int>(r.amount));
	}
	else {
		snprintf(text, sizeof(text), "%d%c%d", static_cast<int>(r.every), 'H', static_cast<int>(r.amount));
	}
	showValuePopup(text);
}

void EvolveView::changeAmount(int32_t track, int32_t offset) {
	EvolveRule r = evolver::rule(track);
	if (!r.active()) {
		return;
	}
	r.amount = static_cast<uint8_t>(std::clamp<int32_t>(r.amount + (offset > 0 ? 1 : -1), 1, 4));
	evolver::setRule(track, r);
	showRule(track);
	refreshOLED();
}

ActionResult EvolveView::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (!velocity || x >= kDisplayWidth) {
		return ActionResult::DEALT_WITH;
	}
	for (int32_t t = 0; t < kNumTracks; t++) {
		EvolveRule r = evolver::rule(t);
		if (y == upperRow(t)) {
			if (x == kOffX) {
				r.source = -1;
			}
			else if (x >= kSourceX && x < kSourceX + kNumTracks) {
				r.source = static_cast<int8_t>(x - kSourceX);
			}
			else if (!r.active()) {
				return ActionResult::DEALT_WITH;
			}
			else if (x == kHitsX) {
				r.target = EvolveRule::HITS;
			}
			else if (x == kRotateX) {
				r.target = EvolveRule::ROTATE;
			}
			else if (x >= kDirectionX && x < kDirectionX + 4) {
				r.direction = static_cast<EvolveRule::Direction>(x - kDirectionX);
			}
			else if (x == kRestoreX) {
				evolver::restoreBaseline(t);
				showValuePopup(display->haveOLED() ? "Pattern restored" : "RESTORED");
				refreshOLED();
				return ActionResult::DEALT_WITH;
			}
			else {
				return ActionResult::DEALT_WITH;
			}
			evolver::setRule(t, r);
			showRule(t);
			refreshOLED();
			return ActionResult::DEALT_WITH;
		}
		if (y == lowerRow(t) && r.active()) {
			r.every = static_cast<uint8_t>(x + 1);
			evolver::setRule(t, r);
			showRule(t);
			refreshOLED();
			return ActionResult::DEALT_WITH;
		}
	}
	return ActionResult::DEALT_WITH;
}

ActionResult EvolveView::horizontalEncoderAction(int32_t offset) {
	changeAmount(kTrackForHorizontalEncoder, offset);
	return ActionResult::DEALT_WITH;
}

ActionResult EvolveView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	changeAmount(kTrackForVerticalEncoder, offset);
	return ActionResult::DEALT_WITH;
}

void EvolveView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (whichModEncoder >= 0 && whichModEncoder < 2) {
		changeAmount(kTrackForModEncoder[whichModEncoder], offset);
	}
}

void EvolveView::selectEncoderAction(int8_t offset) {
	evolver::setFrozen(!evolver::isFrozen());
	showValuePopup(evolver::isFrozen() ? (display->haveOLED() ? "Evolve frozen" : "FROZEN")
	                                   : (display->haveOLED() ? "Evolve running" : "RUNNING"));
	refreshOLED();
}

ActionResult EvolveView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
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
			selectEncoderAction(1);
		}
		return ActionResult::DEALT_WITH;
	}
	return ClipView::buttonAction(b, on, inCardRoutine);
}
