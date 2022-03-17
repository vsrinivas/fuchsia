// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_ANALYSIS_ANALYSIS_H_
#define SRC_MEDIA_AUDIO_LIB_ANALYSIS_ANALYSIS_H_

#include <lib/trace/event.h>
#include <zircon/types.h>

#include <limits>
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
  void Display(std::string tag = "", double magn_display_threshold = 0.0);

  // Raw list of square magnitudes for all bins up to size/2.
  std::vector<double> all_square_magnitudes;

  // Mapping from frequency -> magnitude, for each requested frequency.
  std::unordered_map<int32_t, double> magnitudes;
  // Phase in radians, for each requested frequency.
  std::unordered_map<int32_t, double> phases;
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
                                  std::unordered_set<int32_t> freqs);

// Shorthand that analyzes a single frequency.
template <fuchsia::media::AudioSampleFormat SampleFormat>
AudioFreqResult MeasureAudioFreq(AudioBufferSlice<SampleFormat> slice, int32_t freq) {
  auto result = MeasureAudioFreqs(slice, {freq});
  FX_DCHECK(result.total_magn_signal == result.magnitudes[freq]);
  return result;
}

// Compute the root-mean-square (RMS) energy of a slice. This is a measure of loudness.
template <fuchsia::media::AudioSampleFormat SampleFormat>
double MeasureAudioRMS(AudioBufferSlice<SampleFormat> slice) {
  FX_CHECK(slice.NumFrames() > 0);
  double sum = 0;
  for (int64_t frame = 0; frame < slice.NumFrames(); frame++) {
    for (int32_t chan = 0; chan < slice.format().channels(); chan++) {
      double s = SampleFormatTraits<SampleFormat>::ToFloat(slice.SampleAt(frame, chan));
      sum += s * s;
    }
  }
  return sqrt(sum / static_cast<double>(slice.NumSamples()));
}

// Locate the left edge of the first impulse in the given slice, ignoring samples quieter
// than the given noise floor. Returns the frame index if found, and std::nullopt otherwise.
// The given slice must have a single channel. We assume the impulse has a positive signal.
template <fuchsia::media::AudioSampleFormat SampleFormat>
std::optional<int64_t> FindImpulseLeadingEdge(
    AudioBufferSlice<SampleFormat> slice,
    typename SampleFormatTraits<SampleFormat>::SampleT noise_floor) {
  FX_CHECK(slice.format().channels() == 1);

  auto normalize = [](typename SampleFormatTraits<SampleFormat>::SampleT val) {
    float d = static_cast<float>(val);
    if constexpr (SampleFormat == fuchsia::media::AudioSampleFormat::UNSIGNED_8) {
      d -= 128;
    }
    return d;
  };

  // If our impulse was a single frame, we could simply find the maximum value.
  // To support wider impulses, we need to find the left edge of the impulse. We
  // do this by finding the first value such that there does not exist a value
  // more than 50% larger.
  float max_value = 0;
  for (int64_t f = 0; f < slice.NumFrames(); f++) {
    max_value = std::max(max_value, normalize(slice.SampleAt(f, 0)));
  }
  for (int64_t f = 0; f < slice.NumFrames(); f++) {
    float val = normalize(slice.SampleAt(f, 0));
    if (val <= static_cast<float>(noise_floor)) {
      continue;
    }
    if (1.5 * val > max_value) {
      return f;
    }
  }
  return std::nullopt;
}

// Locate the center of the impulse in the given slice, ignoring samples quieter than the given
// noise floor. Returns the frame index if found, and std::nullopt otherwise.
// This function requires a one-channel slice, and it assumes there is exactly one impulse.
template <fuchsia::media::AudioSampleFormat SampleFormat>
std::optional<int64_t> FindImpulseCenter(
    AudioBufferSlice<SampleFormat> slice,
    typename SampleFormatTraits<SampleFormat>::SampleT noise_floor) {
  constexpr bool kDisplayEdgesAndCenter = false;

  FX_CHECK(slice.format().channels() == 1);

  auto normalize = [](typename SampleFormatTraits<SampleFormat>::SampleT val) {
    float norm = static_cast<float>(val);
    if constexpr (SampleFormat == fuchsia::media::AudioSampleFormat::UNSIGNED_8) {
      norm -= 0x80;
    }
    return norm;
  };

  // If our impulse was a single frame, we could simply find the maximum absolute value.
  // To support wider impulses, we need to find the left and right edges of the impulse.
  // We do this by finding the first and last values such that there does not exist a
  // value more than 50% larger.
  float max_value = 0.0f;
  for (int64_t idx = 0; idx < slice.NumFrames(); idx++) {
    float val = std::abs(normalize(slice.SampleAt(idx, 0)));
    if (val <= static_cast<float>(noise_floor)) {
      continue;
    }
    max_value = std::max(max_value, val);
  }
  if (max_value == 0.0f) {
    return std::nullopt;
  }

  int64_t leading_idx = 0;
  float leading_val = 0.0f;
  for (int64_t idx = 0; idx < slice.NumFrames(); ++idx) {
    float val = normalize(slice.SampleAt(idx, 0));
    if (1.5 * std::abs(val) > max_value) {
      leading_idx = idx;
      leading_val = val;
      break;
    }
  }

  int64_t trailing_idx = slice.NumFrames() - 1;
  float trailing_val = 0.0f;
  for (int64_t idx = slice.NumFrames() - 1; idx >= 0; --idx) {
    float val = normalize(slice.SampleAt(idx, 0));
    if (1.5 * std::abs(val) > max_value) {
      trailing_idx = idx;
      trailing_val = val;
      break;
    }
  }
  int64_t sum_idx = leading_idx + trailing_idx;
  int64_t center_idx = sum_idx / 2;
  center_idx += ((sum_idx & 0x01) && leading_val < trailing_val) ? 1 : 0;

  if constexpr (kDisplayEdgesAndCenter) {
    std::stringstream edge_values;
    edge_values << "   [" << std::setw(5) << slice.start_frame() + leading_idx << "]"
                << std::setw(10) << leading_val << " | [" << std::setw(5)
                << slice.start_frame() + center_idx << "]" << std::setw(10)
                << normalize(slice.SampleAt(center_idx, 0));
    if ((sum_idx & 0x01) && leading_val < trailing_val) {
      edge_values << " | [" << std::setw(5) << slice.start_frame() + center_idx << "]"
                  << std::setw(10) << normalize(slice.SampleAt(center_idx, 0));
    }
    FX_LOGS(INFO) << edge_values.str() << " | [" << std::setw(5)
                  << slice.start_frame() + trailing_idx << "]" << std::setw(10) << trailing_val;
  }

  return center_idx;
}

// Multiply the input buffer by a Tukey window, producing a new output buffer. A Tukey window
// contains a ramp up from zero, followed by a flat top of 1.0, followed by a ramp down to zero.
// The total width of the up and down ramps is described by the alpha parameter, which must be <= 1.
template <fuchsia::media::AudioSampleFormat SampleFormat>
AudioBuffer<SampleFormat> MultiplyByTukeyWindow(AudioBufferSlice<SampleFormat> slice, double alpha);

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_ANALYSIS_ANALYSIS_H_
