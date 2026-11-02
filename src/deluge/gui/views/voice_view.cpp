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

#include "gui/views/voice_view.h"
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
#include "processing/engines/voice_runner.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace deluge;
using namespace gui;
using namespace deluge::voice;

VoiceView voiceView{};

namespace {

constexpr int32_t kPopupMs = 800;
constexpr int32_t kLevelRow = kDisplayHeight - 1;
constexpr int32_t kNoteRow = kDisplayHeight - 2;

const RGB kLevelColour{40, 220, 90};
const RGB kLevelPeak{255, 200, 40};
const RGB kNoteDim{14, 14, 20};
const RGB kNoteSharpDim{6, 6, 10};
const RGB kNoteLit{255, 255, 255};
const RGB kGateColour{255, 60, 60};
const RGB kOctaveColour{60, 140, 255};

char const* const kNoteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

bool isBlackKey(int32_t pitchClass) {
	return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
}

/// 0 to kDisplayWidth lit pads for an RMS level, on a log scale from -60 dB to 0 dB
int32_t levelPads(float level) {
	if (level < 0.001f) {
		return 0;
	}
	float db = 20.0f * std::log10(level);
	float t = (db + 60.0f) / 60.0f;
	return std::max<int32_t>(0, std::min<int32_t>(static_cast<int32_t>(t * kDisplayWidth + 0.5f), kDisplayWidth));
}

} // namespace

bool VoiceView::opened() {
	focusRegained();
	return true;
}

void VoiceView::focusRegained() {
	ClipView::focusRegained();

	Clip* clip = getCurrentClip();
	if (clip) {
		view.focusRegained();
		view.setActiveModControllableTimelineCounter(clip);
	}
	PadLEDs::clearTickSquares();
	lastDrawnNote_ = -2;

	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		display->setScrollingText(runner::isActive() ? "VOICE" : "VOICE OFF  CV TO START");
	}
	uiNeedsRendering(this);
}

void VoiceView::graphicsRoutine() {
	int32_t levelNow = levelPads(runner::currentLevel());
	int32_t noteNow = runner::recentlyVoiced() ? runner::currentNote() : -1;
	bool gate = runner::gateOn();
	if (levelNow != lastDrawnLevel_ || noteNow != lastDrawnNote_ || gate != lastDrawnGate_) {
		lastDrawnLevel_ = levelNow;
		lastDrawnNote_ = noteNow;
		lastDrawnGate_ = gate;
		uiNeedsRendering(this);
	}
}

bool VoiceView::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
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

	if (whichRows & (1 << kLevelRow)) {
		int32_t lit = levelPads(runner::currentLevel());
		for (int32_t x = 0; x < lit; x++) {
			image[kLevelRow][x] = (x >= kDisplayWidth - 3) ? kLevelPeak : kLevelColour;
		}
	}

	if (whichRows & (1 << kNoteRow)) {
		int32_t note = runner::recentlyVoiced() ? runner::currentNote() : -1;
		for (int32_t pc = 0; pc < 12; pc++) {
			image[kNoteRow][pc] = isBlackKey(pc) ? kNoteSharpDim : kNoteDim;
		}
		if (note >= 0) {
			int32_t pc = note % 12;
			image[kNoteRow][pc] = runner::gateOn() ? kNoteLit : RGB{120, 120, 140};
			// octave: which of the four right-hand pads, starting at C1
			int32_t octave = std::max<int32_t>(0, std::min<int32_t>(note / 12 - 2, 3));
			for (int32_t o = 0; o <= octave; o++) {
				image[kNoteRow][12 + o] = kOctaveColour;
			}
		}
	}

	PadLEDs::renderingLock = false;
	return true;
}

bool VoiceView::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                              uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!image) {
		return true;
	}
	bool gate = runner::gateOn();
	int32_t sensitivity = runner::getSettings().sensitivity;
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (!(whichRows & (1 << y))) {
			continue;
		}
		image[y][kDisplayWidth] = gate ? kGateColour : RGB::monochrome(8);
		if (kSideBarWidth > 1) {
			// sensitivity: bottom is least sensitive
			image[y][kDisplayWidth + 1] = (y <= sensitivity) ? kLevelColour : RGB::monochrome(8);
		}
		if (occupancyMask) {
			memset(occupancyMask[y] + kDisplayWidth, 0, kSideBarWidth);
		}
	}
	return true;
}

void VoiceView::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 6;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 1;
#endif
	canvas.drawStringCentred("Voice", yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	char text[40];
	const auto& s = runner::getSettings();
	if (s.channel < 0) {
		snprintf(text, sizeof(text), "off");
	}
	else {
		snprintf(text, sizeof(text), "CV/gate %d  %s", static_cast<int>(s.channel + 1), s.quantise ? "notes" : "glide");
	}
	canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
	yPos += 12;

	int32_t note = runner::recentlyVoiced() ? runner::currentNote() : -1;
	if (note >= 0) {
		snprintf(text, sizeof(text), "%s%d  shift %+d  sens %d", kNoteNames[note % 12], static_cast<int>(note / 12 - 1),
		         static_cast<int>(s.octaveShift), static_cast<int>(s.sensitivity + 1));
	}
	else {
		snprintf(text, sizeof(text), "-  shift %+d  sens %d", static_cast<int>(s.octaveShift),
		         static_cast<int>(s.sensitivity + 1));
	}
	canvas.drawStringCentred(text, yPos, kTextSpacingX, kTextSpacingY);
}

void VoiceView::refreshOLED() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	uiNeedsRendering(this);
}

void VoiceView::showValuePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->displayPopup(text, 0, true);
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kPopupMs);
	}
}

void VoiceView::showNamePopup(char const* text) {
	if (display->haveOLED()) {
		display->popupTextTemporary(text);
	}
	else {
		display->setScrollingText(text);
	}
}

ActionResult VoiceView::timerCallback() {
	display->cancelPopup();
	return ActionResult::DEALT_WITH;
}

ActionResult VoiceView::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	runner::setOctaveShift(runner::getSettings().octaveShift + (offset > 0 ? 1 : -1));
	char text[24];
	snprintf(text, sizeof(text), display->haveOLED() ? "Octave shift %+d" : "O%+d",
	         static_cast<int>(runner::getSettings().octaveShift));
	showValuePopup(text);
	refreshOLED();
	return ActionResult::DEALT_WITH;
}

void VoiceView::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (whichModEncoder == 0) {
		runner::setOctaveShift(runner::getSettings().octaveShift + (offset > 0 ? 1 : -1));
		char text[24];
		snprintf(text, sizeof(text), display->haveOLED() ? "Octave shift %+d" : "O%+d",
		         static_cast<int>(runner::getSettings().octaveShift));
		showValuePopup(text);
		refreshOLED();
	}
}

void VoiceView::selectEncoderAction(int8_t offset) {
	runner::setSensitivity(runner::getSettings().sensitivity + (offset > 0 ? 1 : -1));
	char text[24];
	snprintf(text, sizeof(text), display->haveOLED() ? "Sensitivity %d" : "S%d",
	         static_cast<int>(runner::getSettings().sensitivity + 1));
	showValuePopup(text);
	refreshOLED();
}

ActionResult VoiceView::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
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
			bool quantise = !runner::getSettings().quantise;
			runner::setQuantise(quantise);
			showNamePopup(quantise ? "Snap to notes" : "Glide with voice");
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
				snprintf(text, sizeof(text), "Voice off");
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
