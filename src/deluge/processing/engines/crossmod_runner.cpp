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

#include "processing/engines/crossmod_runner.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/chaos_engine.h"
#include "processing/engines/chaos_runner.h"
#include "processing/engines/marbles_runner.h"
#include "processing/engines/neurons_runner.h"
#include "processing/engines/rungler_runner.h"
#include "processing/engines/turing_runner.h"
#include <algorithm>

namespace deluge::crossmod::runner {

namespace {

CrossmodEngine crossmodEngine;

constexpr float kChaosCouplingMax = 0.3f;
constexpr uint32_t kHitDecaySamples = 13230; ///< a hit fades over about 0.3 s
constexpr int32_t kNumHitGates = 4;

uint32_t lastHit[kNumHitGates] = {};
bool everHit[kNumHitGates] = {};

/// 0.5 when idle, jumps to 1 on a hit and fades back to 0.5, so an idle gate has no effect on its target
float hitLevel(int32_t gate) {
	if (!everHit[gate]) {
		return 0.5f;
	}
	uint32_t age = AudioEngine::audioSampleTimer - lastHit[gate];
	if (age >= kHitDecaySamples) {
		return 0.5f;
	}
	return 0.5f + 0.5f * (1.0f - static_cast<float>(age) / static_cast<float>(kHitDecaySamples));
}

float clamp01(float v) {
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

struct Info {
	char const* name;
	char const* tag;
	int32_t family;
};

// The order here is what is saved in songs, so only add to the end
const Info kSources[kNumSources] = {
    {"Chaos A", "CxA", 0},     {"Chaos B", "CxB", 0},     {"Chaos C", "CxC", 0},     {"Marbles X1", "M-1", 1},
    {"Marbles X2", "M-2", 1},  {"Rungler CV 1", "r-1", 2}, {"Rungler CV 2", "r-2", 2}, {"Neuron 1", "n-1", 3},
    {"Neuron 2", "n-2", 3},    {"Neuron 3", "n-3", 3},    {"Neuron 4", "n-4", 3},    {"Neurons average", "n-A", 3},
    {"Turing note", "tn", 4},     {"Euclid any hit", "EAny", 5}, {"Euclid gate 1 hits", "E-1", 5}, {"Euclid gate 2 hits", "E-2", 5},
};

const Info kTargets[kNumTargets] = {
    {"Marbles deja vu", "Mdv", 1},    {"Marbles spread", "MSPr", 1},   {"Marbles bias", "Mbi", 1},
    {"Marbles steps", "MSt", 1},      {"Marbles jitter", "MJt", 1},    {"Marbles Bernoulli", "Mbn", 1},
    {"Rungler chance", "rCh", 2},     {"Rungler mod", "rMd", 2},       {"Rungler slew", "rSl", 2},
    {"Turing flip chance", "tFP", 4}, {"Neurons noise", "nNo", 3},     {"Neurons leak", "nLk", 3},
    {"Chaos coupling", "CCp", 0}, {"Euclid chaos depth", "EdEP", 5},
};

} // namespace

char const* sourceName(int32_t s) {
	return (s >= 0 && s < kNumSources) ? kSources[s].name : "none";
}
char const* sourceTag(int32_t s) {
	return (s >= 0 && s < kNumSources) ? kSources[s].tag : "oFF";
}
char const* targetName(int32_t t) {
	return (t >= 0 && t < kNumTargets) ? kTargets[t].name : "none";
}
char const* targetTag(int32_t t) {
	return (t >= 0 && t < kNumTargets) ? kTargets[t].tag : "oFF";
}
int32_t sourceFamily(int32_t s) {
	return (s >= 0 && s < kNumSources) ? kSources[s].family : 0;
}
int32_t targetFamily(int32_t t) {
	return (t >= 0 && t < kNumTargets) ? kTargets[t].family : 0;
}

namespace {
bool familyRunning(int32_t family) {
	switch (family) {
	case 0:
		return deluge::chaos::runner::isActive();
	case 1:
		return deluge::marbles::runner::isActive();
	case 2:
		return deluge::rungler::runner::isActive();
	case 3:
		return deluge::neurons::runner::isActive();
	case 4:
		return deluge::turing::runner::isActive();
	default:
		return true; // Euclid is always there
	}
}
} // namespace

bool sourceRunning(int32_t s) {
	return s >= 0 && s < kNumSources && familyRunning(kSources[s].family);
}
bool targetRunning(int32_t t) {
	return t >= 0 && t < kNumTargets && familyRunning(kTargets[t].family);
}

float sourceValue(int32_t s) {
	if (!sourceRunning(s)) {
		return 0.5f;
	}
	switch (s) {
	case 0:
	case 1:
	case 2:
		return clamp01((deluge::chaos::runner::engine().getNormalised(s, 0) + 1.0f) * 0.5f);
	case 3:
	case 4:
		return clamp01(deluge::marbles::runner::engine().getValue(s - 3));
	case 5:
	case 6: {
		auto& e = deluge::rungler::runner::engine();
		float range = e.params().rangeVolts;
		return range > 0.0f ? clamp01(e.getVolts(s - 5) / range) : 0.5f;
	}
	case 7:
	case 8:
	case 9:
	case 10:
		return clamp01(deluge::neurons::runner::engine().getMembrane(s - 7));
	case 11: {
		auto& e = deluge::neurons::runner::engine();
		float sum = 0.0f;
		for (int32_t n = 0; n < deluge::neurons::kNumNeurons; n++) {
			sum += e.getMembrane(n);
		}
		return clamp01(sum / static_cast<float>(deluge::neurons::kNumNeurons));
	}
	case 12:
		return clamp01(static_cast<float>(deluge::turing::runner::getHistoryValue(0)) / 255.0f);
	case 13: {
		float best = 0.5f;
		for (int32_t g = 0; g < kNumHitGates; g++) {
			best = std::max(best, hitLevel(g));
		}
		return best;
	}
	default:
		return hitLevel(s - 14);
	}
}

namespace {

float readTarget(int32_t t) {
	switch (t) {
	case 0:
		return deluge::marbles::runner::engine().params().dejaVu;
	case 1:
		return deluge::marbles::runner::engine().params().spread;
	case 2:
		return deluge::marbles::runner::engine().params().bias;
	case 3:
		return deluge::marbles::runner::engine().params().steps;
	case 4:
		return deluge::marbles::runner::engine().params().jitter;
	case 5:
		return deluge::marbles::runner::engine().params().bernoulli;
	case 6:
		return deluge::rungler::runner::engine().params().chance;
	case 7:
		return deluge::rungler::runner::engine().params().modAmount;
	case 8:
		return deluge::rungler::runner::engine().params().slew;
	case 9:
		return deluge::turing::runner::engine().getFlipProbability();
	case 10:
		return deluge::neurons::runner::engine().params().noise;
	case 11:
		return deluge::neurons::runner::engine().params().leak;
	case 12:
		return deluge::chaos::runner::engine().getCoupling() / kChaosCouplingMax;
	default:
		return deluge::chaos::runner::getProbDepth();
	}
}

void writeTarget(int32_t t, float v) {
	v = clamp01(v);
	switch (t) {
	case 0:
		deluge::marbles::runner::engine().setDejaVu(v);
		break;
	case 1:
		deluge::marbles::runner::engine().setSpread(v);
		break;
	case 2:
		deluge::marbles::runner::engine().setBias(v);
		break;
	case 3:
		deluge::marbles::runner::engine().setSteps(v);
		break;
	case 4:
		deluge::marbles::runner::engine().setJitter(v);
		break;
	case 5:
		deluge::marbles::runner::engine().setBernoulli(v);
		break;
	case 6:
		deluge::rungler::runner::engine().setChance(v);
		break;
	case 7:
		deluge::rungler::runner::engine().setModAmount(v);
		break;
	case 8:
		deluge::rungler::runner::engine().setSlew(v);
		break;
	case 9:
		deluge::turing::runner::setFlipProbability(v);
		break;
	case 10:
		deluge::neurons::runner::engine().setNoise(v);
		break;
	case 11:
		deluge::neurons::runner::engine().setLeak(v);
		break;
	case 12:
		deluge::chaos::runner::engine().setCoupling(v * kChaosCouplingMax);
		break;
	default:
		deluge::chaos::runner::setProbDepth(v);
		break;
	}
}

} // namespace

Settings getSettings() {
	Settings s;
	for (int32_t i = 0; i < kNumRoutes; i++) {
		s.routes[i] = crossmodEngine.route(i);
	}
	return s;
}

void loadSettings(const Settings& s) {
	// hand back whatever the old routes were holding before replacing them
	for (int32_t i = 0; i < kNumRoutes; i++) {
		crossmodEngine.clearRoute(i);
	}
	tick();
	for (int32_t i = 0; i < kNumRoutes; i++) {
		const Route& r = s.routes[i];
		crossmodEngine.setRoute(i, r.source, r.target, r.depth);
	}
}

void resetToDefaults() {
	loadSettings(Settings{});
}

bool isUsed() {
	return crossmodEngine.anyUsed();
}

CrossmodEngine& engine() {
	return crossmodEngine;
}

void noteHit(int32_t gate) {
	if (gate >= 0 && gate < kNumHitGates) {
		lastHit[gate] = AudioEngine::audioSampleTimer;
		everHit[gate] = true;
	}
}

void tick() {
	float sources[kMaxSources];
	float current[kMaxTargets] = {};
	bool available[kMaxTargets] = {};
	float out[kMaxTargets];
	bool write[kMaxTargets];
	for (int32_t i = 0; i < kMaxSources; i++) {
		sources[i] = (i < kNumSources) ? sourceValue(i) : 0.5f;
	}
	for (int32_t t = 0; t < kNumTargets; t++) {
		available[t] = targetRunning(t);
		current[t] = available[t] ? readTarget(t) : 0.5f;
	}
	crossmodEngine.update(sources, current, available, out, write);
	for (int32_t t = 0; t < kNumTargets; t++) {
		if (write[t]) {
			writeTarget(t, out[t]);
		}
	}
}

} // namespace deluge::crossmod::runner
