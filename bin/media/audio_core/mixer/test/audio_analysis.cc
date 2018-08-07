// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/mixer/test/audio_analysis.h"

#include <iomanip>
#include <vector>

#include <fbl/algorithm.h>

#include "lib/fxl/logging.h"

namespace media {
namespace audio {
namespace test {

//
// This library contains standalone functions that enable tests to analyze
// audio- or gain-related outputs.
//
// The GenerateCosine function populates audio buffers with sinusoidal values of
// the given frequency, magnitude and phase. The FFT function performs Fast
// Fourier Transforms on the provided real and imaginary arrays. The
// MeasureAudioFreq function analyzes the given audio buffer at the specified
// frequency, returning the magnitude of signal that resides at that frequency,
// as well as the combined magnitude of all other frequencies (useful for
// computing signal-to-noise and other metrics).
//

//
// Numerically compare two buffers of integers. Emit values if mismatch found.
// For testability, last param bool represents whether we expect comp to fail
//
// TODO(mpuryear): Consider using the googletest Array Matchers for this
template <typename T>
bool CompareBuffers(const T* actual, const T* expect, uint32_t buf_size,
                    bool expect_to_pass) {
  // uint8_t is interpreted as char. Cast into larger int for correct display.
  constexpr bool is_uint8 = std::is_same<T, uint8_t>::value;

  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    if (actual[idx] != expect[idx]) {
      if (expect_to_pass) {
        FXL_LOG(ERROR) << "[" << idx << "] was " << std::setprecision(10)
                       << (is_uint8 ? static_cast<int32_t>(actual[idx])
                                    : actual[idx])
                       << ", should be "
                       << (is_uint8 ? static_cast<int32_t>(expect[idx])
                                    : expect[idx]);
      }
      return false;
    }
  }
  if (!expect_to_pass) {
    FXL_LOG(ERROR) << "We expected two buffers (length " << buf_size
                   << ") to differ, but they did not!";
  }
  return true;
}

// Numerically compares a buffer of integers to a specific value.
// For testability, last param bool represents whether we expect comp to fail
template <typename T>
bool CompareBufferToVal(const T* buf, T val, uint32_t buf_size,
                        bool expect_to_pass) {
  // uint8_t is interpreted as char. Cast into larger int for correct display.
  constexpr bool is_uint8 = std::is_same<T, uint8_t>::value;

  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    if (buf[idx] != val) {
      if (expect_to_pass) {
        FXL_LOG(ERROR) << "[" << idx << "] was " << std::setprecision(10)
                       << (is_uint8 ? static_cast<int32_t>(buf[idx]) : buf[idx])
                       << ", should be "
                       << (is_uint8 ? static_cast<int32_t>(val) : val);
      }
      return false;
    }
  }
  if (!expect_to_pass) {
    FXL_LOG(ERROR) << "We expected buffer (length " << buf_size
                   << ") to differ from value "
                   << (is_uint8 ? static_cast<int32_t>(val) : val)
                   << ", but it was equal!";
  }
  return true;
}

// Display array of double values
void DisplayVals(const double* buf, uint32_t buf_size) {
  printf("\n    ********************************************************");
  printf("\n **************************************************************");
  printf("\n ***       Displaying raw array data for length %5d       ***",
         buf_size);
  printf("\n **************************************************************");
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    if (idx % 8 == 0) {
      printf("\n [%d]  ", idx);
    }
    printf("%.15lf    ", buf[idx]);
  }
  printf("\n **************************************************************");
  printf("\n    ********************************************************");
  printf("\n");
}

// Given a val with fractional content, prep it to be put in an int container.
//
// Used specifically when generating high-precision audio content for source
// buffers, these functions round double-precision floating point values into
// the appropriate container sizes (assumed to be integer, although float
// destination types are specialized).
// In the general case, values are rounded -- and unsigned 8-bit integers
// further biased by 0x80 -- so that the output data is exactly as it would be
// when arriving from an audio source (such as .wav file with int16 values, or
// audio input device operating in uint8 mode). Float and double specializations
// need not do anything, as double-to-float cast poses no real risk of
// distortion from truncation.
// Used only within this file by GenerateCosine, these functions do not check
// for overflow/clamp, leaving that responsibility on users of GenerateCosine.
template <typename T>
inline T Finalize(double value) {
  return round(value);
}
template <>
inline uint8_t Finalize(double value) {
  return round(value) + 0x80;
}
template <>
inline float Finalize(double value) {
  return value;
}
template <>
inline double Finalize(double value) {
  return value;
}

// Populate this buffer with cosine values. Frequency is set so that wave
// repeats itself 'freq' times within buffer length; 'magn' specifies peak
// value. Accumulates these values with preexisting array vals, if bool is set.
template <typename T>
void GenerateCosine(T* buffer, uint32_t buf_size, double freq, bool accumulate,
                    double magn, double phase) {
  // If frequency is 0 (constant val), phase offset causes reduced amplitude
  FXL_DCHECK(freq > 0.0 || (freq == 0.0 && phase == 0.0));

  // Freqs above buf_size/2 (Nyquist limit) will alias into lower frequencies.
  FXL_DCHECK(freq * 2.0 <= buf_size)
      << "Buffer too short--requested frequency will be aliased";

  // freq is defined as: cosine recurs exactly 'freq' times within buf_size.
  const double mult = 2.0 * M_PI / buf_size * freq;

  if (accumulate) {
    for (uint32_t idx = 0; idx < buf_size; ++idx) {
      buffer[idx] += Finalize<T>(magn * std::cos(mult * idx + phase));
    }
  } else {
    for (uint32_t idx = 0; idx < buf_size; ++idx) {
      buffer[idx] = Finalize<T>(magn * std::cos(mult * idx + phase));
    }
  }
}

// Perform a Fast Fourier Transform on the provided data arrays.
//
// On input, real[] and imag[] contain 'buf_size' number of double-float values
// in the time domain (such as audio samples); buf_size must be a power-of-two.
//
// On output, real[] and imag[] contain 'buf_size' number of double-float values
// in frequency domain, but generally used only through buf_size/2 (per Nyquist)
//
// The classic FFT derivation (based on Cooley-Tukey), and what I implement
// here, achieves NlogN performance (instead of N^2) with divide-and-conquer,
// while additional optimizing by working in-place. To do this, it first breaks
// the data stream into single elements (so-called interlaced decomposition)
// that are in the appropriate order, and then combines these to form series of
// 2-element matrices, then combines these to form 4-element matrices, and so
// on, until combining the final matrices (each of which is half the size of the
// original). Two interesting details deserve further explanation:
//
// 1. Interlaced decomposition into the "appropriate order" mentioned above is
// achieved by sorting values by index, but in ascending order if viewing the
// index in bit-reversed manner! (This is exactly what is needed in order to
// combine the pairs of values in the appropriate cross-matrix sequence.) So for
// a stream of 16 values (4 bits of index), this re-sorted order is as follows -
//    0,    8,    4,   12,   2,     10,    6, ...,    7,   15 ... or, in binary:
// 0000, 1000, 0100, 1100, 0010, 11010, 0110, ..., 0111, 1111.
//
// 2. Combining each matrix (called synthesis) is accomplished in the following
// fashion, regardless of size: combining [ac] and [bd] to make [abcd] is done
// by spacing [ac] into [a0c0] and spacing [bd] into [0b0d] and then overlaying
// them.  The frequency-domain equivalent of making [a0c0] from [ac] is simply
// to turn [AC] into [ACAC]. The equivalent of creating [0b0d] from [bd] is to
// multiply [BD] by a sinusoid (to delay it by one sample) while also
// duplicating [BD] into [BDBD]. This results in a 'butterfly' flow (based on
// the shape of two inputs, two outputs, and the four arrows between them).
// Specifically, in each pair of values that are combined:
// even_output = even_input + (sinusoid_factor x odd_input), and
// odd_output  = even input - (sinusoid_factor x odd_input).
// (specifically, this sinusoid is the spectrum of a shifted delta function)
// This butterfly operation transforms two complex points into two other complex
// points, combining two 1-element signals into one 2-element signal (etc).
//
// Classic DSP texts by Oppenheim, Schaffer, Rabiner, or the Cooley-Tukey paper
// itself, are serviceable references for these concepts.
//
// TODO(mpuryear): Consider std::complex<double> instead of real/imag arrays.
void FFT(double* reals, double* imags, uint32_t buf_size) {
  FXL_DCHECK(fbl::is_pow2(buf_size));
  const uint32_t buf_sz_2 = buf_size >> 1;

  uint32_t N = 0;
  while (buf_size > (1 << N)) {
    ++N;
  }

  // First, perform a bit-reversal sort of indices. Again, this is done so
  // that all subsequent matrix-merging work can be done on adjacent values.
  // This sort implementation performs the minimal number of swaps/moves
  // (considering buf_size could be 128K, 256K or more), but is admittedly more
  // difficult to follow than some.
  // When debugging, remember 1) each swap moves both vals to final locations,
  // 2) each val is touched once or not at all, and 3) the final index ordering
  // is **ascending if looking at indices in bit-reversed fashion**.
  uint32_t swap_idx = buf_sz_2;
  for (uint32_t idx = 1; idx < buf_size - 1; ++idx) {
    if (idx < swap_idx) {
      std::swap(reals[idx], reals[swap_idx]);
      std::swap(imags[idx], imags[swap_idx]);
    }
    uint32_t alt_idx = buf_sz_2;
    while (alt_idx <= swap_idx) {
      swap_idx -= alt_idx;
      alt_idx /= 2;
    }
    swap_idx += alt_idx;
  }

  // Loop through log2(buf_size) stages: one for each power of two, starting
  // with 2, then 4, then 8, .... During each stage, combine pairs of shorter
  // signals (of length 'sub_dft_sz_2') into single, longer signals (of length
  // 'sub_dft_sz'). From previous sorting, signals to be combined are adjacent.
  for (uint32_t fft_level = 1; fft_level <= N; ++fft_level) {
    const uint32_t sub_dft_sz = 1 << fft_level;     // length of combined signal
    const uint32_t sub_dft_sz_2 = sub_dft_sz >> 1;  // length of shorter signals
    // 'Odd' values are multiplied by complex (real & imaginary) factors before
    // being combined with 'even' values. These coefficients help the real and
    // imaginary factors advance correctly, within each sub_dft.
    const double real_coef = std::cos(M_PI / static_cast<double>(sub_dft_sz_2));
    const double imag_coef =
        -std::sin(M_PI / static_cast<double>(sub_dft_sz_2));

    // For each point in this signal (for each complex pair in this 'sub_dft'),
    double real_factor = 1.0, imag_factor = 0.0;
    for (uint32_t btrfly_num = 1; btrfly_num <= sub_dft_sz_2; ++btrfly_num) {
      double temp_real, temp_imag;

      // ... perform the so-called butterfly operation on a pair of points.
      for (uint32_t idx = btrfly_num - 1; idx < buf_size; idx += sub_dft_sz) {
        const uint32_t idx2 = idx + sub_dft_sz_2;

        temp_real = reals[idx2] * real_factor - imags[idx2] * imag_factor;
        temp_imag = reals[idx2] * imag_factor + imags[idx2] * real_factor;
        reals[idx2] = reals[idx] - temp_real;
        imags[idx2] = imags[idx] - temp_imag;
        reals[idx] += temp_real;
        imags[idx] += temp_imag;
      }
      // Update the sinusoid coefficients, for the next points in this signal.
      temp_real = real_factor;
      real_factor = temp_real * real_coef - imag_factor * imag_coef;
      imag_factor = temp_real * imag_coef + imag_factor * real_coef;
    }
  }
}

// For specified audio buffer & length, analyze the contents and return the
// magnitude (and phase) of signal at given frequency (i.e. frequency at which
// 'freq' periods fit perfectly within buffer length). Also return the magnitude
// of all other content. Useful for frequency response and signal-to-noise.
// Internally uses an FFT, so buf_size must be a power-of-two.
template <typename T>
void MeasureAudioFreq(T* audio, uint32_t buf_size, uint32_t freq,
                      double* magn_signal, double* magn_other) {
  FXL_DCHECK(fbl::is_pow2(buf_size));

  uint32_t buf_sz_2 = buf_size >> 1;
  bool freq_out_of_range = (freq > buf_sz_2);

  // Copy input to double buffer, before doing a high-res FFT (freq-analysis)
  // Note that we set imags[] to zero: MeasureAudioFreq retrieves a REAL (not
  // Complex) FFT for the data, the return real and imaginary frequency-domain
  // data only spans 0...N/2 (inclusive).
  std::vector<double> reals(buf_size);
  std::vector<double> imags(buf_size);
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    reals[idx] = audio[idx];
    imags[idx] = 0.0;

    // In case of uint8 input data, bias from a zero of 0x80 to 0.0
    if (std::is_same<T, uint8_t>::value) {
      reals[idx] -= 128.0;
    }
  }
  FFT(reals.data(), imags.data(), buf_size);

  // Convert real FFT results from frequency domain into sinusoid amplitudes
  //
  // We only feed REAL (not complex) data to the FFT, so return values in
  // reals[] and imags[] only have meaning through buf_sz_2. Thus, for the
  // frequency bins [1 thru buf_sz_2 - 1], we could either add in the identical
  // "negative" (beyond buf_size/2) frequency vals, or multiply by two (with
  // upcoming div-by-buf_size, this becomes div-by-buf_sz_2 for those elements).
  for (uint32_t bin = 1; bin < buf_sz_2; ++bin) {
    reals[bin] /= buf_sz_2;
    imags[bin] /= buf_sz_2;
  }
  // Frequencies 0 & buf_sz_2 are 'half-width' bins, so these bins get reduced
  reals[0] /= buf_size;         // by half during the normalization process.
  imags[0] /= buf_size;         // Specifically compared to the other indices,
  reals[buf_sz_2] /= buf_size;  // we divide the real and imag values by
  imags[buf_sz_2] /= buf_size;  // buf_size instead of buf_sz_2.

  // Calculate magnitude of primary signal (even if out-of-range aliased back!)
  if (freq_out_of_range) {
    freq = buf_size - freq;
  }
  *magn_signal =
      std::sqrt(reals[freq] * reals[freq] + imags[freq] * imags[freq]);

  // Calculate magnitude of all other frequencies
  if (magn_other) {
    double sum_sq_magn_other = 0.0;
    for (uint32_t bin = 0; bin <= buf_sz_2; ++bin) {
      if (bin != freq || freq_out_of_range) {
        sum_sq_magn_other +=
            (reals[bin] * reals[bin] + imags[bin] * imags[bin]);
      }
    }
    *magn_other = std::sqrt(sum_sq_magn_other);
  }
}

template bool CompareBuffers<uint8_t>(const uint8_t*, const uint8_t*, uint32_t,
                                      bool);
template bool CompareBuffers<int16_t>(const int16_t*, const int16_t*, uint32_t,
                                      bool);
template bool CompareBuffers<int32_t>(const int32_t*, const int32_t*, uint32_t,
                                      bool);
template bool CompareBuffers<float>(const float*, const float*, uint32_t, bool);
template bool CompareBuffers<double>(const double*, const double*, uint32_t,
                                     bool);

template bool CompareBufferToVal<uint8_t>(const uint8_t*, uint8_t, uint32_t,
                                          bool);
template bool CompareBufferToVal<int16_t>(const int16_t*, int16_t, uint32_t,
                                          bool);
template bool CompareBufferToVal<int32_t>(const int32_t*, int32_t, uint32_t,
                                          bool);
template bool CompareBufferToVal<float>(const float*, float, uint32_t, bool);

template void GenerateCosine<uint8_t>(uint8_t*, uint32_t, double, bool, double,
                                      double);
template void GenerateCosine<int16_t>(int16_t*, uint32_t, double, bool, double,
                                      double);
template void GenerateCosine<int32_t>(int32_t*, uint32_t, double, bool, double,
                                      double);
template void GenerateCosine<float>(float*, uint32_t, double, bool, double,
                                    double);
template void GenerateCosine<double>(double*, uint32_t, double, bool, double,
                                     double);

template void MeasureAudioFreq<uint8_t>(uint8_t* audio, uint32_t buf_size,
                                        uint32_t freq, double* magn_signal,
                                        double* magn_other = nullptr);
template void MeasureAudioFreq<int16_t>(int16_t* audio, uint32_t buf_size,
                                        uint32_t freq, double* magn_signal,
                                        double* magn_other = nullptr);
template void MeasureAudioFreq<int32_t>(int32_t* audio, uint32_t buf_size,
                                        uint32_t freq, double* magn_signal,
                                        double* magn_other = nullptr);
template void MeasureAudioFreq<float>(float* audio, uint32_t buf_size,
                                      uint32_t freq, double* magn_signal,
                                      double* magn_other = nullptr);

}  // namespace test
}  // namespace audio
}  // namespace media
