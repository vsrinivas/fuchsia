// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_CORE_MIXER_TEST_FREQUENCY_SET_H_
#define GARNET_BIN_MEDIA_AUDIO_CORE_MIXER_TEST_FREQUENCY_SET_H_

#include <fbl/algorithm.h>
#include <zircon/types.h>
#include <array>

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
//
// To better model how our resamplers are used by the rest of the system, when
// testing our resamplers, we use multiple smaller jobs rather than mixing the
// entire 64k samples at one go. Breaking our 64k buffer into 256 subjobs will
// emulate ~5.33ms buffers (64k/256 = 256 samples @ 48kHz); breaking it into 128
// (512-sample packets) will model client submissions of ~10.67ms, etc.
//
// In our audio fidelity tests (noise floor, frequency response, SINAD, dynamic
// range, plus others in the future), we compare current measurements to
// previous results. For any set of inputs, our results are always exactly the
// same -- but we should note that (as currently implemented), configuration
// changes (such as adjustments to the below const) affect frequency response
// and SINAD results in ways that differ by frequency. Doubling the resampling
// packet size, as an example, may improve frequency response at 25 Hz but
// degrade it at 10 kHz. With this in mind, the values we have saved as
// thresholds represent the worst-case results across kResamplerTestNumPackets
// values of [1,2,4,8,16,32,64,128,256,512,1024,2048,4096,8192,16384,32768].
static constexpr uint32_t kResamplerTestNumPackets = 128;

class FrequencySet {
 public:
  static bool UseFullFrequencySet;

  // The full-spectrum audio tests use a broad set of standard frequencies.
  static constexpr uint32_t kNumReferenceFreqs = 47;

  // Each val represents a standard frequency within the broad set.
  static const std::array<uint32_t, kNumReferenceFreqs> kReferenceFreqs;

  // Because of translation between power-of-two-sized buffers and sample rate,
  // values in kReferenceFreqs translate into the following actual frequencies:
  static const std::array<uint32_t, kNumReferenceFreqs> kRefFreqsTranslated;

  // Certain tests (such as noise floor and sinad) are evaluated with a
  // sinusoidal input at a single reference frequency (usually close to 1 kHz).
  static constexpr uint32_t kRefFreqIdx = 20;  // [20] is 1kHz reference tone.
  static const uint32_t kReferenceFreq;

  // Summary audio tests use a small frequency set taken from the full list.
  static constexpr uint32_t kNumSummaryIdxs = 3;

  // Each val is a kReferenceFreqs index, pointing to a summary freq.
  static const std::array<uint32_t, kNumSummaryIdxs> kSummaryIdxs;

  // class is static only - prevent attempts to instantiate it
  FrequencySet() = delete;
};

}  // namespace test
}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_CORE_MIXER_TEST_FREQUENCY_SET_H_
