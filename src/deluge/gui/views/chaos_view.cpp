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

#include "gui/views/chaos_view.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/ui/ui.h"
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
#include "processing/engines/chaos_runner.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace deluge;
using namespace deluge::chaos;

ChaosView chaosView{};

namespace {

constexpr uint32_t kFrameSamples = 3528; ///< redraw about every 80 ms (44.1 kHz audio clock)
constexpr float kFrameSeconds = 0.08f;
/// A trail fades to 10% brightness over this fraction of the sloth's loop time
constexpr float kTrailLoopFraction = 0.25f;

const RGB kSlothColour[3] = {RGB{255, 70, 60}, RGB{70, 230, 90}, RGB{70, 130, 255}};
const RGB kCVColour[2] = {RGB{0, 210, 225}, RGB{225, 70, 205}};
char const* const kModelNames[] = {"Lorenz", "Rossler", "Thomas"};
char const* const kViewNames[] = {"All", "Sloth A", "Sloth B", "Sloth C"};

int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
	return std::max(lo, std::min(hi, v));
}

RGB scaled(RGB c, float k) {
	return RGB{static_cast<uint8_t>(std::min(255.0f, c.r * k)), static_cast<uint8_t>(std::min(255.0f, c.g * k)),
	           static_cast<uint8_t>(std::min(255.0f, c.b * k))};
}

} // namespace

bool ChaosView::opened() {
	if (!runner::isActive()) {
		runner::setCVChaos(0, true);
		runner::setCVChaos(1, true);
	}
	focusRegained();
	return true;
}

void ChaosView::focusRegained() {
	ClipView::focusRegained();

	Clip* clip = getCurrentClip();
	if (clip) {
		view.focusRegained();
		view.setActiveModControllableTimelineCounter(clip);
	}

	// The clip view's playhead LED column is only updated by that view
	PadLEDs::clearTickSquares();

	lastFrameTimer_ = AudioEngine::audioSampleTimer;

	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		display->setScrollingText("CHAOS");
	}
	uiNeedsRendering(this);
}

float ChaosView::periodSeconds(int32_t sloth) const {
	ChaosEngine& e = runner::engine();
	float p = e.getPeriod(sloth);
	if (e.getTimeMode() == TimeMode::TEMPO) {
		p = p * 60.0f / std::max(runner::getBpm(), 1.0f);
	}
	return std::max(p, 0.5f);
}

void ChaosView::graphicsRoutine() {
	uint32_t now = AudioEngine::audioSampleTimer;
	if (now - lastFrameTimer_ < kFrameSamples) {
		return;
	}
	lastFrameTimer_ = now;

	ChaosEngine& e = runner::engine();
	for (int32_t s = 0; s < 3; s++) {
		// fade
		float fade = std::exp(-2.3026f * kFrameSeconds / (kTrailLoopFraction * periodSeconds(s)));
		for (auto& row : trail_[s]) {
			for (float& v : row) {
				v *= fade;
			}
		}
		// stamp the pad under the sloth: x picks the column, z the row (high z at the top)
		float xn = e.getNormalised(s, 0);
		float zn = e.getNormalised(s, 2);
		headCol_[s] = clampi(static_cast<int32_t>((xn + 1.0f) * 0.5f * kDisplayWidth), 0, kDisplayWidth - 1);
		headRow_[s] = clampi(static_cast<int32_t>((zn + 1.0f) * 0.5f * kDisplayHeight), 0, kDisplayHeight - 1);
		trail_[s][headRow_[s]][headCol_[s]] = 1.0f;
	}
	uiNeedsRendering(this);
}

bool ChaosView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                               uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	if (!image) {
		return true;
	}
	PadLEDs::renderingLock = true;

	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (!(whichRows & (1 << y))) {
			continue;
		}
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			float r = 0, g = 0, b = 0;
			for (int32_t s = 0; s < 3; s++) {
				if (view_ != 0 && view_ != s + 1) {
					continue;
				}
				float k = trail_[s][y][x];
				// a faint floor so a long trail is still visible
				k = k * k * 0.9f;
				r += kSlothColour[s].r * k;
				g += kSlothColour[s].g * k;
				b += kSlothColour[s].b * k;
			}
			RGB c{static_cast<uint8_t>(std::min(255.0f, r)), static_cast<uint8_t>(std::min(255.0f, g)),
			      static_cast<uint8_t>(std::min(255.0f, b))};
			image[y][x] = c;
		}
		if (occupancyMask) {
			memset(occupancyMask[y], 64, kDisplayWidth);
		}
	}

	// heads on top, in white
	for (int32_t s = 0; s < 3; s++) {
		if (view_ != 0 && view_ != s + 1) {
			continue;
		}
		if (whichRows & (1 << headRow_[s])) {
			image[headRow_[s]][headCol_[s]] = RGB::monochrome(255);
		}
	}

	PadLEDs::renderingLock = false;
	return true;
}

bool ChaosView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                              uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}
	ChaosEngine& e = runner::engine();
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (!(whichRows & (1 << y))) {
			continue;
		}
		for (int32_t c = 0; c < kNumCVOutputs && c < kSideBarWidth; c++) {
			int32_t lit = static_cast<int32_t>(e.getCVVolts(c) / 10.0f * kDisplayHeight + 0.5f);
			image[y][kDisplayWidth + c] = (y < lit) ? scaled(kCVColour[c], 0.8f) : RGB::monochrome(8);
		}
		if (occupancyMask) {
			memset(occupancyMask[y] + kDisplayWidth, 0, kSideBarWidth);
		}
	}
	return true;
}

void ChaosView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 6;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 1;
#endif
	canvas.drawStringCentred("Chaos", yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	char text[40];
	ChaosEngine& e = runner::engine();
	snprintf(text, sizeof(text), "%s  view: %s", kModelNames[static_cast<int32_t>(e.getModel())], kViewNames[view_]);
	canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	bool tempo = e.getTimeMode() == TimeMode::TEMPO;
	snprintf(text, sizeof(text), "%.0f %.0f %.0f %s", e.getPeriod(0), e.getPeriod(1), e.getPeriod(2),
	         tempo ? "beats" : "s");
	canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
}

void ChaosView::showPopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->displayPopup(text);
	}
}

void ChaosView::changePeriod(int32_t sloth, int32_t offset) {
	ChaosEngine& e = runner::engine();
	bool tempo = e.getTimeMode() == TimeMode::TEMPO;
	float p = e.getPeriod(sloth) * std::pow(1.04f, static_cast<float>(offset));
	p = std::max(tempo ? 1.0f : 1.0f, std::min(p, tempo ? 2000.0f : 1000.0f));
	e.setPeriod(sloth, p);

	char text[24];
	snprintf(text, sizeof(text), display->haveOLED() ? "%c: %.1f %s" : "%c %.0f", 'A' + sloth, p,
	         tempo ? "beats" : "s");
	showPopup(text);
	if (display->haveOLED()) {
		renderUIsForOled();
	}
}

void ChaosView::changeCoupling(int32_t offset) {
	ChaosEngine& e = runner::engine();
	float c = std::max(0.0f, std::min(e.getCoupling() + 0.005f * offset, 0.3f));
	e.setCoupling(c);
	char text[24];
	snprintf(text, sizeof(text), display->haveOLED() ? "Coupling %.3f" : "C%.0f", display->haveOLED() ? c : c * 1000);
	showPopup(text);
}

void ChaosView::selectEncoderAction(int8_t offset) {
	constexpr int32_t n = static_cast<int32_t>(Model::kNumModels);
	int32_t m = (static_cast<int32_t>(runner::engine().getModel()) + offset % n + n) % n;
	runner::engine().setModel(static_cast<Model>(m));
	for (auto& sloth : trail_) {
		for (auto& row : sloth) {
			for (float& v : row) {
				v = 0.0f;
			}
		}
	}
	showPopup(kModelNames[m]);
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	uiNeedsRendering(this);
}

ActionResult ChaosView::horizontalEncoderAction(int32_t offset) {
	changePeriod(0, offset);
	return ActionResult::DEALT_WITH;
}

ActionResult ChaosView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	changePeriod(1, offset);
	return ActionResult::DEALT_WITH;
}

void ChaosView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (whichModEncoder == 0) {
		changePeriod(2, offset);
	}
	else if (whichModEncoder == 1) {
		changeCoupling(offset);
	}
}

ActionResult ChaosView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
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
	return ClipView::buttonAction(b, on, inCardRoutine);
}

ActionResult ChaosView::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (velocity && x < kDisplayWidth) {
		view_ = (view_ + 1) % kNumViews;
		showPopup(kViewNames[view_]);
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		uiNeedsRendering(this);
	}
	return ActionResult::DEALT_WITH;
}
