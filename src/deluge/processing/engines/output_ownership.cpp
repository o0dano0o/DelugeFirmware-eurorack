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

#include "processing/engines/output_ownership.h"

namespace deluge::owners {

namespace {
constexpr uint8_t kCVBits = (1 << kNumCVOutputs) - 1;
constexpr uint8_t kGateBits = (1 << kNumGateOutputs) - 1;
constexpr uint8_t kPairBits = 0b11; // channels 1 and 2 have both a CV and a gate
} // namespace

bool OutputOwnership::holdsPairs(Owner owner) {
	return owner == Owner::TURING || owner == Owner::VOICE;
}

void OutputOwnership::take(bool isGate, int32_t channel, Change& change) {
	Owner& slot = isGate ? gate_[channel] : cv_[channel];
	Owner previous = slot;
	if (previous == Owner::NONE) {
		return;
	}
	slot = Owner::NONE;
	Masks& lost = change.lost[static_cast<int32_t>(previous)];
	(isGate ? lost.gate : lost.cv) |= static_cast<uint8_t>(1 << channel);
	if (holdsPairs(previous)) {
		// the other half of the pair goes with it
		Owner& partner = isGate ? cv_[channel] : gate_[channel];
		if (partner == previous) {
			partner = Owner::NONE;
			(isGate ? lost.cv : lost.gate) |= static_cast<uint8_t>(1 << channel);
		}
	}
}

Change OutputOwnership::claim(Owner owner, uint8_t cvMask, uint8_t gateMask, uint8_t blockedGates) {
	Change change;
	if (owner == Owner::NONE || static_cast<int32_t>(owner) >= kNumOwners) {
		return change;
	}
	cvMask &= kCVBits;
	gateMask &= kGateBits;
	if (holdsPairs(owner)) {
		uint8_t pairs = (cvMask | gateMask) & kPairBits;
		cvMask = pairs;
		gateMask = pairs;
	}
	gateMask &= ~blockedGates;

	// Outputs this owner held and no longer wants go back to NONE; these are its own choice, not a loss
	for (int32_t ch = 0; ch < kNumCVOutputs; ch++) {
		if (cv_[ch] == owner && !(cvMask & (1 << ch))) {
			cv_[ch] = Owner::NONE;
		}
	}
	for (int32_t ch = 0; ch < kNumGateOutputs; ch++) {
		if (gate_[ch] == owner && !(gateMask & (1 << ch))) {
			gate_[ch] = Owner::NONE;
		}
	}

	// Taking from others: first free everything wanted (so a pair owner loses both halves), then assign
	for (int32_t ch = 0; ch < kNumCVOutputs; ch++) {
		if ((cvMask & (1 << ch)) && cv_[ch] != owner) {
			take(false, ch, change);
		}
	}
	for (int32_t ch = 0; ch < kNumGateOutputs; ch++) {
		if ((gateMask & (1 << ch)) && gate_[ch] != owner) {
			take(true, ch, change);
		}
	}
	for (int32_t ch = 0; ch < kNumCVOutputs; ch++) {
		if (cvMask & (1 << ch)) {
			cv_[ch] = owner;
		}
	}
	for (int32_t ch = 0; ch < kNumGateOutputs; ch++) {
		if (gateMask & (1 << ch)) {
			gate_[ch] = owner;
		}
	}
	return change;
}

Change OutputOwnership::release(uint8_t cvMask, uint8_t gateMask) {
	Change change;
	cvMask &= kCVBits;
	gateMask &= kGateBits;
	for (int32_t ch = 0; ch < kNumCVOutputs; ch++) {
		if (cvMask & (1 << ch)) {
			take(false, ch, change);
		}
	}
	for (int32_t ch = 0; ch < kNumGateOutputs; ch++) {
		if (gateMask & (1 << ch)) {
			take(true, ch, change);
		}
	}
	return change;
}

void OutputOwnership::clear() {
	for (Owner& o : cv_) {
		o = Owner::NONE;
	}
	for (Owner& o : gate_) {
		o = Owner::NONE;
	}
}

Owner OutputOwnership::cvOwner(int32_t channel) const {
	return (channel >= 0 && channel < kNumCVOutputs) ? cv_[channel] : Owner::NONE;
}

Owner OutputOwnership::gateOwner(int32_t channel) const {
	return (channel >= 0 && channel < kNumGateOutputs) ? gate_[channel] : Owner::NONE;
}

Masks OutputOwnership::masksOf(Owner owner) const {
	Masks m;
	for (int32_t ch = 0; ch < kNumCVOutputs; ch++) {
		if (cv_[ch] == owner) {
			m.cv |= static_cast<uint8_t>(1 << ch);
		}
	}
	for (int32_t ch = 0; ch < kNumGateOutputs; ch++) {
		if (gate_[ch] == owner) {
			m.gate |= static_cast<uint8_t>(1 << ch);
		}
	}
	return m;
}

Masks OutputOwnership::owned() const {
	Masks m;
	for (int32_t ch = 0; ch < kNumCVOutputs; ch++) {
		if (cv_[ch] != Owner::NONE) {
			m.cv |= static_cast<uint8_t>(1 << ch);
		}
	}
	for (int32_t ch = 0; ch < kNumGateOutputs; ch++) {
		if (gate_[ch] != Owner::NONE) {
			m.gate |= static_cast<uint8_t>(1 << ch);
		}
	}
	return m;
}

} // namespace deluge::owners
