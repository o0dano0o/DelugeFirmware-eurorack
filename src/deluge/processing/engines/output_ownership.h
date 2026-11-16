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

/// Who drives each physical output (2 CV, 4 gates). Pure bookkeeping with no hardware calls, so it can be tested on the
/// host. Every output has exactly one owner: NONE means the normal note path (Euclidean patterns, clips), anything else
/// is a generator that writes the output itself.
namespace deluge::owners {

enum class Owner : uint8_t { NONE = 0, CHAOS, TURING, VOICE, MARBLES, RUNGLER, NEURONS, KURAMOTO, kCount };

constexpr int32_t kNumOwners = static_cast<int32_t>(Owner::kCount);
constexpr int32_t kNumCVOutputs = 2;
constexpr int32_t kNumGateOutputs = 4;

/// Bit n is channel n (CV n or gate n)
struct Masks {
	uint8_t cv = 0;
	uint8_t gate = 0;
	bool any() const { return cv || gate; }
};

/// What each owner lost in one change. Only outputs taken away from an owner are listed, not ones it gave up itself.
struct Change {
	Masks lost[kNumOwners];
	bool any() const {
		for (const Masks& m : lost) {
			if (m.any()) {
				return true;
			}
		}
		return false;
	}
};

class OutputOwnership {
public:
	/// Owners that hold a CV and its gate as a pair (Turing machine, voice tracker): they only use channels 1 and 2, and
	/// losing either half of a channel loses both
	static bool holdsPairs(Owner owner);

	/// Sets what `owner` holds to exactly these outputs, taking them from whoever had them. Outputs it held before and
	/// isn't asking for any more go back to NONE. `owner` can't be NONE (use release). For a pair owner the CV and gate
	/// masks are merged to whole channels 1 and 2. Gates in `blockedGates` (the system's run and clock gates) are never
	/// granted; a pair owner then gets just the CV of that channel.
	Change claim(Owner owner, uint8_t cvMask, uint8_t gateMask, uint8_t blockedGates = 0);

	/// Hands these outputs back to the note path, whoever had them
	Change release(uint8_t cvMask, uint8_t gateMask);

	/// Everything back to NONE
	void clear();

	Owner cvOwner(int32_t channel) const;
	Owner gateOwner(int32_t channel) const;
	Masks masksOf(Owner owner) const;
	/// Outputs held by any owner other than NONE
	Masks owned() const;

private:
	/// Takes an output from its owner (to NONE) and records the loss; for a pair owner the other half goes too
	void take(bool isGate, int32_t channel, Change& change);

	Owner cv_[kNumCVOutputs] = {Owner::NONE, Owner::NONE};
	Owner gate_[kNumGateOutputs] = {Owner::NONE, Owner::NONE, Owner::NONE, Owner::NONE};
};

} // namespace deluge::owners
