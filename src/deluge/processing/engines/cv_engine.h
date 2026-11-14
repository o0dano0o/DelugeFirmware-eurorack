/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "model/drum/gate_drum.h"
#include "processing/engines/output_ownership.h"
#include <cstdint>

#define WHICH_GATE_OUTPUT_IS_RUN 2
#define WHICH_GATE_OUTPUT_IS_CLOCK 3

const uint8_t gatePort[] = {2, 2, 2, 4};
const uint8_t gatePin[] = {7, 8, 9, 0};

class CVChannel {
public:
	CVChannel() {
		noteCurrentlyPlaying = -32768;
		voltsPerOctave = 100;
		transpose = 0;
		cents = 0;
		pitchBend = 0;
	}
	int16_t noteCurrentlyPlaying;
	uint8_t voltsPerOctave;
	int8_t transpose;
	int8_t cents;
	int32_t
	    pitchBend; // (1 << 23) represents one semitone. So full 32-bit range can be +-256 semitones. This is different
	               // to the equivalent calculation in Voice, which needs to get things into a number of octaves.
};

class GateChannel {
public:
	GateChannel() { on = false; }
	bool on; // Means either on now, or "awaiting" switch-on
	GateType mode;
	uint32_t timeLastSwitchedOff;
};

class CVEngine {
public:
	CVEngine();
	void init();
	void sendNote(bool on, uint8_t channel, int16_t note = -32768);
	void setGateType(uint8_t whichGate, GateType value);
	void setCVVoltsPerOctave(uint8_t channel, uint8_t value);
	void setCVTranspose(uint8_t channel, int32_t semitones, int32_t cents);
	void setCVPitchBend(uint8_t channel, int32_t value, bool outputToo = true);
	int32_t calculateVoltage(int32_t note, uint8_t channel);
	void physicallySwitchGate(int32_t channel);
	// defer updating the gate while CV is pending and do it when it's done
	void cvOutUpdated();

	void analogOutTick();
	void playbackBegun();
	void playbackEnded();
	/// toggles clock, does not physically update until updateGateOutputs called
	void updateClockOutput();
	void updateRunOutput();
	bool isTriggerClockOutputEnabled();
	/// physically send all gate outs if any output pending
	void updateGateOutputs();

	bool isGatePending() const { return gateOutputPending; }
	bool isRunPending() const { return asapGateOutputPending; }
	bool isClockPending() const { return clockOutputPending; }
	bool isAnythingButRunPending() const { return isGatePending() || isClockPending(); }
	bool isAnythingPending() const { return isGatePending() || isClockPending() || isRunPending(); }
	GateChannel gateChannels[NUM_GATE_CHANNELS];

	CVChannel cvChannels[NUM_PHYSICAL_CV_CHANNELS];

	uint8_t minGateOffTime; // in 100uS's

	bool clockState;

	// When one or more note-on is pending, this is the latest time that one of them last switched off.
	// But it seems I only use this very coarsely - more to see if we're still in the same audio frame than to measure
	// time exactly. This could be improved.
	uint32_t mostRecentSwitchOffTimeOfPendingNoteOn;

	void sendVoltageOut(uint8_t channel, uint16_t voltage);

	/// Generator ownership. Each CV channel and gate has one owner: the normal note path (Owner::NONE) or a generator
	/// (chaos, Turing machine, voice tracker, ...). While a generator owns an output, note-ons and note-offs no longer
	/// move it and only that generator's sendOwned...() calls do. Handing an output back restores the voltage of the
	/// note last played on a CV, and switches a gate off.
	///
	/// Gives `owner` exactly these outputs, taking them from whoever held them (their loss handlers are called).
	/// Gates set to SPECIAL (run/clock) can't be owned. Check what was granted with getOwnedCV(owner) afterwards.
	void claimOutputs(deluge::owners::Owner owner, uint8_t cvMask, uint8_t gateMask);
	/// Hands outputs back to the note path, whoever owned them
	void releaseOutputs(uint8_t cvMask, uint8_t gateMask);
	deluge::owners::Owner cvOwner(uint8_t channel) const { return ownership_.cvOwner(channel); }
	deluge::owners::Owner gateOwner(uint8_t channel) const { return ownership_.gateOwner(channel); }
	/// Outputs owned by `owner` (bit n is channel n), or by any generator
	uint8_t getOwnedCV(deluge::owners::Owner owner) const { return ownership_.masksOf(owner).cv; }
	uint8_t getOwnedGates(deluge::owners::Owner owner) const { return ownership_.masksOf(owner).gate; }
	uint8_t getOwnedCV() const { return ownedCV_; }
	uint8_t getOwnedGates() const { return ownedGate_; }
	/// Called when an owner has outputs taken away by someone else. The outputs are already reassigned, so the
	/// generator should just stop driving them (it may call claimOutputs/releaseOutputs from inside).
	using LossHandler = void (*)(uint8_t cvLost, uint8_t gateLost);
	void setLossHandler(deluge::owners::Owner owner, LossHandler handler);

	/// Writes a DAC code to a CV channel that `owner` owns, skipping the write if the value hasn't changed. Ignored if
	/// someone else owns the channel.
	void sendOwnedVoltage(deluge::owners::Owner owner, uint8_t channel, uint16_t code);
	/// Moves a gate that `owner` owns. Ignored if someone else owns the gate.
	void sendOwnedGate(deluge::owners::Owner owner, uint8_t channel, bool high);

	/// Output mutes (set from the Eurorack overview). A muted gate stays low whatever drives it. A muted CV freezes at
	/// the voltage it had; the latest requested voltage is applied again when it is unmuted.
	void setMutedGates(uint8_t mask);
	void setMutedCV(uint8_t mask);
	uint8_t getMutedGates() const { return mutedGates_; }
	uint8_t getMutedCV() const { return mutedCV_; }

	/// CV quantiser (set from the Eurorack overview): snaps the voltage of the chaos, Turing and voice outputs on a CV
	/// channel to the notes of the song's scale
	void setQuantisedCV(uint8_t mask);
	uint8_t getQuantisedCV() const { return quantisedCV_; }

	/// For displays: the last DAC code written to a CV channel (0 to 65535, 6552 per volt)
	uint16_t getLastVoltageCode(uint8_t channel) const { return lastVoltageCode_[channel]; }
	/// For displays: true while a gate is high, or for `holdMs` after it last went high (triggers are short)
	bool gateRecentlyOn(uint8_t channel, int32_t holdMs) const;
	/// Counts every time a gate goes high (not counting mutes), for things that follow another gate's triggers
	uint32_t gateRiseCount(uint8_t channel) const { return channel < NUM_GATE_CHANNELS ? gateRises_[channel] : 0; }

	inline bool isNoteOn(int32_t channel, int32_t note) {
		return (gateChannels[channel].on && cvChannels[channel].noteCurrentlyPlaying == note);
	}

private:
	void recalculateCVChannelVoltage(uint8_t channel);
	void switchGateOff(int32_t channel);
	void switchGateOn(int32_t channel, int32_t doInstantlyIfPossible = false);
	/// signifies there's a gate that can't go until the cv is output
	bool cvOutPending{false};
	void applyOwnership(const deluge::owners::Change& change);
	deluge::owners::OutputOwnership ownership_;
	LossHandler lossHandlers_[deluge::owners::kNumOwners] = {};
	uint8_t ownedCV_{0};   ///< all generator-owned CV channels, kept in step with ownership_ for the note path
	uint8_t ownedGate_{0}; ///< same for gates
	int32_t lastOwnedCode_[NUM_PHYSICAL_CV_CHANNELS] = {-1, -1};
	uint16_t lastVoltageCode_[NUM_PHYSICAL_CV_CHANNELS] = {0, 0};
	uint16_t wantedVoltageCode_[NUM_PHYSICAL_CV_CHANNELS] = {0, 0};
	uint8_t mutedGates_ = 0;
	uint8_t mutedCV_ = 0;
	uint8_t quantisedCV_ = 0;
	uint16_t quantiseCode(uint8_t channel, uint16_t code) const;
	uint32_t gateLastOnTime_[NUM_GATE_CHANNELS] = {};
	bool gateEverOn_[NUM_GATE_CHANNELS] = {};
	uint32_t gateRises_[NUM_GATE_CHANNELS] = {};
	bool gateWasHigh_[NUM_GATE_CHANNELS] = {};
	/// gate 1-4 as synths or drums
	bool gateOutputPending{false};
	/// gate 3 as a run signal
	bool asapGateOutputPending;
	/// gate 4 as a clock signal
	bool clockOutputPending;
};

extern CVEngine cvEngine;
