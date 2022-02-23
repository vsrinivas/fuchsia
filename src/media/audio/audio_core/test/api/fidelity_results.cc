// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#include "src/media/audio/audio_core/test/api/fidelity_results.h"

#include <array>

#include "src/media/audio/lib/test/hermetic_fidelity_test.h"

namespace media::audio::test {

// SiNAD (Signal-to-Noise and Distortion) is an indicator of audio quality, measured in decibels.
// Given a recognizable input signal that produces a given output, SiNAD is the ratio between the
// strength of the recognized signal in that output, versus the combined strengths of all other
// (unintended) output components. Ideal SiNAD is generally defined (in dB) as
//    B * 6.0206 + 1.76
// where B is the number of bits, when quantizing input and calculating output;
//       Constant factor 6.0206 converts bits to decibels: log10(2) * 20
//       Constant factor 1.76 represents ideal quantization noise, in RMS dB: 10 * log10(3/2)

/////////////////////////////////////////////////////////////////////////
// Generic pipeline frequency response and signal-to-noise-and-distortion
//
// For source channels with >13 bits of precision, where we expect theoretical best-case response.
const std::array<double, HermeticFidelityTest::kNumReferenceFreqs>
    FidelityResults::kFullScaleLimitsDb = HermeticFidelityTest::FillArray(-0.001);

// Channels fed no input should generate no output. Without effects, we should have no inter-channel
// crosstalk/bleed-through/distortion. Used for frequency response.
const std::array<double, HermeticFidelityTest::kNumReferenceFreqs> FidelityResults::kSilenceDb =
    HermeticFidelityTest::FillArray(-INFINITY);

/////////////////////////////////////////////////
// Format-specific (but not rate-specific) limits
//
// (1) uint8 source: theoretical best-case output is 127/128, or -0.0681 dB
const std::array<double, HermeticFidelityTest::kNumReferenceFreqs> FidelityResults::kUint8LimitsDb =
    HermeticFidelityTest::FillArray(-0.069);
// uint8 SiNAD reflects 8 bits of precision (8 * 6.0206db == 48.165 db, plus ~ 1.76 db).
const std::array<double, HermeticFidelityTest::kNumReferenceFreqs>
    FidelityResults::kUint8SinadLimitsDb = HermeticFidelityTest::FillArray(49.952);

// (2) int16 source: theoretical best-case output is 32767/32768, or -0.000265 dB. We measure at
// .001 precision, so instead of a specific kInt16LimitsDb we use kFullScaleLimitsDb.
//
// int16 SiNAD reflects 16 bits of precision (16 * 6.0206 == 96.330 db, plus ~ 1.76 db).
const std::array<double, HermeticFidelityTest::kNumReferenceFreqs>
    FidelityResults::kInt16SinadLimitsDb = HermeticFidelityTest::FillArray(98.104);

// (3) int24 source: theoretical best-case output is 8388607/8388608, or -0.000001 dB. We measure at
// .001 precision, so instead of a specific kInt24LimitsDb we use kFullScaleLimitsDb.
//
// int24 SiNAD reflects 24 bits of precision (24 * 6.0206 == 144.494 db, plus ~ 1.76 db).
const std::array<double, HermeticFidelityTest::kNumReferenceFreqs>
    FidelityResults::kInt24SinadLimitsDb = HermeticFidelityTest::FillArray(146.309);

// (4) float32 source: max output is 1.0. kFullScaleLimitsDb instead of a specific kFloat32LimitsDb.
//
// float32 SiNAD reflects 25 bits of precision (25 * 6.0206 == 150.515, plus a moveable decimal
// point that increases the usual 1.76 db quantization factor to > 3 db).
// (Why 25 bits, with 8 bits of exponent?  sign + implicit "1." + 23 bits of fractional mantissa)
const std::array<double, HermeticFidelityTest::kNumReferenceFreqs>
    FidelityResults::kFloat32SinadLimitsDb = HermeticFidelityTest::FillArray(153.745);

////////////////////////////////////////////////////
// Rate-specific frequency response and sinad limits
//
// These thresholds are specific to resamplers with low-pass (and/or high-pass) filtering.
// When upsampling, FR/sinad show the effects of low-pass filtering as we near the Nyquist limit.
// When downsampling, sinad shows imperfect but increasing out-of-band rejection of source
// frequencies beyond the output's Nyquist limit.

//
// Single-MixStage frequency response and sinad limits, running at 48k
//
// (1) PointSampler (48kPoint48k): expect kFullScaleLimitsDb and kFloat32SinadLimitsDb.
//
// (2) SincSampler-at-unity (48kMicro48k): expect kFullScaleLimitsDb and kFloat32SinadLimitsDb.
//
// (3) 44.1k stream, resampled to 48k
const std::array<double, HermeticFidelityTest::kNumReferenceFreqs>
    FidelityResults::k44100To48kLimitsDb = {
        // clang-format off
   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,
   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,
   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,
   -0.001,   -0.001,   -0.297,   -1.900,   -5.740,    0.000,    0.000,    0.000,    0.000,    0.000,
    0.000,    0.000,
        // clang-format on
};
const std::array<double, HermeticFidelityTest::kNumReferenceFreqs>
    FidelityResults::k44100To48kSinadLimitsDb = {
        // clang-format off
  153.745,   91.990,   91.994,   91.999,   92.005,   92.015,   92.032,   92.055,   92.098,   92.163,
   92.261,   92.439,   92.695,   93.109,   93.827,   95.092,   97.218,  101.406,  107.035,   98.261,
   93.125,   91.170,   94.021,   96.243,   89.236,   93.624,   87.789,   84.565,   82.241,   84.075,
   72.547,   63.392,   29.185,   12.237,    0.563,  153.745,  153.745,  153.745,  153.745,  153.745,
  153.745,  153.745,
        // clang-format on
};

// (4) 44.1k stream synchronizing to a custom clock, resampled to 48k
// FR: k44100Micro48kLimitsDb is identical to k44100To48kLimitsDb
const std::array<double, HermeticFidelityTest::kNumReferenceFreqs>
    FidelityResults::k44100Micro48kSinadLimitsDb = {
        // clang-format off
  153.745,   91.990,   91.994,   91.999,   92.003,   92.013,   92.024,   92.055,   92.079,   92.132,
   92.261,   92.439,   92.695,   92.879,   93.422,   94.245,   97.218,  101.406,  107.037,   92.453,
   89.443,   87.850,   94.021,   84.893,   82.554,   93.623,   87.788,   84.565,   74.672,   73.502,
   69.173,   63.392,   29.185,   13.564,    0.563,  153.745,  153.745,  153.745,  153.745,  153.745,
  153.745,  153.745,
        // clang-format on
};

// (5) 96k stream synchronizing to a custom clock, resampled to 48k
const std::array<double, HermeticFidelityTest::kNumReferenceFreqs>
    FidelityResults::k96kMicro48kLimitsDb = {
        // clang-format off
   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,
   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,
   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,
   -0.002,   -0.002,   -0.001,   -0.001,   -0.500,   -2.256,   -3.833,    0.000,    0.000,    0.000,
    0.000,    0.000,
        // clang-format on
};
const std::array<double, HermeticFidelityTest::kNumReferenceFreqs>
    FidelityResults::k96kMicro48kSinadLimitsDb = {
        // clang-format off
  141.104,  141.104,  141.079,  141.160,  141.134,  141.090,  141.091,  141.103,  141.170,  141.157,
  141.119,  141.114,  141.096,  141.136,  141.178,  141.181,  141.191,  141.217,  141.278,  141.254,
  141.269,  141.367,  141.425,  141.491,  141.595,  141.755,  141.938,  142.045,  141.974,  106.849,
  142.804,  142.880,   62.410,   58.146,  142.313,  140.775,  139.485,    8.953,   12.817,   25.059,
   49.353,   66.795,
        // clang-format on
};

//
// Single-MixStage frequency response and sinad limits, running at 96k
//
// (1) Ultrasound (96k point-sampled to 96k): expect kFullScaleLimitsDb and kFloat32SinadLimitsDb.
//
// (2) 48k stream, resampled to 96k
const std::array<double, HermeticFidelityTest::kNumReferenceFreqs>
    FidelityResults::k48kTo96kLimitsDb = {
        // clang-format off
   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,
   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,
   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,
   -0.001,   -0.002,   -0.001,   -0.001,   -0.500,   -2.253,   -3.835,    0.000,    0.000,    0.000,
    0.000,    0.000,
        // clang-format on
};
const std::array<double, HermeticFidelityTest::kNumReferenceFreqs>
    FidelityResults::k48kTo96kSinadLimitsDb = {
        // clang-format off
  153.745,   88.963,   88.966,   88.969,   88.974,   88.983,   88.999,   89.019,   89.056,   89.111,
   89.190,   89.340,   89.565,   89.916,   90.506,   91.558,   93.303,   96.770,  109.572,  100.168,
   92.268,   88.848,   89.922,  108.221,   89.050,   94.242,   86.834,   90.091,   88.074,   80.937,
   81.349,   75.860,   49.612,   47.307,   24.547,   10.570,    5.113,  153.745,  153.745,  153.745,
  153.745,  153.745,
        // clang-format on
};

//
// Two-MixStage cases involving resamplers in series.
//
// (1) 24k stream, resampled to 48k, then resampled to 96k
const std::array<double, HermeticFidelityTest::kNumReferenceFreqs>
    FidelityResults::k24kTo48kTo96kLimitsDb = {
        // clang-format off
   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,
   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,
   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.002,   -0.001,
    0.000,    0.000,    0.000,    0.000,    0.000,    0.000,    0.000,    0.000,    0.000,    0.000,
    0.000,    0.000,
        // clang-format on
};
const std::array<double, HermeticFidelityTest::kNumReferenceFreqs>
    FidelityResults::k24kTo48kTo96kSinadLimitsDb = {
        // clang-format off
  153.745,   87.143,   87.149,   87.156,   87.166,   87.184,   87.215,   87.254,   87.328,   87.436,
   87.591,   87.884,   88.316,   88.969,   89.998,   91.516,   92.857,   92.622,   91.004,   91.230,
   92.186,   86.216,   88.684,   86.925,   86.449,   87.079,   79.982,   83.641,   75.546,   49.339,
  153.745,  153.745,  153.745,  153.745,  153.745,  153.745,  153.745,  153.745,  153.745,  153.745,
  153.745,  153.745,
        // clang-format on
};

// (2) 96k stream, resampled to 48k, then resampled to 96k
const std::array<double, HermeticFidelityTest::kNumReferenceFreqs>
    FidelityResults::k96kTo48kTo96kLimitsDb = {
        // clang-format off
   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,
   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,
   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.001,   -0.002,   -0.001,   -0.001,    0.001,
   -0.002,   -0.004,   -0.001,   -0.001,   -1.001,   -4.507,   -7.671,    0.000,    0.000,    0.000,
    0.000,    0.000,
        // clang-format on
};
const std::array<double, HermeticFidelityTest::kNumReferenceFreqs>
    FidelityResults::k96kTo48kTo96kSinadLimitsDb = {
        // clang-format off
  144.494,   88.963,   88.965,   88.969,   88.974,   88.983,   88.999,   89.019,   89.056,   89.111,
   89.190,   89.341,   89.566,   89.918,   90.509,   91.562,   93.306,   96.765,  109.565,  100.167,
   92.268,   88.848,   89.922,  108.219,   89.051,   94.244,   86.834,   90.094,   88.074,   80.873,
   81.349,   75.860,   48.896,   46.510,   24.547,   10.570,    5.113,   11.617,   14.711,   25.532,
   49.338,   66.776,
        // clang-format on
};

}  // namespace media::audio::test
