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

#include "processing/engines/turing_engine.h"

#include <algorithm>
#include <bit>

namespace deluge::turing {

void TuringEngine::setLength(int32_t length) {
	length_ = std::clamp(length, kMinLength, kMaxLength);
	register_ &= mask();
}

void TuringEngine::setFlipProbability(float probability) {
	// written so that NaN also ends up as 0
	flipProbability_ = (probability > 0.0f) ? std::min(probability, 1.0f) : 0.0f;
}

void TuringEngine::setRegister(uint16_t bits) {
	register_ = bits & mask();
}

void TuringEngine::randomise(uint32_t seed) {
	// xorshift32, then take the high half, which is better mixed than the low bits
	uint32_t x = seed ? seed : 0x9E3779B9u;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	register_ = static_cast<uint16_t>(x >> 16) & mask();
}

bool TuringEngine::clock(float rnd) {
	bool leaving = (register_ >> (length_ - 1)) & 1u;
	bool flip = rnd < flipProbability_;
	bool entering = leaving != flip;
	register_ = static_cast<uint16_t>(((register_ << 1) | (entering ? 1u : 0u)) & mask());
	return flip;
}

int32_t TuringEngine::noteOffsetFromValue(uint8_t value, uint16_t scaleBits, int32_t octaves) {
	scaleBits &= 0xFFF;
	if (scaleBits == 0) {
		scaleBits = 0xFFF;
	}
	octaves = std::clamp(octaves, int32_t{1}, int32_t{8});

	int32_t notesPerOctave = std::popcount(scaleBits);
	int32_t total = notesPerOctave * octaves;
	int32_t index = (static_cast<int32_t>(value) * total) >> 8; // 0 to total - 1
	int32_t octave = index / notesPerOctave;
	int32_t degree = index % notesPerOctave;

	// the degree'th set bit of the scale
	uint16_t bits = scaleBits;
	for (int32_t i = 0; i < degree; i++) {
		bits &= bits - 1; // clear the lowest set bit
	}
	int32_t semitone = std::countr_zero(bits);
	return octave * 12 + semitone;
}

} // namespace deluge::turing
