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

#include "gui/views/neurons_view.h"
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
#include "processing/engines/neurons_runner.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace deluge;
using namespace gui;
using namespace deluge::neurons;

NeuronsView neuronsView{};

namespace {

constexpr int32_t kPopupMs = 800;
constexpr int32_t kFlashMs = 120;
constexpr int32_t kNumPages = 4;
constexpr int32_t kTopRow = kDisplayHeight - 1;  ///< neuron 1's row
constexpr int32_t kSpikeColumn = 4;
constexpr int32_t kMembraneStart = 5;
constexpr int32_t kMembraneWidth = kDisplayWidth - kMembraneStart;
constexpr int32_t kCV1Row = 1;
constexpr int32_t kCV2Row = 0;

const RGB kExcite{40, 220, 70};
const RGB kInhibit{255, 50, 50};
const RGB kMembraneColour{40, 160, 255};
const RGB kSpikeColour{255, 255, 255};
const RGB kCV1Colour{200, 60, 255};
const RGB kCV2Colour{255, 60, 120};
const RGB kNoiseColour{255, 200, 40};
const RGB kGateColour{40, 160, 255};

char const* const kPageNames[kNumPages] = {"Rates 1 and 2", "Rates 3 and 4", "CV sources", "Range and preset"};
char const* const kPageNamesShort[kNumPages] = {"r12", "r34", "SrC", "rP"};
char const kLetters[kNumNeurons + 1] = {'A', 'b', 'C', 'd', 'S'};

float clamp01(float v) {
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

int32_t percent(float v) {
	return static_cast<int32_t>(v * 100.0f + 0.5f);
}

/// 0, then 0.5 and 1 excite, then 1 and 0.5 inhibit, then back to nothing
int32_t nextLevel(int32_t level) {
	switch (level) {
	case 0:
		return 1;
	case 1:
		return 2;
	case 2:
		return -1;
	case -1:
		return -2;
	default:
		return 0;
	}
}

} // namespace

bool NeuronsView::opened() {
	focusRegained();
	return true;
}

void NeuronsView::focusRegained() {
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
		display->setScrollingText(runner::isActive() ? "NEURONS" : "NEURONS OFF  CV TO START");
	}
	uiNeedsRendering(this);
}

void NeuronsView::graphicsRoutine() {
	const NeuronsEngine& e = runner::engine();
	uint32_t signature = 0;
	for (int32_t n = 0; n < kNumNeurons; n++) {
		signature = signature * 31 + static_cast<uint32_t>(e.getMembrane(n) * kMembraneWidth);
		signature = signature * 3 + (runner::gateRecentlyHigh(n, kFlashMs) ? 1 : 0) + (runner::isGateNeurons(n) ? 1 : 0);
	}
	signature = signature * 31 + static_cast<uint32_t>(e.getVolts(0) * 4.0f);
	signature = signature * 31 + static_cast<uint32_t>(e.getVolts(1) * 4.0f);
	if (signature != lastSignature_) {
		lastSignature_ = signature;
		uiNeedsRendering(this);
	}
}

bool NeuronsView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
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

	const NeuronsEngine& e = runner::engine();
	const Params& p = e.params();

	for (int32_t n = 0; n < kNumNeurons; n++) {
		int32_t y = kTopRow - n;
		if (!(whichRows & (1 << y))) {
			continue;
		}
		for (int32_t s = 0; s < kNumNeurons; s++) {
			int32_t level = p.weight[n][s];
			RGB c = RGB::monochrome(6);
			if (n == s) {
				c = RGB::monochrome(2);
			}
			else if (level > 0) {
				c = kExcite.transform([level](uint8_t v) { return static_cast<uint8_t>(v * (level == 2 ? 10 : 4) / 10); });
			}
			else if (level < 0) {
				c = kInhibit.transform([level](uint8_t v) { return static_cast<uint8_t>(v * (level == -2 ? 10 : 4) / 10); });
			}
			image[y][s] = c;
		}
		bool fired = runner::gateRecentlyHigh(n, kFlashMs);
		image[y][kSpikeColumn] = fired ? kSpikeColour : RGB::monochrome(3);
		int32_t lit = static_cast<int32_t>(clamp01(e.getMembrane(n)) * kMembraneWidth + 0.5f);
		RGB dim = kMembraneColour.transform([](uint8_t v) { return v / 12; });
		for (int32_t x = 0; x < kMembraneWidth; x++) {
			image[y][kMembraneStart + x] = (x < lit) ? kMembraneColour : dim;
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
	bar(kCV1Row, e.getVolts(0) / p.rangeVolts, kCV1Colour);
	bar(kCV2Row, e.getVolts(1) / p.rangeVolts, kCV2Colour);

	PadLEDs::renderingLock = false;
	return true;
}

bool NeuronsView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}
	int32_t lit = static_cast<int32_t>(runner::engine().params().noise * kDisplayHeight + 0.5f);
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (!(whichRows & (1 << y))) {
			continue;
		}
		image[y][kDisplayWidth] = (y < lit) ? kNoiseColour : RGB::monochrome(8);
		if (kSideBarWidth > 1) {
			// gates 1 to 4 from the top
			int32_t gate = kDisplayHeight - 1 - y;
			RGB c = colours::black;
			if (gate >= 0 && gate < kNumGates) {
				bool owned = runner::isGateNeurons(gate);
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

void NeuronsView::pageValueText(int32_t knob, char* out, size_t size) const {
	const Params& p = runner::engine().params();
	// The 7-segment display shows four characters, so every value there is a short tag plus the number:
	// A, b, C, d a neuron's rate in Hz, 1-A the neuron a CV follows (S is the average), r the range in volts,
	// P the weight preset
	bool oled = display->haveOLED();
	auto rateText = [&](int32_t n) {
		int32_t tenths = static_cast<int32_t>(p.rate[n] * 10.0f + 0.5f);
		if (oled) {
			snprintf(out, size, "Neuron %d rate %d.%d Hz", static_cast<int>(n + 1), static_cast<int>(tenths / 10),
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
	case 2: {
		int32_t src = p.cvSource[knob];
		if (oled) {
			if (src >= kNumNeurons) {
				snprintf(out, size, "CV %d follows the average", static_cast<int>(knob + 1));
			}
			else {
				snprintf(out, size, "CV %d follows neuron %d", static_cast<int>(knob + 1), static_cast<int>(src + 1));
			}
		}
		else {
			snprintf(out, size, "%d-%c", static_cast<int>(knob + 1), kLetters[src > kNumNeurons ? kNumNeurons : src]);
		}
		break;
	}
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
			snprintf(out, size, "Weights: %s", NeuronsEngine::presetName(preset_));
		}
		else {
			snprintf(out, size, "P%d", static_cast<int>(preset_ + 1));
		}
		break;
	}
}

void NeuronsView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 6;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 1;
#endif
	canvas.drawStringCentred("Neurons", yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	const Params& p = runner::engine().params();
	char text[64];
	if (!runner::isActive()) {
		snprintf(text, sizeof(text), "off");
	}
	else {
		snprintf(text, sizeof(text), "Leak %d%%  Noise %d%%", static_cast<int>(percent(p.leak)),
		         static_cast<int>(percent(p.noise)));
	}
	canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	char a[40], b[40];
	pageValueText(0, a, sizeof(a));
	pageValueText(1, b, sizeof(b));
	snprintf(text, sizeof(text), "%s", a);
	canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
	yPos += 10;
	snprintf(text, sizeof(text), "%s", b);
	canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
}

void NeuronsView::refreshOLED() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	uiNeedsRendering(this);
}

void NeuronsView::showValuePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->displayPopup(text, 0, true);
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kPopupMs);
	}
}

void NeuronsView::showNamePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->setScrollingText(text);
	}
}

ActionResult NeuronsView::timerCallback() {
	display->cancelPopup();
	return ActionResult::DEALT_WITH;
}

ActionResult NeuronsView::horizontalEncoderAction(int32_t offset) {
	auto& e = runner::engine();
	e.setNoise(e.params().noise + 0.01f * offset);
	char text[24];
	snprintf(text, sizeof(text), display->haveOLED() ? "Noise %d%%" : "n%d", static_cast<int>(percent(e.params().noise)));
	showValuePopup(text);
	refreshOLED();
	return ActionResult::DEALT_WITH;
}

ActionResult NeuronsView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	auto& e = runner::engine();
	e.setLeak(e.params().leak + 0.02f * (offset > 0 ? 1 : -1));
	char text[24];
	snprintf(text, sizeof(text), display->haveOLED() ? "Leak %d%%" : "L%d", static_cast<int>(percent(e.params().leak)));
	showValuePopup(text);
	refreshOLED();
	return ActionResult::DEALT_WITH;
}

void NeuronsView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
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
	case 2:
		e.setCVSource(knob, p.cvSource[knob] + dir);
		break;
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

void NeuronsView::selectEncoderAction(int8_t offset) {
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

ActionResult NeuronsView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
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
				runner::engine().randomiseWeights();
				showNamePopup("Random weights");
			}
			else {
				runner::engine().reset();
				showNamePopup("Restarted");
			}
			refreshOLED();
		}
		return ActionResult::DEALT_WITH;
	}

	if (b == CV) {
		if (on) {
			bool enable = !runner::isActive();
			runner::setAll(enable);
			showNamePopup(!enable ? "Neurons off" : (runner::isActive() ? "Neurons on" : "No free outputs"));
			refreshOLED();
		}
		return ActionResult::DEALT_WITH;
	}

	return ClipView::buttonAction(b, on, inCardRoutine);
}

ActionResult NeuronsView::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (!velocity) {
		return ActionResult::DEALT_WITH;
	}
	auto& e = runner::engine();

	// the noise column: the pad you press becomes the top of the bar, and tapping the top lit pad again zeroes it
	if (x == kDisplayWidth && y >= 0 && y < kDisplayHeight) {
		int32_t lit = static_cast<int32_t>(e.params().noise * kDisplayHeight + 0.5f);
		e.setNoise((lit == y + 1) ? 0.0f : static_cast<float>(y + 1) / kDisplayHeight);
		char text[24];
		snprintf(text, sizeof(text), display->haveOLED() ? "Noise %d%%" : "n%d", static_cast<int>(percent(e.params().noise)));
		showValuePopup(text);
		refreshOLED();
		return ActionResult::DEALT_WITH;
	}

	// tap a weight pad to step it round: nothing, excite, excite strongly, inhibit, inhibit strongly
	int32_t target = kTopRow - y;
	if (x >= 0 && x < kNumNeurons && target >= 0 && target < kNumNeurons && x != target) {
		int32_t level = nextLevel(e.params().weight[target][x]);
		e.setWeight(target, x, level);
		char text[40];
		if (display->haveOLED()) {
			snprintf(text, sizeof(text), "Neuron %d to %d: %s", static_cast<int>(x + 1), static_cast<int>(target + 1),
			         level == 0 ? "none" : (level > 0 ? (level == 2 ? "excite" : "excite weakly")
			                                          : (level == -2 ? "inhibit" : "inhibit weakly")));
		}
		else {
			snprintf(text, sizeof(text), "%c%c%c%d", kLetters[x], kLetters[target], level == 0 ? ' ' : (level > 0 ? '+' : '-'),
			         static_cast<int>(level < 0 ? -level : level));
		}
		showValuePopup(text);
		uiNeedsRendering(this);
	}
	return ActionResult::DEALT_WITH;
}
