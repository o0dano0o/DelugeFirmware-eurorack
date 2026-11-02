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

#include "gui/views/eurorack_view.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/ui/ui.h"
#include "gui/views/chaos_view.h"
#include "gui/views/euclidean_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/turing_view.h"
#include "gui/views/view.h"
#include "gui/views/voice_view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/pad_leds.h"
#include "model/clip/clip.h"
#include "model/song/song.h"
#include "processing/engines/chaos_runner.h"
#include "processing/engines/cv_engine.h"
#include "processing/engines/turing_runner.h"
#include "processing/engines/voice_runner.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace deluge;
using namespace gui;

EurorackView eurorackView{};

namespace {

enum Owner : uint8_t { EUCLID, CHAOS, TURING, VOICE, SYSTEM, kNumOwners = SYSTEM };

constexpr int32_t kNumGates = 4;
constexpr int32_t kNumCVs = 2;
constexpr int32_t kNumOutputs = kNumGates + kNumCVs;
constexpr int32_t kTopRow = kDisplayHeight - 1;
constexpr int32_t kKeyRow = 0;
constexpr int32_t kMeterStart = 5;
constexpr int32_t kMeterWidth = kDisplayWidth - kMeterStart;
constexpr int32_t kHoldMs = 120;

const RGB kOwnerColour[kNumOwners] = {RGB{230, 230, 230}, RGB{200, 40, 255}, RGB{30, 220, 40}, RGB{255, 40, 110}};
const RGB kSystemColour{60, 60, 60};

char const* const kOwnerNames[kNumOwners] = {"Euclid", "Chaos", "Turing", "Voice"};

/// Output index 0-3 are gates 1-4, 4-5 are CV 1-2
int32_t rowForOutput(int32_t output) {
	return kTopRow - output;
}

int32_t outputForRow(int32_t y) {
	int32_t output = kTopRow - y;
	return (output >= 0 && output < kNumOutputs) ? output : -1;
}

bool isGate(int32_t output) {
	return output < kNumGates;
}

int32_t channelOf(int32_t output) {
	return isGate(output) ? output : output - kNumGates;
}

/// Gates set to the run or clock signal belong to the system
bool isSystemGate(int32_t gate) {
	return cvEngine.gateChannels[gate].mode == GateType::SPECIAL;
}

Owner ownerOf(int32_t output) {
	int32_t channel = channelOf(output);
	if (isGate(output) && isSystemGate(channel)) {
		return SYSTEM;
	}
	if (deluge::turing::runner::getChannel() == channel) {
		return TURING;
	}
	if (deluge::voice::runner::getChannel() == channel) {
		return VOICE;
	}
	if (isGate(output) ? deluge::chaos::runner::isGateChaos(channel) : deluge::chaos::runner::isCVChaos(channel)) {
		return CHAOS;
	}
	return EUCLID;
}

/// Turing and voice use a CV and gate pair, so only channels 1 and 2 can have them
bool canOwn(int32_t output, Owner owner) {
	if (isGate(output) && isSystemGate(channelOf(output))) {
		return false;
	}
	if (owner == TURING || owner == VOICE) {
		return channelOf(output) < kNumCVs;
	}
	if (owner == CHAOS && !isGate(output)) {
		return channelOf(output) < deluge::chaos::kNumCVOutputs;
	}
	return true;
}

} // namespace

bool EurorackView::opened() {
	focusRegained();
	return true;
}

void EurorackView::focusRegained() {
	PadLEDs::clearTickSquares();
	lastSignature_ = 0xFFFFFFFF;

	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		display->setScrollingText("EURORACK");
	}
	uiNeedsRendering(this);
}

void EurorackView::graphicsRoutine() {
	// Redraw when an owner changes, a gate changes (or just pulsed), or a CV bar moves a step
	uint32_t signature = 0;
	for (int32_t output = 0; output < kNumOutputs; output++) {
		signature = signature * 31 + ownerOf(output);
		int32_t channel = channelOf(output);
		if (isGate(output)) {
			bool on = cvEngine.gateChannels[channel].on;
			bool recent = cvEngine.gateRecentlyOn(channel, kHoldMs);
			signature = signature * 3 + (on ? 2 : (recent ? 1 : 0));
		}
		else {
			signature = signature * 13 + cvEngine.getLastVoltageCode(channel) * kMeterWidth / 65521;
		}
	}
	if (signature != lastSignature_) {
		lastSignature_ = signature;
		uiNeedsRendering(this);
	}
}

bool EurorackView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
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

		int32_t output = outputForRow(y);
		if (output >= 0) {
			Owner owner = ownerOf(output);
			RGB ownerColour = owner == SYSTEM ? kSystemColour : kOwnerColour[owner];

			// who drives it: the current owner bright, the others a faint tint of their colour, unavailable ones dark
			for (int32_t o = 0; o < kNumOwners; o++) {
				if (!canOwn(output, static_cast<Owner>(o))) {
					continue;
				}
				image[y][o] = (o == owner) ? kOwnerColour[o] : kOwnerColour[o].transform([](uint8_t v) { return v / 12; });
			}
			if (owner == SYSTEM) {
				image[y][0] = kSystemColour;
			}

			int32_t channel = channelOf(output);
			if (isGate(output)) {
				bool on = cvEngine.gateChannels[channel].on;
				bool recent = cvEngine.gateRecentlyOn(channel, kHoldMs);
				RGB c = on ? ownerColour : (recent ? ownerColour.transform([](uint8_t v) { return v / 3; })
				                                   : ownerColour.transform([](uint8_t v) { return v / 20; }));
				for (int32_t x = kMeterStart; x < kDisplayWidth; x++) {
					image[y][x] = c;
				}
			}
			else {
				int32_t lit = (cvEngine.getLastVoltageCode(channel) * kMeterWidth + 32760) / 65520;
				lit = std::max<int32_t>(0, std::min<int32_t>(lit, kMeterWidth));
				for (int32_t x = 0; x < kMeterWidth; x++) {
					image[y][kMeterStart + x] =
					    (x < lit) ? ownerColour : ownerColour.transform([](uint8_t v) { return v / 20; });
				}
			}
		}
		else if (y == kKeyRow) {
			for (int32_t o = 0; o < kNumOwners; o++) {
				image[y][o] = kOwnerColour[o];
			}
		}
	}

	PadLEDs::renderingLock = false;
	return true;
}

bool EurorackView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                 uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (!(whichRows & (1 << y))) {
			continue;
		}
		std::fill(image[y] + kDisplayWidth, image[y] + kDisplayWidth + kSideBarWidth, colours::black);
		if (occupancyMask) {
			memset(occupancyMask[y] + kDisplayWidth, 0, kSideBarWidth);
		}
	}
	return true;
}

void EurorackView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 6;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 1;
#endif
	canvas.drawStringCentred("Eurorack", yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	// "G1 Euclid  G2 Chaos" in two lines, then the CVs
	for (int32_t pair = 0; pair < 3; pair++) {
		char text[48];
		char* end = text;
		for (int32_t k = 0; k < 2; k++) {
			int32_t output = pair * 2 + k;
			if (k) {
				end += snprintf(end, 8, "  ");
			}
			Owner owner = ownerOf(output);
			end += snprintf(end, 20, "%s%d %s", isGate(output) ? "G" : "CV", static_cast<int>(channelOf(output) + 1),
			                owner == SYSTEM ? "Sys" : kOwnerNames[owner]);
		}
		canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
		yPos += 10;
	}
}

ActionResult EurorackView::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (!velocity) {
		return ActionResult::DEALT_WITH;
	}
	// Tapping the meter part of a row opens the feature that drives that output
	if (x >= kNumOwners + 1 && x < kDisplayWidth) {
		return openOwnerOfRow(y);
	}
	if (x >= kNumOwners) {
		return ActionResult::DEALT_WITH;
	}
	int32_t output = outputForRow(y);
	if (output < 0 || !canOwn(output, static_cast<Owner>(x))) {
		return ActionResult::DEALT_WITH;
	}
	auto owner = static_cast<Owner>(x);
	int32_t channel = channelOf(output);
	namespace chaos = deluge::chaos::runner;

	// First take the output away from whatever has it. Turing and voice hold a whole CV and gate pair.
	if (deluge::turing::runner::getChannel() == channel && owner != TURING) {
		deluge::turing::runner::setChannel(-1);
	}
	if (deluge::voice::runner::getChannel() == channel && owner != VOICE) {
		deluge::voice::runner::setChannel(-1);
	}
	if (owner != CHAOS) {
		if (isGate(output)) {
			chaos::setGateChaos(channel, false);
		}
		else {
			chaos::setCVChaos(channel, false);
		}
	}

	switch (owner) {
	case CHAOS:
		if (isGate(output)) {
			chaos::setGateChaos(channel, true);
		}
		else {
			chaos::setCVChaos(channel, true);
		}
		break;
	case TURING:
		deluge::turing::runner::setChannel(channel);
		break;
	case VOICE:
		deluge::voice::runner::setChannel(channel);
		break;
	default:
		break;
	}

	char text[40];
	snprintf(text, sizeof(text), "%s%d %s", isGate(output) ? "Gate " : "CV ", static_cast<int>(channel + 1),
	         kOwnerNames[owner]);
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
		renderUIsForOled();
	}
	else {
		display->setScrollingText(text);
	}
	uiNeedsRendering(this);
	return ActionResult::DEALT_WITH;
}

ActionResult EurorackView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	if (b == BACK || (b == CV && Buttons::isShiftButtonPressed())) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			display->cancelPopup();
			changeRootUI(returnTo_ ? returnTo_ : static_cast<UI*>(&sessionView));
		}
		return ActionResult::DEALT_WITH;
	}

	// KIT opens the Euclidean view, turning the current clip into a Kit first if it isn't one
	if (b == KIT) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			Clip* clip = getCurrentClip();
			if (!clip || clip->type != ClipType::INSTRUMENT) {
				showNotice("Open a clip first");
				return ActionResult::DEALT_WITH;
			}
			display->cancelPopup();
			// Go through the clip view, which knows how to turn the clip into a Kit and then opens the Euclidean view
			openedFromHere_ = true;
			changeRootUI(&instrumentClipView);
			instrumentClipView.openEuclidean();
		}
		return ActionResult::DEALT_WITH;
	}

	// The generator views are built around a clip, so they need one
	if (b == CV || b == KEYBOARD || b == MIDI) {
		if (on) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			if (!getCurrentClip()) {
				showNotice("Open a clip first");
				return ActionResult::DEALT_WITH;
			}
			display->cancelPopup();
			openedFromHere_ = true;
			changeRootUI(b == CV ? static_cast<UI*>(&chaosView)
			                     : (b == KEYBOARD ? static_cast<UI*>(&turingView) : static_cast<UI*>(&voiceView)));
		}
		return ActionResult::DEALT_WITH;
	}

	return UI::buttonAction(b, on, inCardRoutine);
}

ActionResult EurorackView::openOwnerOfRow(int32_t y) {
	int32_t output = outputForRow(y);
	if (output < 0) {
		return ActionResult::DEALT_WITH;
	}
	int32_t channel = channelOf(output);
	if (!getCurrentClip()) {
		showNotice("Open a clip first");
		return ActionResult::DEALT_WITH;
	}
	UI* target = nullptr;
	if (deluge::turing::runner::getChannel() == channel) {
		target = &turingView;
	}
	else if (deluge::voice::runner::getChannel() == channel) {
		target = &voiceView;
	}
	else if (isGate(output) ? deluge::chaos::runner::isGateChaos(channel) : deluge::chaos::runner::isCVChaos(channel)) {
		target = &chaosView;
	}
	if (target) {
		display->cancelPopup();
		openedFromHere_ = true;
		changeRootUI(target);
		return ActionResult::DEALT_WITH;
	}
	// Euclid (or unassigned) opens the Euclidean view, which needs a Kit clip
	if (getCurrentClip()->type != ClipType::INSTRUMENT) {
		showNotice("Open a clip first");
		return ActionResult::DEALT_WITH;
	}
	display->cancelPopup();
	openedFromHere_ = true;
	changeRootUI(&instrumentClipView);
	instrumentClipView.openEuclidean();
	return ActionResult::DEALT_WITH;
}

void EurorackView::showNotice(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->setScrollingText(text);
	}
}
