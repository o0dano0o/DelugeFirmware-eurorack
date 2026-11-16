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

#include "processing/engines/marbles_engine.h"
#include <cstdint>

/// Four leaky integrate-and-fire neurons that excite or inhibit each other.
///
/// Each neuron has a membrane value that rises towards its threshold (1.0), leaks back down, and gets a little noise.
/// When it reaches the threshold it fires: its gate pulses, its membrane resets to 0, and every other neuron gets a
/// kick from the weight that connects them (positive pushes it towards firing, negative holds it back). Rhythms come
/// from the weights, not from a pattern.
///
/// A neuron's "rate" is how often it fires on its own, with nothing connected and no noise. The engine works out the
/// input current that gives exactly that rate for the chosen leak, so the number means what it says.
///
/// Outputs:
///   gates 1 to 4  a short pulse each time neuron 1 to 4 fires
///   CV 1 and CV 2 the membrane value of a chosen neuron, or the average of all four
///
/// Pure compute with no hardware calls, so it can be tested on the host.
namespace deluge::neurons {

using marbles::TimeMode;

constexpr int32_t kNumNeurons = 4;
constexpr int32_t kNumCV = 2;
constexpr int32_t kNumGates = 4;
constexpr int32_t kNumPresets = 5;
constexpr int32_t kAverageSource = kNumNeurons; ///< CV source value meaning "average of all neurons"
constexpr int32_t kMaxWeightLevel = 2;          ///< weights are stored as -2 to 2 (a step of 0.5)

struct Params {
	float rate[kNumNeurons] = {2.0f, 3.0f, 4.5f, 1.5f}; ///< Hz each neuron fires at on its own (0.1 to 16)
	int8_t weight[kNumNeurons][kNumNeurons] = {};       ///< weight[target][source] in steps of 0.5, -2 to 2
	float leak = 0.3f;                                  ///< 0 (perfect integrator) to 1
	float noise = 0.1f;                                 ///< 0 to 1
	float rangeVolts = 5.0f;                            ///< outputs reach this many volts at most (1 to 10)
	TimeMode timeMode = TimeMode::FREE;                 ///< TEMPO makes time run at bpm / 120
	uint8_t cvSource[kNumCV] = {0, kAverageSource};     ///< neuron 0 to 3, or kAverageSource
};

class NeuronsEngine {
public:
	NeuronsEngine() { reset(); }

	/// Back to the start: same random numbers, same starting membranes
	void reset();

	const Params& params() const { return params_; }
	void setParams(const Params& p);
	void setRate(int32_t neuron, float hz);
	void setWeight(int32_t target, int32_t source, int32_t level);
	void setLeak(float v);
	void setNoise(float v);
	void setRange(float volts);
	void setTimeMode(TimeMode m);
	void setCVSource(int32_t cv, int32_t source);
	/// 0 independent, 1 ring, 2 pairs inhibit, 3 cascade, 4 everyone excites everyone. Replaces all weights.
	void applyPreset(int32_t preset);
	static char const* presetName(int32_t preset);
	/// Fills the weights with random values
	void randomiseWeights();

	/// Advances by dt seconds. bpm is the song tempo, used in TEMPO mode.
	void step(float dt, float bpm);

	float getMembrane(int32_t neuron) const;
	uint32_t getSpikeCount() const { return spikeCount_; }
	uint32_t getSpikes(int32_t neuron) const;
	float getVolts(int32_t cv) const;
	uint16_t getCVCode(int32_t cv) const;
	/// Bit n is gate n+1
	uint8_t getGateLevels() const;

private:
	uint32_t nextRandom();
	float random01();
	float gaussian();

	Params params_;
	uint32_t rng_ = 1;
	float v_[kNumNeurons] = {};
	float pulse_[kNumNeurons] = {};
	uint32_t spikes_[kNumNeurons] = {};
	uint32_t spikeCount_ = 0;
};

} // namespace deluge::neurons
