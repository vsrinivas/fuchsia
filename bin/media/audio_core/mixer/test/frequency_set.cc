// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/mixer/test/frequency_set.h"

namespace media {
namespace audio {
namespace test {

bool FrequencySet::UseFullFrequencySet = false;

//
// In determining these, the values need not be perfectly precise (that is, our
// "100 Hz" proxy need not be perfectly 100.0000). However, we DO make sure to
// avoid any nearby integer-multiple relationships (in large part these are
// relatively prime or at least avoid multiples of 2, 3, 5, 7 where possible).
// This is done to ensure that sampling occurs across a good statistical mix
// of sinusoid's period, rather than hitting the same few locations on the wave.
//
// The extended audio analysis tests use this large set of standard frequencies.
constexpr uint32_t FrequencySet::kNumReferenceFreqs;
const std::array<uint32_t, FrequencySet::kNumReferenceFreqs>
    FrequencySet::kReferenceFreqs = {
        0,     18,    23,    27,    34,    41,    53,    67,    85,    109,
        137,   169,   221,   271,   341,   431,   541,   683,   859,   1091,
        1363,  1703,  2183,  2729,  3413,  4301,  5461,  6827,  8603,  10921,
        13651, 16381, 21841, 26623, 27307, 27989, 28673, 30103, 31949, 32768,
        34133, 43007, 54613, 60073, 60209, 64853, 65535};

// Because of translation between our power-of-two-sized buffers and our nominal
// sample rate, the above array contains __proxies__ of the desired frequencies,
// but not the actual frequency values themselves.
// The below is an actual representation of the standard set of audio
// frequencies for fidelity testing -- reverse-calculated from the above values.
const std::array<uint32_t, FrequencySet::kNumReferenceFreqs>
    FrequencySet::kRefFreqsTranslated = {
        0,     13,    17,    20,    25,    30,    39,    49,    62,    80,
        100,   124,   162,   198,   250,   316,   396,   500,   630,   799,
        998,   1247,  1599,  1999,  2500,  3150,  4000,  5000,  6301,  7999,
        9998,  11998, 15997, 19499, 20000, 20500, 21001, 22049, 23400, 24000,
        25000, 31500, 40000, 44000, 44100, 47500, 47999};

// Certain tests (such as noise floor and sinad) are evaluated with a sinusoidal
// input at a single reference frequency (usually close to 1 kHz).
constexpr uint32_t FrequencySet::kRefFreqIdx;  // 1kHz reference tone
const uint32_t FrequencySet::kReferenceFreq = kReferenceFreqs[kRefFreqIdx];

//
// Summary audio tests use a small set of frequencies, taken from the full set.
constexpr uint32_t FrequencySet::kNumSummaryIdxs;
const std::array<uint32_t, FrequencySet::kNumSummaryIdxs>
    FrequencySet::kSummaryIdxs = {
        6,   //  kReferenceFreqs[6] == 53, which translates => 40 Hz
        20,  //  => 1000 Hz
        31   //  => 12000 Hz
};

}  // namespace test
}  // namespace audio
}  // namespace media
