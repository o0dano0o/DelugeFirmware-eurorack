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

#pragma once

#include <cstdint>

namespace deluge::turing {

constexpr int32_t kMinLength = 2;
constexpr int32_t kMaxLength = 16;

/// A "Turing machine" looping random sequencer: a shift register of up to 16 bits.
///
/// Each clock the bits shift one place and a new bit enters. The new bit is the bit that has just fallen off the end,
/// either unchanged or flipped. The flip probability decides which:
///   0    the sequence is locked and loops forever
///   0.5  every new bit is random, so the sequence is constantly new
///   1    every bit is inverted as it comes round, so the sequence repeats at twice its length with inverted halves
/// In between, the loop mutates more or less slowly.
///
/// The low 8 bits of the register are read as a number (0 to 255), which the caller turns into a voltage or a note.
/// This class is pure computation: the caller supplies the random number for each clock, so it can come from an RNG or
/// from something else such as the chaos engine.
class TuringEngine {
public:
	/// Number of steps in the loop, clamped to 2 to 16. The register is masked to this length.
	void setLength(int32_t length);
	int32_t getLength() const { return length_; }

	/// Probability (0 to 1) that the bit coming round is flipped
	void setFlipProbability(float probability);
	float getFlipProbability() const { return flipProbability_; }

	/// Replaces the register, masked to the current length
	void setRegister(uint16_t bits);
	uint16_t getRegister() const { return register_; }

	/// Fills the register with pseudo-random bits from a seed (the same seed gives the same bits)
	void randomise(uint32_t seed);

	/// Advances one step. `rnd` is a random number in [0, 1); the new bit is flipped if it is below the flip
	/// probability. Returns true if the bit was flipped.
	bool clock(float rnd);

	/// The low 8 bits of the register, 0 to 255
	uint8_t getValue() const { return static_cast<uint8_t>(register_ & 0xFF); }

	/// Turns a value into a note offset in semitones above the root. `scaleBits` has bit n set if semitone n of the
	/// octave belongs to the scale (bit 0 is the root); an empty set means all twelve semitones. The value is spread
	/// evenly over `octaves` octaves of scale notes, so 0 is the root and 255 is just under `octaves` octaves up.
	static int32_t noteOffsetFromValue(uint8_t value, uint16_t scaleBits, int32_t octaves);

private:
	uint16_t mask() const { return static_cast<uint16_t>((1u << length_) - 1u); }

	int32_t length_{16};
	float flipProbability_{0.0f};
	uint16_t register_{0};
};

} // namespace deluge::turing
