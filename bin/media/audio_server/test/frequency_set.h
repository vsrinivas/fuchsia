// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/algorithm.h>
#include <zircon/types.h>

namespace media {
namespace audio {
namespace test {

//
// In performing all of our audio analysis tests with a specific buffer length,
// we can choose input sinusoids with frequencies that perfectly fit within
// those buffers, eliminating the need for FFT windowing. Our measurement
// frequencies have been specifically chosen as approximations of the standard
// "3 freqs per octave" representative set, assuming a 65536/48000 ratio between
// buffer size and eventual sample rate.
//
// Working in concert with GenerateCosine, these summary frequencies (currently
// 40 Hz, 1 kHz and 12 kHz) are "phase-locked" to the size of the buffer used in
// our frequency-based testing, in that the actual frequency is calculated so
// that there is an exact integral number of complete sinusoidal periods within
// the source data buffer. This eliminates the need for us to performing
// windowing or other data conditioning before performing frequency analysis,
// although it does make the actual values sent to GenerateCosine slightly
// different than the actual frequency.

// Furthermore, we adjust these values slightly so that their periods are not
// closely related geometrically to the sample rate -- we do this so that
// sampling of a given sinusoid will be more statistically spread across the
// entire waveform, rather than this happening at just a few spots (for example,
// using approximately 11997.8 Hz instead of 12000 Hz).
//
// For now we assume an eventual 48 kHz output sample rate, so (along with our
// source buffer of size 65536) this translation ratio is 65536/48000. In other
// words, the 'freq' value that we should send to GenerateCosine in order to
// simulate a 1 kHz sinusoid would be 1363.
//
static constexpr uint32_t kFreqTestBufSize = 65536;

class FrequencySet {
 public:
  static bool UseFullFrequencySet;

  //
  // The summary audio analysis tests use a small set of standard frequencies.
  //
  // clang-format off
  static constexpr uint32_t kReferenceFreqs[] = {
          0,    18,    23,    27,    34,    41,    53,    67,    85,    109,
        137,   169,   221,   271,   341,   431,   541,   683,   859,   1091,
       1363,  1703,  2183,  2729,  3413,  4301,  5461,  6827,  8603,  10921,
      13651, 16381, 21841, 26623, 27307, 27989, 28673, 30103, 31949,  32768,
      34133, 43007, 54613, 60073, 60209, 64853, 65535};
  // clang-format on
  static constexpr uint32_t kNumReferenceFreqs = fbl::count_of(kReferenceFreqs);

  static constexpr uint32_t kSummaryIdxs[] = {6, 20, 31};
  static constexpr uint32_t kNumSummaryIdxs = fbl::count_of(kSummaryIdxs);

  //
  // Because of translation between power-of-two-sized buffers and eventual
  // sample rate, the above array represents the following actual frequencies:
  //
  // clang-format off
  static constexpr uint32_t kRefFreqsTranslated[] = {
          0,    13,    17,    20,    25,    30,    39,    49,    62,    80,
        100,   124,   162,   198,   250,   316,   396,   500,   630,   799,
        998,  1247,  1599,  1999,  2500,  3150,  4000,  5000,  6301,  7999,
       9998, 11998, 15997, 19499, 20000, 20500, 21001, 22049, 23400, 24000,
      25000, 31500, 40000, 44000, 44100, 47500, 47999};
  // clang-format on

  //
  // Certain tests (such as noise floor and sinad) are evaluated with a
  // sinusoidal input at a single reference frequency (usually close to 1 kHz).
  static constexpr uint32_t kRefFreqIdx = 20;  // 1kHz reference tone
  static constexpr uint32_t kReferenceFreq = kReferenceFreqs[kRefFreqIdx];

  // class is static only - prevent attempts to instantiate it
  FrequencySet() = delete;
};

}  // namespace test
}  // namespace audio
}  // namespace media
