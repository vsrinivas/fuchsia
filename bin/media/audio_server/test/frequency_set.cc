// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/test/frequency_set.h"

namespace media {
namespace test {

//
// In determining these, the values need not be perfectly precise (that is, our
// "100 Hz" proxy need not be perfectly 100.0000). However, we DO make sure to
// avoid any nearby integer-multiple relationships (in large part these are
// relatively prime or at least avoid multiples of 2, 3, 5, 7 where possible).
// This is done to ensure that sampling occurs across a good statistical mix
// of sinusoid's period, rather than hitting the same few locations on the wave.
//
// The summary audio analysis tests use a small set of standard frequencies.
constexpr uint32_t FrequencySet::kNumSummaryFreqs;
constexpr uint32_t FrequencySet::kSummaryFreqs[FrequencySet::kNumSummaryFreqs];

// Certain tests (such as noise floor and sinad) are evaluated with a sinusoidal
// input at a single reference frequency (usually close to 1 kHz).
constexpr uint32_t FrequencySet::kRefFreqBin;  // 1kHz reference tone
constexpr uint32_t FrequencySet::kReferenceFreq;

// Because of translation between our power-of-two-sized buffers and our nominal
// sample rate, the above array contains __proxies__ of the desired frequencies,
// but not the actual frequency values themselves.
// The below is an actual representation of the standard set of audio
// frequencies for fidelity testing -- reverse-calculated from the above values.
constexpr uint32_t
    FrequencySet::kSummaryFreqsTranslated[FrequencySet::kNumSummaryFreqs];

static_assert(
    (FrequencySet::kSummaryFreqsTranslated[FrequencySet::kRefFreqBin] > 980) &&
        (FrequencySet::kSummaryFreqsTranslated[FrequencySet::kRefFreqBin] <
         1020),
    "Incorrect 1kHz reference frequency");

}  // namespace test
}  // namespace media
