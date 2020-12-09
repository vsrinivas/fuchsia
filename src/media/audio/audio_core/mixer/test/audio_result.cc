// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/test/audio_result.h"

#include <cstdio>
#include <string>

#include "src/media/audio/audio_core/mixer/test/mixer_tests_shared.h"

namespace media::audio::test {

// See audio_result.h for in-depth descriptions of these class members/consts.
//
// In summary, however:
// * For all TOLERANCE measurements, smaller is better (tighter tolerance). Measured results must be
//   WITHIN the tolerance.
// * For ALL other measurements (frequency response, SINAD, level, noise floor), larger results are
//   better (e.g. frequency response closer to 0, higher noise floor or SINAD).

//
//
// Input
//
double AudioResult::LevelToleranceSource8 = 0.0;
double AudioResult::LevelToleranceSource16 = 0.0;
double AudioResult::LevelToleranceSource24 = 0.0;
double AudioResult::LevelToleranceSourceFloat = 0.0;

constexpr double AudioResult::kPrevLevelToleranceSource8;
constexpr double AudioResult::kPrevLevelToleranceSource16;
constexpr double AudioResult::kPrevLevelToleranceSource24;
constexpr double AudioResult::kPrevLevelToleranceSourceFloat;

double AudioResult::LevelSource8 = -INFINITY;
double AudioResult::LevelSource16 = -INFINITY;
double AudioResult::LevelSource24 = -INFINITY;
double AudioResult::LevelSourceFloat = -INFINITY;

double AudioResult::FloorSource8 = -INFINITY;
double AudioResult::FloorSource16 = -INFINITY;
double AudioResult::FloorSource24 = -INFINITY;
double AudioResult::FloorSourceFloat = -INFINITY;

constexpr double AudioResult::kPrevFloorSource8;
constexpr double AudioResult::kPrevFloorSource16;
constexpr double AudioResult::kPrevFloorSource24;
constexpr double AudioResult::kPrevFloorSourceFloat;

//
//
// Rechannel
//
double AudioResult::LevelToleranceStereoMono = 0.0;
constexpr double AudioResult::kPrevLevelToleranceStereoMono;

double AudioResult::LevelStereoMono = -INFINITY;
constexpr double AudioResult::kPrevLevelStereoMono;

double AudioResult::FloorStereoMono = -INFINITY;
constexpr double AudioResult::kPrevFloorStereoMono;

//
//
// Interpolate
//
double AudioResult::LevelToleranceInterpolation = 0.0;
constexpr double AudioResult::kPrevLevelToleranceInterpolation;

std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespPointUnity = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespPointDown0 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespPointDown1 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespPointDown2 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespPointMicro = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespPointUp1 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespPointUp2 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespPointUp3 = {NAN};

std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespLinearUnity = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespLinearDown0 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespLinearDown1 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespLinearDown2 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespLinearMicro = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespLinearUp1 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespLinearUp2 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespLinearUp3 = {NAN};

std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespSincUnity = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespSincDown0 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespSincDown1 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespSincDown2 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespSincMicro = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespSincUp1 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespSincUp2 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespSincUp3 = {NAN};

std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespPointNxN = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::FreqRespLinearNxN = {NAN};

// We test our interpolation fidelity across these six rate-conversion ratios:
// - 1:1 (referred to in these variables and constants as Unity)
// - 191999:48000, significant but not perfectly integral down-sampling (referred to as Down0)
// - 2:1, which equates to 96k -> 48k (Down1)
// - 294:160, which equates to 88.2k -> 48k (Down2)
// - 48001:48000, representing small adjustment for multi-device sync (Micro)
// - 147:160, which equates to 44.1k -> 48k (Up1)
// - 1:2, which equates to 24k -> 48k, or 48k -> 96k (Up2)
// - 12001:48000, significant but not perfectly integral up-sampling (Up3)
//
// For Frequency Response, values closer to 0 (flatter response) are desired. Below you see that for
// 1:1 and 2:1, response is near-ideal. For other rates, response drops off at higher frequencies.
//
// clang-format off
const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespPointUnity = {
       0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespPointDown0 = {
       0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
       0.000,     0.000,     0.000,     0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.002,    -0.003,    -0.004,    -0.007,    -0.010,    -0.016,    -0.025,
      -0.039,    -0.056,    -0.100,    -0.148,    -0.156,    -0.164,    -0.172,    -0.190,    -0.214      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespPointDown1 = {
       0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespPointDown2 = {
       0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.002,
      -0.002,    -0.003,    -0.005,    -0.008,    -0.012,    -0.019,    -0.030,    -0.046,    -0.074,    -0.119,
      -0.185,    -0.266,    -0.476,    -0.711,    -0.748,    -0.785,    -0.826,    -0.913,    -1.032      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespPointMicro = {
       0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.002,    -0.002,    -0.003,    -0.005,
      -0.007,    -0.010,    -0.016,    -0.025,    -0.039,    -0.062,    -0.100,    -0.156,    -0.248,    -0.401,
      -0.630,    -0.912,    -1.650,    -2.501,    -2.640,    -2.783,    -2.931,    -3.256,    -3.707      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespPointUp1 = {
       0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.002,    -0.002,    -0.003,    -0.005,
      -0.008,    -0.012,    -0.019,    -0.030,    -0.046,    -0.074,    -0.118,    -0.185,    -0.294,    -0.476,
      -0.748,    -1.085,    -1.969,    -2.999,    -3.168,    -3.344,    -3.524,    -3.922, -INFINITY      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespPointUp2 = {
       0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.002,    -0.002,    -0.003,    -0.005,    -0.008,    -0.012,
      -0.019,    -0.029,    -0.048,    -0.075,    -0.117,    -0.186,    -0.302,    -0.474,    -0.761,    -1.249,
      -2.010,    -3.010, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespPointUp3 = {
       0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.002,    -0.003,    -0.004,    -0.007,    -0.010,    -0.016,    -0.025,    -0.040,    -0.064,
      -0.100,    -0.155,    -0.256,    -0.401,    -0.629,    -1.008,    -1.650,    -2.640, -INFINITY, -INFINITY,
   -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespLinearUnity = {
       0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespLinearDown0 = {
       0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.002,    -0.002,    -0.004,    -0.005,    -0.008,    -0.013,    -0.020,    -0.031,    -0.050,
      -0.078,    -0.112,    -0.199,    -0.296,    -0.312,    -0.327,    -0.344,    -0.379,    -0.427      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespLinearDown1 = {
       0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespLinearDown2 = {
       0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.002,    -0.003,
      -0.004,    -0.006,    -0.010,    -0.015,    -0.023,    -0.037,    -0.059,    -0.092,    -0.147,    -0.236,
      -0.369,    -0.533,    -0.951,    -1.420,    -1.496,    -1.573,    -1.652,    -1.824,    -2.061      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespLinearMicro = {
       0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.002,    -0.002,    -0.004,    -0.005,    -0.008,
      -0.013,    -0.020,    -0.032,    -0.050,    -0.078,    -0.124,    -0.199,    -0.312,    -0.496,    -0.801,
      -1.258,    -1.824,    -3.299,    -5.002,    -5.280,    -5.567,    -5.863,    -6.514,    -7.419      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespLinearUp1 = {
        0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
       -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.002,    -0.003,    -0.004,    -0.006,    -0.010,
       -0.015,    -0.023,    -0.038,    -0.059,    -0.092,    -0.147,    -0.236,    -0.369,    -0.588,    -0.951,
       -1.495,    -2.170,    -3.938,    -5.998,    -6.336,    -6.685,    -7.047,    -7.844, -INFINITY      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespLinearUp2 = {
       0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.002,    -0.003,    -0.004,    -0.006,    -0.010,    -0.015,    -0.024,
      -0.038,    -0.058,    -0.096,    -0.150,    -0.234,    -0.372,    -0.603,    -0.948,    -1.522,    -2.498,
      -4.020,    -6.019, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespLinearUp3 = {
       0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.002,
      -0.002,    -0.004,    -0.006,    -0.008,    -0.013,    -0.020,    -0.032,    -0.050,    -0.079,    -0.127,
      -0.199,    -0.310,    -0.511,    -0.801,    -1.259,    -2.016,    -3.300,    -5.280, -INFINITY, -INFINITY,
   -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespSincUnity = {
       0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespSincDown0 = {
       0.000,     0.000,     0.000,     0.000,     0.000,     0.000,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
       0.000,    -0.002,    -0.002,    -0.002,     0.000,     0.000,     0.000,    -0.550,    -3.474      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespSincDown1 = {
       0.000,     0.000,     0.000,     0.000,     0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
       0.000,    -0.002,    -0.002,    -0.002,     0.000,     0.000,     0.000,    -0.550,    -3.474      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespSincDown2 = {
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,
       0.000,    -0.002,    -0.002,    -0.002,     0.000,     0.000,     0.000,    -0.551,    -3.474      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespSincMicro = {
       0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
       0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
      -0.001,    -0.001,    -0.001,    -0.001,    -0.001,     0.000,     0.000,    -0.001,     0.000,    -0.001,
       0.000,    -0.002,    -0.002,    -0.002,     0.000,     0.000,     0.000,    -0.550,    -3.474      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespSincUp1 = {
       0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
       0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,    -0.001,
      -0.001,    -0.001,    -0.001,    -0.001,     0.000,     0.000,    -0.001,    -0.001,     0.000,    -0.001,
       0.000,     0.000,     0.000,    -0.024,    -0.297,    -0.881,    -1.902,    -6.011, -INFINITY      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespSincUp2 = {
       0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
       0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,    -0.001,    -0.001,    -0.001,
      -0.001,    -0.001,     0.000,     0.000,    -0.001,     0.000,    -0.001,     0.000,    -0.001,    -0.002,
       0.000,    -5.999, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespSincUp3 = {
       0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
       0.000,     0.000,     0.000,     0.000,    -0.001,    -0.001,    -0.001,    -0.001,    -0.001,     0.000,
       0.000,    -0.001,     0.000,    -0.001,     0.000,    -0.001,    -0.002,     0.000, -INFINITY, -INFINITY,
   -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespPointNxN = {
   -INFINITY,     0.000,     0.000,     0.000,     0.000,     0.000,    -0.001,     0.000,     0.000,     0.000,
       0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
      -0.007,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
       0.000,    -0.912,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000      };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespLinearNxN = {
   -INFINITY,     0.000,     0.000,     0.000,     0.000,     0.000,    -0.001,     0.000,     0.000,     0.000,
       0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
      -0.013,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
       0.000,    -1.824,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000      };
// clang-format on

std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadPointUnity = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadPointDown0 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadPointDown1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadPointDown2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadPointMicro = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadPointUp1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadPointUp2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadPointUp3 = {NAN};

std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadLinearUnity = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadLinearDown0 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadLinearDown1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadLinearDown2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadLinearMicro = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadLinearUp1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadLinearUp2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadLinearUp3 = {NAN};

std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadSincUnity = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadSincDown0 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadSincDown1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadSincDown2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadSincMicro = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadSincUp1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadSincUp2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadSincUp3 = {NAN};

std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadPointNxN = {-INFINITY};
std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::SinadLinearNxN = {-INFINITY};

// We test our interpolation fidelity across these six rate-conversion ratios:
// - 1:1 (referred to in these variables and constants as Unity)
// - 191999:48000, significant but not perfectly integral down-sampling (referred to as Down0)
// - 2:1, which equates to 96k -> 48k (Down1)
// - 294:160, which equates to 88.2k -> 48k (Down2)
// - 48001:48000, representing small adjustment for multi-device sync (Micro)
// - 147:160, which equates to 44.1k -> 48k (Up1)
// - 1:2, which equates to 24k -> 48k, or 48k -> 96k (Up2)
// - 12001:48000, significant but not perfectly integral up-sampling (Up3)
//
// For SINAD, higher values (lower noise/artifacts vs. signal) are desired. For 1:1 and 2:1, SINAD
// is near-ideal. For other rates, performance drops off (lower values) at higher frequencies.
//
// clang-format off
const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadPointUnity = {
      160.000,   153.714,   153.745,   153.745,   153.714,   153.745,   153.745,   153.745,   153.745,   153.745,
      153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,
      153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,
      153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadPointDown0 = {
      160.000,    78.100,    75.968,    74.574,    72.571,    70.944,    68.714,    66.678,    64.610,    62.450,
       60.464,    58.641,    56.311,    54.539,    52.543,    50.509,    48.535,    46.510,    44.519,    42.442,
       40.508,    38.574,    36.417,    34.478,    32.534,    30.525,    28.449,    26.508,    24.496,    22.418,
       20.472,    18.878,    16.353,    14.604,    14.379,    14.160,    13.946,    13.512,    12.980,     0.219,
       -0.001,    -0.001,    -0.001,    -0.001,    -0.001,     0.000,     0.000,     0.000,     0.001,     1.563,
        2.229,     2.256      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadPointDown1 = {
      160.000,   153.714,   153.745,   153.745,   153.714,   153.745,   153.745,   153.745,   153.745,   153.745,
      153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,
      153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,
      153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,    -0.000,
        0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadPointDown2 = {
      160.000,    71.336,    69.207,    67.815,    65.812,    64.186,    61.956,    59.920,    57.853,    55.693,
       53.707,    51.884,    49.554,    47.782,    45.787,    43.752,    41.778,    39.753,    37.762,    35.685,
       33.751,    31.816,    29.658,    27.717,    25.772,    23.760,    21.679,    19.730,    17.705,    15.605,
       13.628,    11.995,     9.369,     7.505,     7.262,     7.027,     6.790,     6.313,     5.721,     0.967,
        0.000,    -0.001,    -0.001,    -0.001,    -0.001,     0.000,    -0.001,     0.000, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadPointMicro = {
      160.000,    66.059,    63.927,    62.533,    60.530,    58.903,    56.673,    54.636,    52.569,    50.409,
       48.423,    46.600,    44.269,    42.498,    40.502,    38.467,    36.493,    34.468,    32.476,    30.398,
       28.464,    26.527,    24.367,    22.422,    20.471,    18.449,    16.352,    14.379,    12.315,    10.150,
        8.073,     6.315,     3.354,     1.087,     0.775,     0.467,     0.160,    -0.479,    -1.297,     2.256,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadPointUp1 = {
      160.000,    65.316,    63.187,    61.794,    59.792,    58.166,    55.936,    53.900,    51.833,    49.673,
       47.687,    45.863,    43.533,    41.762,    39.766,    37.731,    35.757,    33.732,    31.740,    29.662,
       27.727,    25.790,    23.629,    21.683,    19.731,    17.706,    15.605,    13.626,    11.551,     9.369,
        7.263,     5.471,     2.414,     0.023,    -0.310,    -0.643,    -0.972,    -1.664, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadPointUp2 = {
      160.000,    61.281,    59.152,    57.759,    55.757,    54.130,    51.901,    49.865,    47.798,    45.637,
       43.652,    41.828,    39.498,    37.726,    35.730,    33.695,    31.720,    29.695,    27.701,    25.622,
       23.684,    21.742,    19.573,    17.616,    15.645,    13.590,    11.439,     9.383,     7.180,     4.772,
        2.302,     0.002, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadPointUp3 = {
      160.000,    54.018,    51.882,    50.489,    48.488,    46.861,    44.631,    42.595,    40.528,    38.368,
       36.382,    34.558,    32.227,    30.455,    28.458,    26.421,    24.443,    22.413,    20.413,    18.322,
       16.367,    14.399,    12.182,    10.155,     8.072,     5.830,     3.353,     0.775, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadLinearUnity = {
      160.000,   153.714,   153.745,   153.745,   153.714,   153.745,   153.745,   153.745,   153.745,   153.745,
      153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,
      153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,
      153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadLinearDown0 = {
      160.000,   150.094,   149.187,   148.351,   146.706,   144.990,   142.020,   138.825,   135.269,   131.314,
      127.563,   124.036,   119.477,   115.977,   112.019,   107.972,   104.036,    99.996,    96.018,    91.868,
       88.003,    84.135,    79.821,    75.942,    72.054,    68.033,    63.878,    59.990,    55.957,    51.786,
       47.870,    44.655,    39.535,    35.961,    35.499,    35.049,    34.608,    33.713,    32.613,     0.443,
        0.464,     0.484,     0.523,     0.606,     0.767,     1.232,     1.486,     1.492,     1.725,     1.586,
        3.253,     4.297      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadLinearDown1 = {
      160.000,   153.714,   153.745,   153.745,   153.714,   153.745,   153.745,   153.745,   153.745,   153.745,
      153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,
      153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,
      153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,    -0.000,
        0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadLinearDown2 = {
      160.000,   145.493,   142.766,   140.722,   137.372,   134.536,   130.422,   126.535,   122.510,   118.265,
      114.333,   110.711,   106.070,   102.537,    98.552,    94.487,    90.541,    86.493,    82.510,    78.356,
       74.488,    70.617,    66.298,    62.413,    58.516,    54.479,    50.299,    46.374,    42.281,    38.011,
       33.951,    30.561,    25.008,    20.969,    20.434,    19.909,    19.392,    18.335,    17.015,     2.014,
        2.155,     2.239,     2.409,     2.760,     3.383,     4.591,     4.771,     4.771, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadLinearMicro = {
      160.000,   137.769,   134.032,   131.468,   127.678,   124.544,   120.182,   116.168,   112.070,   107.773,
      103.814,   100.175,    95.520,    91.980,    87.990,    83.922,    79.973,    75.923,    71.938,    67.780,
       63.907,    60.029,    55.698,    51.794,    47.869,    43.787,    39.533,    35.499,    31.230,    26.676,
       22.208,    18.337,    11.619,     6.339,     5.609,     4.885,     4.162,     2.660,     0.730,     4.297,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadLinearUp1 = {
      160.000,   136.515,   132.679,   130.093,   126.266,   123.110,   118.737,   114.712,   110.607,   106.306,
      102.345,    98.704,    94.049,    90.508,    86.518,    82.450,    78.500,    74.450,    70.464,    66.306,
       62.432,    58.551,    54.217,    50.309,    46.377,    42.283,    38.009,    33.947,    29.633,    25.007,
       20.438,    16.448,     9.440,     3.841,     3.059,     2.281,     1.503,    -0.123, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadLinearUp2 = {
      160.000,   122.552,   118.300,   115.517,   111.513,   108.262,   103.802,    99.730,    95.596,    91.276,
       87.304,    83.657,    78.996,    75.453,    71.461,    67.391,    63.441,    59.390,    55.403,    51.244,
       47.368,    43.485,    39.147,    35.233,    31.291,    27.181,    22.879,    18.767,    14.361,     9.545,
        4.604,     0.004, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadLinearUp3 = {
      160.000,   114.922,   110.705,   107.934,   103.939,   100.696,    96.243,    92.174,    88.042,    83.722,
       79.750,    76.103,    71.440,    67.893,    63.895,    59.816,    55.851,    51.776,    47.752,    43.530,
       39.564,    35.540,    30.952,    26.686,    22.207,    17.253,    11.617,     5.609, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadSincUnity = {
      160.000,   153.714,   153.745,   153.745,   153.714,   153.745,   153.745,   153.745,   153.745,   153.745,
      153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,
      153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,
      153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745,   153.745, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadSincDown0 = {
      129.402,   133.966,   133.944,   133.892,   133.856,   133.825,   133.818,   133.600,   133.476,   133.162,
      132.762,   132.216,   131.286,   130.383,   129.155,   127.705,   126.118,   124.358,   122.546,   120.582,
      118.714,   116.822,   114.701,   112.782,   110.851,   108.850,   106.782,   104.845,   102.837,   100.767,
       98.830,    97.247,    94.749,    93.029,    92.809,    92.595,    92.382,    91.962,    91.445,     6.020,
        8.953,    12.817,    25.034,    49.327,    69.102,    88.851,    97.578,    94.967,    99.620,    95.011,
      113.842,   122.436      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadSincDown1 = {
      160.000,   138.942,   138.949,   138.935,   138.826,   138.936,   138.912,   138.866,   138.944,   138.889,
      138.905,   138.934,   138.885,   138.948,   138.862,   138.902,   138.966,   138.924,   138.879,   139.009,
      138.986,   138.942,   138.944,   138.937,   138.968,   139.014,   138.849,   139.028,   138.963,   138.882,
      138.961,   139.032,   138.955,   138.971,   138.788,   138.792,   138.910,   138.852,   136.570,     6.020,
        8.953,    12.817,    25.034,    49.324,    69.070,    88.061,    94.238,    91.785,    93.294,    88.936,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadSincDown2 = {
      128.762,   132.047,   131.923,   131.860,   131.675,   131.552,   131.207,   130.703,   130.031,   129.082,
      127.980,   126.790,   125.017,   123.574,   121.842,   120.024,   118.255,   116.453,   114.736,   113.041,
      111.549,   110.053,   108.181,   106.198,   104.185,   102.298,   100.195,    98.302,    96.279,    94.212,
       92.284,    90.702,    88.194,    86.473,    86.257,    86.050,    85.831,    85.417,    84.879,     6.021,
        8.954,    12.818,    25.036,    49.329,    69.104,    88.021,    91.848,    91.920, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadSincMicro = {
       88.988,    92.000,    92.003,    92.004,    92.008,    92.012,    92.021,    92.034,    92.055,    92.092,
       92.146,    92.223,    92.385,    92.584,    92.937,    93.532,    94.503,    96.257,    99.576,   106.674,
      101.358,    94.706,    91.390,    92.145,    98.565,    90.301,    92.595,    87.213,    87.999,    85.787,
       80.329,    76.134,    74.969,    75.913,    49.400,    44.095,    50.769,    23.713,     6.174,     3.012,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadSincUp1 = {
       88.989,    92.001,    92.003,    92.005,    92.009,    92.014,    92.024,    92.040,    92.065,    92.108,
       92.173,    92.265,    92.457,    92.694,    93.118,    93.835,    95.023,    97.223,   101.587,   106.990,
       98.288,    93.152,    91.160,    93.974,    96.174,    89.221,    93.525,    87.749,    84.539,    82.209,
       83.988,    78.889,    63.366,    51.199,    29.186,    19.439,    12.226,     0.020, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadSincUp2 = {
       88.931,    88.945,    88.951,    88.957,    88.970,    88.987,    89.023,    89.076,    89.164,    89.314,
       89.539,    89.867,    90.574,    91.490,    93.276,    96.953,   108.101,   100.196,    92.123,    88.866,
       89.904,   107.422,    88.653,    94.128,    86.840,    90.064,    88.079,    80.904,    84.836,    75.734,
       49.454,     0.044, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadSincUp3 = {
       88.989,    92.038,    92.064,    92.089,    92.143,    92.210,    92.353,    92.570,    92.931,    93.569,
       94.575,    96.154,   100.188,   106.452,   101.313,    94.449,    91.441,    92.165,    98.614,    89.985,
       92.530,    87.251,    86.964,    85.827,    80.321,    81.931,    75.049,    49.392, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadPointNxN = {
    -INFINITY,     0.000,     0.000,     0.000,     0.000,     0.000,    56.673,     0.000,     0.000,     0.000,
        0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
       28.464,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
        0.000,     6.315,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
        0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
        0.000,     0.000      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadLinearNxN = {
    -INFINITY,     0.000,     0.000,     0.000,     0.000,     0.000,   120.182,     0.000,     0.000,     0.000,
        0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
       63.907,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
        0.000,    18.337,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
        0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,     0.000,
        0.000,     0.000      };
// clang-format on

constexpr double AudioResult::kPhaseTolerance;

std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhasePointUnity = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhasePointDown0 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhasePointDown1 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhasePointDown2 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhasePointMicro = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhasePointUp1 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhasePointUp2 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhasePointUp3 = {NAN};

std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhaseLinearUnity = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhaseLinearDown0 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhaseLinearDown1 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhaseLinearDown2 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhaseLinearMicro = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhaseLinearUp1 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhaseLinearUp2 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhaseLinearUp3 = {NAN};

std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhaseSincUnity = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhaseSincDown0 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhaseSincDown1 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhaseSincDown2 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhaseSincMicro = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhaseSincUp1 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhaseSincUp2 = {NAN};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhaseSincUp3 = {NAN};

std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhasePointNxN = {-INFINITY};
std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::PhaseLinearNxN = {-INFINITY};

// clang-format off
const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhasePointUnity = {
      0.00000,   0.00000,   0.00000,   0.00000,   0.00000,  -0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,   0.00000,   0.00000,   0.00000,  -0.00000,
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhasePointDown0 = {
      0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,  -0.00000,   0.00000,   0.00000,   0.00000,
     -0.00000,  -0.00000,  -0.00000,   0.00000,   0.00000,  -0.00000,  -0.00000,   0.00000,  -0.00000,   0.00000,
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,   0.00000,  -0.00000,   0.00000,  -0.00000    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhasePointDown1 = {
      0.00000,   0.00000,   0.00000,   0.00000,   0.00000,  -0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,   0.00000,   0.00000,   0.00000,  -0.00000,
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhasePointDown2 = {
      0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
      0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
      0.00000,   0.00000,   0.00000,   0.00000,   0.00001,   0.00001,   0.00001,   0.00001,   0.00001,   0.00002,
      0.00002,   0.00003,   0.00003,   0.00004,   0.00004,   0.00004,   0.00005,   0.00005,   0.00005    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhasePointMicro = {
      0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
      0.00000,  -0.00000,  -0.00000,   0.00000,   0.00000,   0.00000,  -0.00000,   0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,   0.00000,   0.00000,  -0.00000,  -0.00000,   0.00000,  -0.00000,  -0.00000,
      0.00000,  -0.00000,   0.00000,  -0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhasePointUp1 = {
      0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
      0.00000,  -0.00000,  -0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,  -0.00000,
      0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000, -INFINITY    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhasePointUp2 = {
      0.00000,   0.00086,   0.00110,   0.00129,   0.00163,   0.00197,   0.00254,   0.00321,   0.00407,   0.00523,
      0.00657,   0.00810,   0.01059,   0.01299,   0.01635,   0.02066,   0.02593,   0.03274,   0.04118,   0.05230,
      0.06534,   0.08164,   0.10465,   0.13082,   0.16361,   0.20618,   0.26178,   0.32727,   0.41240,   0.52352,
      0.65439,   0.78525, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhasePointUp3 = {
      0.00000,   0.00000,   0.00000,   0.00000,   0.00000,  -0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
      0.00000,  -0.00000,  -0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,  -0.00000,
      0.00000,  -0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseLinearUnity = {
      0.00000,   0.00000,   0.00000,   0.00000,   0.00000,  -0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,   0.00000,   0.00000,   0.00000,  -0.00000,
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseLinearDown0 = {
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00001,
     -0.00002,  -0.00002,  -0.00003,  -0.00003,  -0.00003,  -0.00004,  -0.00004,  -0.00004,  -0.00004    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseLinearDown1 = {
      0.00000,   0.00000,   0.00000,   0.00000,   0.00000,  -0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,   0.00000,   0.00000,   0.00000,  -0.00000,
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseLinearDown2 = {
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00002,  -0.00002,  -0.00003,
     -0.00003,  -0.00004,  -0.00005,  -0.00006,  -0.00007,  -0.00007,  -0.00007,  -0.00007,  -0.00008    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseLinearMicro = {
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00001,
     -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00002,  -0.00002,  -0.00003,  -0.00003,  -0.00004,  -0.00006,
     -0.00007,  -0.00008,  -0.00011,  -0.00014,  -0.00014,  -0.00014,  -0.00014,  -0.00015,  -0.00016    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseLinearUp1 = {
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00001,
     -0.00001,  -0.00001,  -0.00001,  -0.00002,  -0.00002,  -0.00002,  -0.00003,  -0.00004,  -0.00005,  -0.00006,
     -0.00008,  -0.00009,  -0.00012,  -0.00015,  -0.00015,  -0.00016,  -0.00016,  -0.00017, -INFINITY    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseLinearUp2 = {
      0.00000,  -0.00000,   0.00000,   0.00000,  -0.00000,  -0.00000,   0.00000,   0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,
     -0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,  -0.00000,   0.00000,
      0.00000,   0.00000, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseLinearUp3 = {
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,  -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00002,  -0.00002,
     -0.00003,  -0.00003,  -0.00004,  -0.00006,  -0.00007,  -0.00009,  -0.00011,  -0.00014, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseSincUnity = {
      0.00000,   0.00000,   0.00000,   0.00000,   0.00000,  -0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,   0.00000,   0.00000,   0.00000,  -0.00000,
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseSincDown0 = {
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00001,
     -0.00002,  -0.00002,  -0.00003,  -0.00003,  -0.00003,  -0.00004,  -0.00004,  -0.00004,  -0.00004    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseSincDown1 = {
      0.00000,   0.00000,  -0.00000,  -0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
      0.00000,  -0.00000,   0.00000,   0.00000,  -0.00000,   0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,   0.00000    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseSincDown2 = {
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00002,  -0.00002,  -0.00003,
     -0.00003,  -0.00004,  -0.00005,  -0.00006,  -0.00007,  -0.00007,  -0.00007,  -0.00007,  -0.00008    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseSincMicro = {
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00001,
     -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00002,  -0.00002,  -0.00003,  -0.00003,  -0.00004,  -0.00006,
     -0.00007,  -0.00008,  -0.00011,  -0.00014,  -0.00014,  -0.00014,  -0.00015,  -0.00015,  -0.00016    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseSincUp1 = {
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00001,
     -0.00001,  -0.00001,  -0.00001,  -0.00002,  -0.00002,  -0.00002,  -0.00003,  -0.00004,  -0.00005,  -0.00006,
     -0.00008,  -0.00009,  -0.00012,  -0.00015,  -0.00015,  -0.00016,  -0.00016,  -0.00017, -INFINITY    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseSincUp2 = {
      0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
      0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,  -0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
      0.00000,   0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,   0.00000,
     -0.00000,  -0.00000, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseSincUp3 = {
      0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
     -0.00000,  -0.00000,  -0.00000,  -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00002,  -0.00002,
     -0.00003,  -0.00003,  -0.00004,  -0.00006,  -0.00007,  -0.00009,  -0.00011,  -0.00014, -INFINITY, -INFINITY,
    -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhasePointNxN = {
    -INFINITY,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
      0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
     -0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
      0.00000,  -0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseLinearNxN = {
    -INFINITY,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,  -0.00000,   0.00000,   0.00000,   0.00000,
      0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
     -0.00001,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
      0.00000,  -0.00008,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000    };
// clang-format on
//
//
// Scale
//
Gain::AScale AudioResult::ScaleEpsilon = 0;
constexpr float AudioResult::kMaxGainDbNonUnity;

Gain::AScale AudioResult::MinScaleNonZero = 0;
constexpr float AudioResult::kMinGainDbNonMute;

double AudioResult::DynRangeTolerance = 0.0;
constexpr double AudioResult::kPrevDynRangeTolerance;

double AudioResult::LevelEpsilonDown = -INFINITY;
constexpr double AudioResult::kPrevLevelEpsilonDown;

double AudioResult::SinadEpsilonDown = -INFINITY;
constexpr double AudioResult::kPrevSinadEpsilonDown;

double AudioResult::Level30Down = -INFINITY;
double AudioResult::Level60Down = -INFINITY;
double AudioResult::Level90Down = -INFINITY;

double AudioResult::Sinad30Down = -INFINITY;
constexpr double AudioResult::kPrevSinad30Down;

double AudioResult::Sinad60Down = -INFINITY;
constexpr double AudioResult::kPrevSinad60Down;

double AudioResult::Sinad90Down = -INFINITY;
constexpr double AudioResult::kPrevSinad90Down;

//
//
// Sum
//
double AudioResult::LevelToleranceMix8 = 0.0;
double AudioResult::LevelToleranceMix16 = 0.0;
double AudioResult::LevelToleranceMix24 = 0.0;
double AudioResult::LevelToleranceMixFloat = 0.0;

constexpr double AudioResult::kPrevLevelToleranceMix8;
constexpr double AudioResult::kPrevLevelToleranceMix16;
constexpr double AudioResult::kPrevLevelToleranceMix24;
constexpr double AudioResult::kPrevLevelToleranceMixFloat;

double AudioResult::LevelMix8 = -INFINITY;
double AudioResult::LevelMix16 = -INFINITY;
double AudioResult::LevelMix24 = -INFINITY;
double AudioResult::LevelMixFloat = -INFINITY;

constexpr double AudioResult::kPrevLevelMix8;
constexpr double AudioResult::kPrevLevelMix16;
constexpr double AudioResult::kPrevLevelMix24;
constexpr double AudioResult::kPrevLevelMixFloat;

double AudioResult::FloorMix8 = -INFINITY;
double AudioResult::FloorMix16 = -INFINITY;
double AudioResult::FloorMix24 = -INFINITY;
double AudioResult::FloorMixFloat = -INFINITY;

constexpr double AudioResult::kPrevFloorMix8;
constexpr double AudioResult::kPrevFloorMix16;
constexpr double AudioResult::kPrevFloorMix24;
constexpr double AudioResult::kPrevFloorMixFloat;

//
//
// Output
//
double AudioResult::LevelToleranceOutput8 = -INFINITY;
double AudioResult::LevelToleranceOutput16 = -INFINITY;
double AudioResult::LevelToleranceOutput24 = -INFINITY;
double AudioResult::LevelToleranceOutputFloat = -INFINITY;

constexpr double AudioResult::kPrevLevelToleranceOutput8;
constexpr double AudioResult::kPrevLevelToleranceOutput16;
constexpr double AudioResult::kPrevLevelToleranceOutput24;
constexpr double AudioResult::kPrevLevelToleranceOutputFloat;

double AudioResult::LevelOutput8 = -INFINITY;
double AudioResult::LevelOutput16 = -INFINITY;
double AudioResult::LevelOutput24 = -INFINITY;
double AudioResult::LevelOutputFloat = -INFINITY;

constexpr double AudioResult::kPrevLevelOutput8;
constexpr double AudioResult::kPrevLevelOutput16;
constexpr double AudioResult::kPrevLevelOutput24;
constexpr double AudioResult::kPrevLevelOutputFloat;

double AudioResult::FloorOutput8 = -INFINITY;
double AudioResult::FloorOutput16 = -INFINITY;
double AudioResult::FloorOutput24 = -INFINITY;
double AudioResult::FloorOutputFloat = -INFINITY;

constexpr double AudioResult::kPrevFloorOutput8;
constexpr double AudioResult::kPrevFloorOutput16;
constexpr double AudioResult::kPrevFloorOutput24;
constexpr double AudioResult::kPrevFloorOutputFloat;

//
// The subsequent methods are used when updating the kPrev threshold arrays. They display the
// current run's results in an easily-imported format.
//
void AudioResult::DumpThresholdValues() {
  DumpFreqRespValues();
  DumpSinadValues();
  DumpPhaseValues();

  DumpLevelValues();
  DumpLevelToleranceValues();
  DumpNoiseFloorValues();
  DumpDynamicRangeValues();

  printf("\n\n");
}

void AudioResult::DumpFreqRespValues() {
  printf("\n\n Frequency Response");
  printf("\n   (all results given in dB)");

  DumpFreqRespValueSet(AudioResult::FreqRespPointUnity.data(), "FreqRespPointUnity");
  DumpFreqRespValueSet(AudioResult::FreqRespPointDown0.data(), "FreqRespPointDown0");
  DumpFreqRespValueSet(AudioResult::FreqRespPointDown1.data(), "FreqRespPointDown1");
  DumpFreqRespValueSet(AudioResult::FreqRespPointDown2.data(), "FreqRespPointDown2");
  DumpFreqRespValueSet(AudioResult::FreqRespPointMicro.data(), "FreqRespPointMicro");
  DumpFreqRespValueSet(AudioResult::FreqRespPointUp1.data(), "FreqRespPointUp1");
  DumpFreqRespValueSet(AudioResult::FreqRespPointUp2.data(), "FreqRespPointUp2");
  DumpFreqRespValueSet(AudioResult::FreqRespPointUp3.data(), "FreqRespPointUp3");

  DumpFreqRespValueSet(AudioResult::FreqRespLinearUnity.data(), "FreqRespLinearUnity");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearDown0.data(), "FreqRespLinearDown0");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearDown1.data(), "FreqRespLinearDown1");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearDown2.data(), "FreqRespLinearDown2");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearMicro.data(), "FreqRespLinearMicro");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearUp1.data(), "FreqRespLinearUp1");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearUp2.data(), "FreqRespLinearUp2");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearUp3.data(), "FreqRespLinearUp3");

  DumpFreqRespValueSet(AudioResult::FreqRespSincUnity.data(), "FreqRespSincUnity");
  DumpFreqRespValueSet(AudioResult::FreqRespSincDown0.data(), "FreqRespSincDown0");
  DumpFreqRespValueSet(AudioResult::FreqRespSincDown1.data(), "FreqRespSincDown1");
  DumpFreqRespValueSet(AudioResult::FreqRespSincDown2.data(), "FreqRespSincDown2");
  DumpFreqRespValueSet(AudioResult::FreqRespSincMicro.data(), "FreqRespSincMicro");
  DumpFreqRespValueSet(AudioResult::FreqRespSincUp1.data(), "FreqRespSincUp1");
  DumpFreqRespValueSet(AudioResult::FreqRespSincUp2.data(), "FreqRespSincUp2");
  DumpFreqRespValueSet(AudioResult::FreqRespSincUp3.data(), "FreqRespSincUp3");

  DumpFreqRespValueSet(AudioResult::FreqRespPointNxN.data(), "FreqRespPointNxN");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearNxN.data(), "FreqRespLinearNxN");
}

void AudioResult::DumpSinadValues() {
  printf("\n\n Signal-to-Noise+Distortion");
  printf("\n   (all results given in dB)");

  DumpSinadValueSet(AudioResult::SinadPointUnity.data(), "SinadPointUnity");
  DumpSinadValueSet(AudioResult::SinadPointDown0.data(), "SinadPointDown0");
  DumpSinadValueSet(AudioResult::SinadPointDown1.data(), "SinadPointDown1");
  DumpSinadValueSet(AudioResult::SinadPointDown2.data(), "SinadPointDown2");
  DumpSinadValueSet(AudioResult::SinadPointMicro.data(), "SinadPointMicro");
  DumpSinadValueSet(AudioResult::SinadPointUp1.data(), "SinadPointUp1");
  DumpSinadValueSet(AudioResult::SinadPointUp2.data(), "SinadPointUp2");
  DumpSinadValueSet(AudioResult::SinadPointUp3.data(), "SinadPointUp3");

  DumpSinadValueSet(AudioResult::SinadLinearUnity.data(), "SinadLinearUnity");
  DumpSinadValueSet(AudioResult::SinadLinearDown0.data(), "SinadLinearDown0");
  DumpSinadValueSet(AudioResult::SinadLinearDown1.data(), "SinadLinearDown1");
  DumpSinadValueSet(AudioResult::SinadLinearDown2.data(), "SinadLinearDown2");
  DumpSinadValueSet(AudioResult::SinadLinearMicro.data(), "SinadLinearMicro");
  DumpSinadValueSet(AudioResult::SinadLinearUp1.data(), "SinadLinearUp1");
  DumpSinadValueSet(AudioResult::SinadLinearUp2.data(), "SinadLinearUp2");
  DumpSinadValueSet(AudioResult::SinadLinearUp3.data(), "SinadLinearUp3");

  DumpSinadValueSet(AudioResult::SinadSincUnity.data(), "SinadSincUnity");
  DumpSinadValueSet(AudioResult::SinadSincDown0.data(), "SinadSincDown0");
  DumpSinadValueSet(AudioResult::SinadSincDown1.data(), "SinadSincDown1");
  DumpSinadValueSet(AudioResult::SinadSincDown2.data(), "SinadSincDown2");
  DumpSinadValueSet(AudioResult::SinadSincMicro.data(), "SinadSincMicro");
  DumpSinadValueSet(AudioResult::SinadSincUp1.data(), "SinadSincUp1");
  DumpSinadValueSet(AudioResult::SinadSincUp2.data(), "SinadSincUp2");
  DumpSinadValueSet(AudioResult::SinadSincUp3.data(), "SinadSincUp3");

  DumpSinadValueSet(AudioResult::SinadPointNxN.data(), "SinadPointNxN");
  DumpSinadValueSet(AudioResult::SinadLinearNxN.data(), "SinadLinearNxN");
}

void AudioResult::DumpPhaseValues() {
  printf("\n\n Phase Response");
  printf("\n   (all results given in radians)");

  DumpPhaseValueSet(AudioResult::PhasePointUnity.data(), "PhasePointUnity");
  DumpPhaseValueSet(AudioResult::PhasePointDown0.data(), "PhasePointDown0");
  DumpPhaseValueSet(AudioResult::PhasePointDown1.data(), "PhasePointDown1");
  DumpPhaseValueSet(AudioResult::PhasePointDown2.data(), "PhasePointDown2");
  DumpPhaseValueSet(AudioResult::PhasePointMicro.data(), "PhasePointMicro");
  DumpPhaseValueSet(AudioResult::PhasePointUp1.data(), "PhasePointUp1");
  DumpPhaseValueSet(AudioResult::PhasePointUp2.data(), "PhasePointUp2");
  DumpPhaseValueSet(AudioResult::PhasePointUp3.data(), "PhasePointUp3");

  DumpPhaseValueSet(AudioResult::PhaseLinearUnity.data(), "PhaseLinearUnity");
  DumpPhaseValueSet(AudioResult::PhaseLinearDown0.data(), "PhaseLinearDown0");
  DumpPhaseValueSet(AudioResult::PhaseLinearDown1.data(), "PhaseLinearDown1");
  DumpPhaseValueSet(AudioResult::PhaseLinearDown2.data(), "PhaseLinearDown2");
  DumpPhaseValueSet(AudioResult::PhaseLinearMicro.data(), "PhaseLinearMicro");
  DumpPhaseValueSet(AudioResult::PhaseLinearUp1.data(), "PhaseLinearUp1");
  DumpPhaseValueSet(AudioResult::PhaseLinearUp2.data(), "PhaseLinearUp2");
  DumpPhaseValueSet(AudioResult::PhaseLinearUp3.data(), "PhaseLinearUp3");

  DumpPhaseValueSet(AudioResult::PhaseSincUnity.data(), "PhaseSincUnity");
  DumpPhaseValueSet(AudioResult::PhaseSincDown0.data(), "PhaseSincDown0");
  DumpPhaseValueSet(AudioResult::PhaseSincDown1.data(), "PhaseSincDown1");
  DumpPhaseValueSet(AudioResult::PhaseSincDown2.data(), "PhaseSincDown2");
  DumpPhaseValueSet(AudioResult::PhaseSincMicro.data(), "PhaseSincMicro");
  DumpPhaseValueSet(AudioResult::PhaseSincUp1.data(), "PhaseSincUp1");
  DumpPhaseValueSet(AudioResult::PhaseSincUp2.data(), "PhaseSincUp2");
  DumpPhaseValueSet(AudioResult::PhaseSincUp3.data(), "PhaseSincUp3");

  DumpPhaseValueSet(AudioResult::PhasePointNxN.data(), "PhasePointNxN");
  DumpPhaseValueSet(AudioResult::PhaseLinearNxN.data(), "PhaseLinearNxN");
}

// Display a single frequency response results array, for import and processing.
void AudioResult::DumpFreqRespValueSet(double* freq_resp_vals, const std::string& arr_name) {
  printf("\nconst std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>");
  printf("\n    AudioResult::kPrev%s = {", arr_name.c_str());
  for (auto freq_idx = 0u; freq_idx < FrequencySet::kFirstOutBandRefFreqIdx; ++freq_idx) {
    if (freq_idx % 10 == 0) {
      printf("\n  ");
    }
    if (freq_idx >= FrequencySet::kFirstInBandRefFreqIdx) {
      // For positive values, display 0 as threshold (we also bump up LevelTolerance, which limits
      // future values from being FURTHER above 0 dB)
      if (!isinf(freq_resp_vals[freq_idx])) {
        printf(" %9.3lf", std::min(0.0, std::floor(freq_resp_vals[freq_idx] / kFreqRespTolerance) *
                                            kFreqRespTolerance));
      } else {
        printf(" %sINFINITY", (freq_resp_vals[freq_idx] < 0) ? "-" : " ");
      }
      if (freq_idx < FrequencySet::kFirstOutBandRefFreqIdx - 1) {
        printf(",");
      }
    } else {
      printf("           ");
    }
  }
  printf("      };\n");
}

// Display a single sinad results array, for import and processing.
void AudioResult::DumpSinadValueSet(double* sinad_vals, const std::string& arr_name) {
  printf("\nconst std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrev%s = {",
         arr_name.c_str());
  for (auto freq_idx = 0u; freq_idx < FrequencySet::kNumReferenceFreqs; ++freq_idx) {
    if (freq_idx % 10 == 0) {
      printf("\n   ");
    }
    if (!isinf(sinad_vals[freq_idx])) {
      printf(" %9.3lf", std::floor(sinad_vals[freq_idx] / kSinadTolerance) * kSinadTolerance);
    } else if (sinad_vals[freq_idx] > 0.0) {
      printf(" %9.3lf", 160.0);
    } else {
      printf(" -INFINITY");
    }

    if (freq_idx < FrequencySet::kNumReferenceFreqs - 1) {
      printf(",");
    }
  }
  printf("      };\n");
}

// Display a single phase results array, for import and processing.
void AudioResult::DumpPhaseValueSet(double* phase_vals, const std::string& arr_name) {
  printf(
      "\nconst std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrev%s = {",
      arr_name.c_str());
  for (auto freq_idx = 0u; freq_idx < FrequencySet::kFirstOutBandRefFreqIdx; ++freq_idx) {
    if (freq_idx % 10 == 0) {
      printf("\n   ");
    }
    if (freq_idx >= FrequencySet::kFirstInBandRefFreqIdx) {
      if (!isinf(phase_vals[freq_idx])) {
        printf(" %9.5lf", phase_vals[freq_idx]);
      } else {
        printf(" %sINFINITY", (phase_vals[freq_idx] < 0) ? "-" : " ");
      }
      if (freq_idx < FrequencySet::kFirstOutBandRefFreqIdx - 1) {
        printf(",");
      }
    } else {
      printf("           ");
    }
  }
  printf("    };\n");
}

void AudioResult::DumpLevelValues() {
  printf("\n\n Level (in dB)");
  printf("\n       8-bit:   Source %13.6le  Mix %13.6le  Output %13.6le", AudioResult::LevelSource8,
         AudioResult::LevelMix8, AudioResult::LevelOutput8);

  printf("\n       16-bit:  Source %13.6le  Mix %13.6le  Output %13.6le",
         AudioResult::LevelSource16, AudioResult::LevelMix16, AudioResult::LevelOutput16);

  printf("\n       24-bit:  Source %13.6le  Mix %13.6le  Output %13.6le",
         AudioResult::LevelSource24, AudioResult::LevelMix24, AudioResult::LevelOutput24);

  printf("\n       Float:   Source %13.6le  Mix %13.6le  Output %13.6le",
         AudioResult::LevelSourceFloat, AudioResult::LevelMixFloat, AudioResult::LevelOutputFloat);
  printf("\n       Stereo-to-Mono: %13.6le", AudioResult::LevelStereoMono);

  printf("\n");
}

void AudioResult::DumpLevelToleranceValues() {
  printf("\n\n Level Tolerance (in dB)");
  printf("\n       8-bit:   Source %13.6le  Mix %13.6le  Output %13.6le",
         AudioResult::LevelToleranceSource8, AudioResult::LevelToleranceMix8,
         AudioResult::LevelToleranceOutput8);

  printf("\n       16-bit:  Source %13.6le  Mix %13.6le  Output %13.6le",
         AudioResult::LevelToleranceSource16, AudioResult::LevelToleranceMix16,
         AudioResult::LevelToleranceOutput16);

  printf("\n       24-bit:  Source %13.6le  Mix %13.6le  Output %13.6le",
         AudioResult::LevelToleranceSource24, AudioResult::LevelToleranceMix24,
         AudioResult::LevelToleranceOutput24);

  printf("\n       Float:   Source %13.6le  Mix %13.6le  Output %13.6le",
         AudioResult::LevelToleranceSourceFloat, AudioResult::LevelToleranceMixFloat,
         AudioResult::LevelToleranceOutputFloat);

  printf("\n       Stereo-to-Mono: %13.6le             ", AudioResult::LevelToleranceStereoMono);
  printf("Interpolation: %13.6le", LevelToleranceInterpolation);

  printf("\n");
}

void AudioResult::DumpNoiseFloorValues() {
  printf("\n\n Noise Floor (in dB)");
  printf("\n       8-bit:   Source %9.5lf  Mix %9.5lf  Output %9.5lf", AudioResult::FloorSource8,
         AudioResult::FloorMix8, AudioResult::FloorOutput8);
  printf("\n       16-bit:  Source %9.5lf  Mix %9.5lf  Output %9.5lf", AudioResult::FloorSource16,
         AudioResult::FloorMix16, AudioResult::FloorOutput16);
  printf("\n       24-bit:  Source %9.5lf  Mix %9.5lf  Output %9.5lf", AudioResult::FloorSource24,
         AudioResult::FloorMix24, AudioResult::FloorOutput24);
  printf("\n       Float:   Source %9.5lf  Mix %9.5lf  Output %9.5lf",
         AudioResult::FloorSourceFloat, AudioResult::FloorMixFloat, AudioResult::FloorOutputFloat);
  printf("\n       Stereo-to-Mono: %9.5lf", AudioResult::FloorStereoMono);

  printf("\n");
}

void AudioResult::DumpDynamicRangeValues() {
  printf("\n\n Dynamic Range");
  printf("\n       Epsilon:  %7.4e  (%10.4le dB)", AudioResult::ScaleEpsilon,
         Gain::ScaleToDb(1.0 - AudioResult::ScaleEpsilon));
  printf("  Level: %11.4le dB  Sinad: %8.4lf dB", AudioResult::LevelEpsilonDown,
         AudioResult::SinadEpsilonDown);

  printf("\n       -30 dB down:                          ");
  printf("  Level: %11.4lf dB  Sinad: %8.4lf dB", AudioResult::Level30Down,
         AudioResult::Sinad30Down);

  printf("\n       -60 dB down:                          ");
  printf("  Level: %11.4lf dB  Sinad: %8.4lf dB", AudioResult::Level60Down,
         AudioResult::Sinad60Down);

  printf("\n       -90 dB down:                          ");
  printf("  Level: %11.4lf dB  Sinad: %8.4lf dB", AudioResult::Level90Down,
         AudioResult::Sinad90Down);

  printf("\n       Gain Accuracy: +/- %9.4le dB", AudioResult::DynRangeTolerance);

  printf("\n       MinScale: %8.6f  (%8.5f dB)", AudioResult::MinScaleNonZero,
         Gain::ScaleToDb(AudioResult::MinScaleNonZero));

  printf("\n");
}

}  // namespace media::audio::test
