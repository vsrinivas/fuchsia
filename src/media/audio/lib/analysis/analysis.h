// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_ANALYSIS_ANALYSIS_H_
#define SRC_MEDIA_AUDIO_LIB_ANALYSIS_ANALYSIS_H_

#include <zircon/types.h>

#include <unordered_map>
#include <unordered_set>

#include "src/media/audio/lib/format/audio_buffer.h"

namespace media::audio {

namespace internal {

// Perform a Fast Fourier Transform on the provided data arrays.
//
// On input, real[] and imag[] contain 'buf_size' number of double-float values
// in the time domain (such as audio samples); buf_size must be a power-of-two.
//
// On output, real[] and imag[] contain 'buf_size' number of double-float values
// in frequency domain, but generally used only through buf_size/2 (per Nyquist)
void FFT(double* real, double* imag, uint32_t buf_size);

// Calculate phase in radians for the complex pair. Correctly handles negative
// or zero values: range of return value is [-PI,PI], not just [-PI/2,PI/2].
double GetPhase(double real, double imag);

// Convert provided real-imag (cartesian) data into magn-phase (polar) format.
// This is done with 2 in-buffers 2 two out-buffers -- NOT 2 in-out-buffers.
// TODO(mpuryear): will clients (tests) want this transformed in-place?
void RectangularToPolar(const double* real, const double* imag, uint32_t buf_size, double* magn,
                        double* phase = nullptr);

void RealDFT(const double* reals, uint32_t len, double* r_freq, double* i_freq);

void InverseDFT(double* real, double* imag, uint32_t buf_size, double* real_out);

void InverseFFT(double* real, double* imag, uint32_t buf_size);

}  // namespace internal

struct AudioFreqResult {
  // Mapping from frequency -> magnitude, for each requested frequency.
  std::unordered_map<size_t, double> magnitudes;
  // Phase in radians, for each requested frequency.
  std::unordered_map<size_t, double> phases;
  // Total magnitude over all requested frequencies.
  // Magnitude is the root-sum-of-squares of the magnitude at all requested frequencies.
  double total_magn_signal;
  // Total magnitude over all other frequencies.
  // Magnitude is the root-sum-of-squares of the magnitude at all other frequencies.
  double total_magn_other;
};

// For the given audio buffer, analyze contents and return the magnitude (and phase) at the given
// frequency. Also return magnitude of all other content. Useful for frequency response and
// signal-to-noise. Internally uses an FFT, so slice.NumFrames() must be a power-of-two. The format
// must have channels() == 1.
//
// |freq| is the number of **complete sinusoidal periods** that should perfectly fit into the
// buffer.
template <fuchsia::media::AudioSampleFormat SampleFormat>
AudioFreqResult MeasureAudioFreqs(AudioBufferSlice<SampleFormat> slice,
                                  std::unordered_set<size_t> freqs);

// Shorthand that analyzes a single frequency.
template <fuchsia::media::AudioSampleFormat SampleFormat>
AudioFreqResult MeasureAudioFreq(AudioBufferSlice<SampleFormat> slice, size_t freq) {
  auto result = MeasureAudioFreqs(slice, {freq});
  FX_DCHECK(result.total_magn_signal == result.magnitudes[freq]);
  return result;
}

// Compute the root-mean-square (RMS) energy of a slice. This is a measure of loudness.
template <fuchsia::media::AudioSampleFormat SampleFormat>
double MeasureAudioRMS(AudioBufferSlice<SampleFormat> slice) {
  FX_CHECK(slice.NumFrames() > 0);
  double sum = 0;
  for (size_t frame = 0; frame < slice.NumFrames(); frame++) {
    for (size_t chan = 0; chan < slice.format().channels(); chan++) {
      double s = SampleFormatTraits<SampleFormat>::ToFloat(slice.SampleAt(frame, chan));
      sum += s * s;
    }
  }
  return sqrt(sum / slice.NumSamples());
}

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_ANALYSIS_ANALYSIS_H_
