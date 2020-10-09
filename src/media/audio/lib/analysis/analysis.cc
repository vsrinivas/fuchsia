// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/lib/analysis/analysis.h"

#include <lib/syslog/cpp/macros.h>

#include <iomanip>
#include <unordered_set>
#include <vector>

#include <fbl/algorithm.h>

#include "src/media/audio/lib/format/traits.h"

namespace media::audio {

namespace internal {

template <typename T>
double SampleToDouble(T value) {
  return static_cast<double>(value);
}

template <>
double SampleToDouble<uint8_t>(uint8_t value) {
  // In case of uint8 input data, bias from a zero of 0x80 to 0.0
  return static_cast<double>(value) - 128.0;
}

// Perform a Fast Fourier Transform on the provided data arrays.
//
// On input, real[] and imag[] contain 'buf_size' number of double-float values in the time domain
// (such as audio samples); buf_size must be a power-of-two.
//
// On output, real[] and imag[] contain 'buf_size' number of double-float values in frequency
// domain, but generally used only through buf_size/2 (per Nyquist).
//
// The classic FFT derivation (based on Cooley-Tukey), and what I implement here, achieves NlogN
// performance (instead of N^2) with divide-and-conquer, while additional optimizing by working
// in-place. To do this, it first breaks the data stream into single elements (so-called interlaced
// decomposition) that are in the appropriate order, and then combines these to form series of
// 2-element matrices, then combines these to form 4-element matrices, and so on, until combining
// the final matrices (each of which is half the size of the original). Two interesting details
// deserve further explanation:
//
// 1. Interlaced decomposition into the "appropriate order" mentioned above is achieved by sorting
// values by index, but in ascending order if viewing the index in bit-reversed manner! (This is
// exactly what is needed in order to combine the pairs of values in the appropriate cross-matrix
// sequence.) So for a stream of 16 values (4 bits of index), this re-sorted order is as follows -
//    0,    8,    4,   12,   2,     10,    6, ...,    7,   15 ... or, in binary:
// 0000, 1000, 0100, 1100, 0010, 11010, 0110, ..., 0111, 1111.
//
// 2. Combining each matrix (called synthesis) is accomplished in the following fashion, regardless
// of size: combining [ac] and [bd] to make [abcd] is done by spacing [ac] into [a0c0] and spacing
// [bd] into [0b0d] and then overlaying them.  The frequency-domain equivalent of making [a0c0] from
// [ac] is simply to turn [AC] into [ACAC]. The equivalent of creating [0b0d] from [bd] is to
// multiply [BD] by a sinusoid (to delay it by one sample) while also duplicating [BD] into [BDBD].
// This results in a 'butterfly' flow (based on the shape of two inputs, two outputs, and the four
// arrows between them).
// Specifically, in each pair of values that are combined:
// even_output = even_input + (sinusoid_factor x odd_input), and
// odd_output  = even input - (sinusoid_factor x odd_input).
// (specifically, this sinusoid is the spectrum of a shifted delta function)
// This butterfly operation transforms two complex points into two other complex points, combining
// two 1-element signals into one 2-element signal (etc).
//
// Classic DSP texts by Oppenheim, Schaffer, Rabiner, or the Cooley-Tukey paper itself, are
// serviceable references for these concepts.
//
// TODO(mpuryear): Consider std::complex<double> instead of real/imag arrays.
void FFT(double* reals, double* imags, uint32_t buf_size) {
  FX_DCHECK(fbl::is_pow2(buf_size));
  const uint32_t buf_sz_2 = buf_size >> 1;

  uint32_t N = 0;
  while (buf_size > (1 << N)) {
    ++N;
  }

  // First, perform a bit-reversal sort of indices. Again, this is done so that all subsequent
  // matrix-merging work can be done on adjacent values. This sort implementation performs the
  // minimal number of swaps/moves (considering buf_size could be 128K, 256K or more), but is
  // admittedly more difficult to follow than some.
  // When debugging, remember 1) each swap moves both vals to final locations, 2) each val is
  // touched once or not at all, and 3) the final index ordering is **ascending if looking at
  // indices in bit-reversed fashion**.
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

  // Loop through log2(buf_size) stages: one for each power of two, starting with 2, then 4, then 8,
  // .... During each stage, combine pairs of shorter signals (of length 'sub_dft_sz_2') into
  // single, longer signals (of length 'sub_dft_sz'). From previous sorting, signals to be combined
  // are adjacent.
  for (uint32_t fft_level = 1; fft_level <= N; ++fft_level) {
    const uint32_t sub_dft_sz = 1 << fft_level;     // length of combined signal
    const uint32_t sub_dft_sz_2 = sub_dft_sz >> 1;  // length of shorter signals
    // 'Odd' values are multiplied by complex (real & imaginary) factors before being combined with
    // 'even' values. These coefficients help the real and imaginary factors advance correctly,
    // within each sub_dft.
    const double real_coef = std::cos(M_PI / static_cast<double>(sub_dft_sz_2));
    const double imag_coef = -std::sin(M_PI / static_cast<double>(sub_dft_sz_2));

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

// Calculate phase for a given complex number, spanning [-PI, PI].
double GetPhase(double real, double imag) {
  if (real == 0.0f) {
    real = 1e-20;
  }
  if (imag < 1e-19 && imag > -1e-19) {
    imag = 0.0;
  }
  double phase = std::atan(imag / real);

  if (real < 0.0) {
    if (imag < 0.0) {
      phase -= M_PI;
    } else {
      phase += M_PI;
    }
  }
  return phase;
}

// Convert 2 incoming arrs (reals & imags == x & y) into magnitude and phase arrs. Magnitude is
// absolute value, phase is in radians with range (-PI, PI].
void RectangularToPolar(const double* reals, const double* imags, uint32_t buf_size, double* magn,
                        double* phase) {
  for (uint32_t freq = 0; freq < buf_size; ++freq) {
    double sum_sq = (reals[freq] * reals[freq]) + (imags[freq] * imags[freq]);
    magn[freq] = std::sqrt(sum_sq);
  }
  if (phase) {
    for (uint32_t freq = 0; freq < buf_size; ++freq) {
      phase[freq] = GetPhase(reals[freq], imags[freq]);
    }
  }
}

// Perform the Discrete Fourier Transform, converting time-domain reals[] (len buf_size) into
// freq-domain real_freq[] & imag_freq[], both (buf_size/2 + 1). This is a simple, unoptimized
// (N^2)/2 implementation.
void RealDFT(const double* reals, uint32_t buf_size, double* real_freq, double* imag_freq) {
  FX_DCHECK((buf_size & 1u) == 0) << "DFT buffer size must be even";

  const double multiplier = M_PI * 2.0 / static_cast<double>(buf_size);
  const uint32_t buf_sz_2 = buf_size >> 1;

  for (uint32_t freq = 0; freq <= buf_sz_2; ++freq) {
    const double freq_mult = multiplier * static_cast<double>(freq);
    double real = 0.0;
    double imag = 0.0;
    for (uint32_t idx = 0; idx < buf_size; ++idx) {
      const double idx_mult = freq_mult * static_cast<double>(idx);

      real += (std::cos(idx_mult) * reals[idx]);
      imag -= (std::sin(idx_mult) * reals[idx]);
    }
    real_freq[freq] = real;
    imag_freq[freq] = imag;
  }
}

// Converts frequency-domain arrays real_freq & imag_freq (len buf_size/2 + 1) into time-domain
// array reals (len buf_size). This is a simple, unoptimized (N^2)/2 implementation.
void InverseDFT(double* real_freq, double* imag_freq, uint32_t buf_size, double* reals) {
  uint32_t buf_sz_2 = buf_size >> 1;

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    real_freq[idx] = real_freq[idx] / static_cast<double>(buf_sz_2);
    imag_freq[idx] = -imag_freq[idx] / static_cast<double>(buf_sz_2);
  }
  real_freq[0] /= 2.0;
  real_freq[buf_sz_2] /= 2.0;

  double mult = M_PI * 2.0 / static_cast<double>(buf_size);
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    double idx_mult = mult * idx;
    double val = 0.0;
    for (uint32_t freq = 0; freq <= buf_sz_2; ++freq) {
      double freq_mult = idx_mult * freq;
      val += (real_freq[freq] * std::cos(freq_mult));
      val += (imag_freq[freq] * std::sin(freq_mult));
    }
    reals[idx] = val;
  }
}

// Converts frequency-domain arrays reals & imags (len buf_size) in-place into time-domain arrays
// (also len buf_size)
void InverseFFT(double* reals, double* imags, uint32_t buf_size) {
  FX_DCHECK(fbl::is_pow2(buf_size));

  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    imags[idx] = -imags[idx];
  }

  FFT(reals, imags, buf_size);

  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    reals[idx] = reals[idx] / static_cast<double>(buf_size);
    imags[idx] = -imags[idx] / static_cast<double>(buf_size);
  }
}

}  // namespace internal

// For specified audio buffer & length, analyze the contents and return the magnitude (and phase) of
// signal at given frequency (i.e. frequency at which 'freq' periods fit perfectly within buffer
// length). Also return the magnitude of all other content. Useful for frequency response and
// signal-to-noise. Internally uses an FFT, so slice.NumFrames() must be a power-of-two.
template <fuchsia::media::AudioSampleFormat SampleFormat>
AudioFreqResult MeasureAudioFreqs(AudioBufferSlice<SampleFormat> slice,
                                  std::unordered_set<size_t> freqs) {
  FX_CHECK(fbl::is_pow2(slice.NumFrames()));
  FX_CHECK(slice.format().channels() == 1);

  const size_t buf_size = slice.NumFrames();
  const size_t buf_sz_2 = buf_size >> 1;

  // Copy input to double buffer, before doing a high-res FFT (freq-analysis). Note that we set
  // imags[] to zero: MeasureAudioFreq retrieves a REAL (not Complex) FFT for the data, the return
  // real and imaginary frequency-domain data only spans 0...N/2 (inclusive).
  std::vector<double> reals(buf_size);
  std::vector<double> imags(buf_size);
  for (size_t frame = 0; frame < buf_size; ++frame) {
    reals[frame] = internal::SampleToDouble(slice.SampleAt(frame, 0));
    imags[frame] = 0.0;
  }
  internal::FFT(reals.data(), imags.data(), buf_size);

  // Convert real FFT results from frequency domain into sinusoid amplitudes
  //
  // We only feed REAL (not complex) data to the FFT, so return values in reals[] and imags[] only
  // have meaning through buf_sz_2. Thus, for the frequency bins [1 thru buf_sz_2 - 1], we could
  // either add in the identical "negative" (beyond buf_size/2) frequency vals, or multiply by two
  // (with upcoming div-by-buf_size, this becomes div-by-buf_sz_2 for those elements).
  for (uint32_t bin = 1; bin < buf_sz_2; ++bin) {
    reals[bin] /= buf_sz_2;
    imags[bin] /= buf_sz_2;
  }
  // Frequencies 0 & buf_sz_2 are 'half-width' bins, so these bins get reduced
  reals[0] /= buf_size;         // by half during the normalization process.
  imags[0] /= buf_size;         // Specifically compared to the other indices,
  reals[buf_sz_2] /= buf_size;  // we divide the real and imag values by
  imags[buf_sz_2] /= buf_size;  // buf_size instead of buf_sz_2.

  AudioFreqResult out;

  for (uint32_t bin = 0; bin <= buf_sz_2; ++bin) {
    out.all_square_magnitudes.push_back(reals[bin] * reals[bin] + imags[bin] * imags[bin]);
  }

  // Calculate magnitude and phase of primary signal.
  double sum_sq_magn_signal = 0.0;
  for (auto freq : freqs) {
    FX_CHECK(freq <= buf_sz_2);
    auto mag2 = out.all_square_magnitudes[freq];
    sum_sq_magn_signal += mag2;
    out.magnitudes[freq] = std::sqrt(mag2);
    out.phases[freq] = internal::GetPhase(reals[freq], imags[freq]);
  }
  out.total_magn_signal = std::sqrt(sum_sq_magn_signal);

  // Calculate magnitude of all other frequencies
  double sum_sq_magn_other = 0.0;
  for (uint32_t bin = 0; bin <= buf_sz_2; ++bin) {
    if (freqs.count(bin) == 0) {
      sum_sq_magn_other += out.all_square_magnitudes[bin];
    }
  }
  out.total_magn_other = std::sqrt(sum_sq_magn_other);

  return out;
}

template <fuchsia::media::AudioSampleFormat SampleFormat>
AudioBuffer<SampleFormat> MultiplyByTukeyWindow(AudioBufferSlice<SampleFormat> slice,
                                                double alpha) {
  FX_CHECK(alpha <= 1);

  auto out = slice.Clone();
  size_t ramp_length_frames = static_cast<size_t>(alpha / 2 * slice.NumFrames());

  for (size_t frame = 0; frame < ramp_length_frames; ++frame) {
    double x = static_cast<double>(frame) / static_cast<double>(ramp_length_frames);
    double w = 0.5 * (1.0 - std::cos(M_PI * x));
    for (size_t chan = 0; chan < slice.format().channels(); ++chan) {
      size_t a = slice.SampleIndex(frame, chan);
      size_t b = slice.NumSamples() - 1 - a;

      double a_val = w * internal::SampleToDouble(slice.buf()->samples()[a]);
      double b_val = w * internal::SampleToDouble(slice.buf()->samples()[b]);

      out.samples()[a] = static_cast<typename AudioBuffer<SampleFormat>::SampleT>(a_val);
      out.samples()[b] = static_cast<typename AudioBuffer<SampleFormat>::SampleT>(b_val);
    }
  }

  return out;
}

// Explicitly instantiate all possible implementations.
#define INSTANTIATE(T)                                                                            \
  template AudioFreqResult MeasureAudioFreqs<T>(AudioBufferSlice<T>, std::unordered_set<size_t>); \
  template AudioBuffer<T> MultiplyByTukeyWindow<T>(AudioBufferSlice<T>, double);

INSTANTIATE_FOR_ALL_FORMATS(INSTANTIATE)

}  // namespace media::audio
