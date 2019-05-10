// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/test/audio_result.h"

#include <cstdio>
#include <string>

#include "src/media/audio/audio_core/mixer/test/mixer_tests_shared.h"

namespace media::audio::test {

// See audio_result.h for in-depth descriptions of these class members/consts.
//
// In summary, however:
// * For all TOLERANCE measurements, smaller is better (tighter tolerance).
//   Measured results must be WITHIN the tolerance.
// * For ALL other measurements (frequency response, SINAD, level, noise floor),
//   larger results are better (e.g. frequency response closer to 0, higher
//   noise floor or SINAD).

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

std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespPointUnity = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespPointDown0 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespPointDown1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespPointDown2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespPointUp1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespPointUp2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespPointUp3 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespPointMicro = {NAN};

std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearUnity = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearDown0 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearDown1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearDown2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearUp1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearUp2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearUp3 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearMicro = {NAN};

std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespPointNxN = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearNxN = {NAN};

// We test our interpolation fidelity across these six rate-conversion ratios:
// - 1:1 (referred to in these variables and constants as Unity)
// - 2:1, which equates to 96k -> 48k (referred to as Down1)
// - 294:160, which equates to 88.2k -> 48k (Down2)
// - 147:160, which equates to 44.1k -> 48k (Up1)
// - 1:2, which equates to 24k -> 48k, or 48k -> 96k (Up2)
// - 47999:48000, representing small adjustment for multi-device sync (Micro)
//
// For Frequency Response, values closer to 0 (flatter response) are desired.
// Below you see that for 1:1 and 2:1, our response is near-ideal. For all other
// rates, our response drops off at higher frequencies.
//
// clang-format off
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointUnity = {
         0.0000000e+00, -1.9772600e-09, -5.3325766e-10, -5.3325381e-10, -1.9772590e-09, -5.3325670e-10,
        -5.3325188e-10, -5.3325574e-10, -5.3324995e-10, -5.3324802e-10, -5.3326249e-10, -5.3325477e-10,
        -5.3324513e-10, -5.3045726e-10, -5.3043797e-10, -5.3318245e-10, -5.3304358e-10, -5.3029525e-10,
        -5.3021232e-10, -5.2741866e-10, -5.3282082e-10, -5.2770507e-10, -5.2953150e-10, -5.2982369e-10,
        -5.2636369e-10, -5.3142834e-10, -5.2545818e-10, -5.2888540e-10, -5.2436078e-10, -5.2107724e-10,
        -5.0774735e-10, -5.2798954e-10, -4.9616384e-10, -5.1692003e-10, -5.2461536e-10, -5.1789786e-10,
        -5.2736370e-10, -5.2348999e-10, -4.9876946e-10,  0.0000000e+00, -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointDown0 = {
         0.0000000e+00,  0.0000000e+00, -1.3126491e-07,  0.0000000e+00, -2.0876720e-07, -3.8765154e-07,
        -6.3277078e-07, -8.7239859e-07, -1.4254718e-06, -2.3715742e-06, -4.0288977e-06, -5.7841053e-06,
        -1.0358865e-05, -1.5023484e-05, -2.4492948e-05, -3.9024768e-05, -6.0366597e-05, -9.7631582e-05,
        -1.5265413e-04, -2.4852011e-04, -3.8507092e-04, -6.0466871e-04, -9.9300886e-04, -1.5512596e-03,
        -2.4256060e-03, -3.8511220e-03, -6.1972736e-03, -9.7008467e-03, -1.5404765e-02, -2.4804792e-02,
        -3.8771409e-02, -5.5854122e-02, -9.9409879e-02, -1.4792272e-01, -1.5566146e-01, -1.6369384e-01,
        -1.7185471e-01, -1.8944819e-01, -2.1463965e-01, -9.1209736e-01, -2.4295932e-01, -3.8786815e-01,
        -6.2921487e-01, -7.6371200e-01, -7.6715614e-01, -8.9269835e-01, -9.1198639e-01   };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointDown1 = {
         0.0000000e+00, -1.9772600e-09, -5.3325766e-10, -5.3325381e-10, -1.9772590e-09, -5.3325670e-10,
        -5.3325188e-10, -5.3325574e-10, -5.3324995e-10, -5.3324802e-10, -5.3326249e-10, -5.3325477e-10,
        -5.3324513e-10, -5.3045726e-10, -5.3043797e-10, -5.3318245e-10, -5.3304358e-10, -5.3029525e-10,
        -5.3021232e-10, -5.2741866e-10, -5.3282082e-10, -5.2770507e-10, -5.2953150e-10, -5.2982369e-10,
        -5.2636369e-10, -5.3142834e-10, -5.2545818e-10, -5.2888540e-10, -5.2436078e-10, -5.2107724e-10,
        -5.0774735e-10, -5.2798954e-10, -4.9616384e-10, -5.1692003e-10, -5.2461536e-10, -5.1789786e-10,
        -5.2736370e-10, -5.2348999e-10, -4.9876946e-10,  0.0000000e+00, -5.2786707e-10, -5.0713018e-10,
        -5.0078008e-10, -4.8733832e-10, -5.3374176e-10, -4.9920340e-10, -4.8059960e-10   };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointDown2 = {
         0.0000000e+00, -6.2545361e-07, -5.5337752e-07, -5.9509624e-07, -1.7323314e-06, -1.6037873e-06,
        -3.0242621e-06, -5.1639610e-06, -7.5432833e-06, -1.0690382e-05, -1.8359708e-05, -2.7993350e-05,
        -4.6269320e-05, -6.6792744e-05, -1.1673195e-04, -1.7596492e-04, -2.8496660e-04, -4.5818400e-04,
        -7.2568652e-04, -1.2862017e-03, -1.6838537e-03, -2.8753928e-03, -4.7381167e-03, -7.3740357e-03,
        -1.1445448e-02, -1.8281240e-02, -2.9394615e-02, -4.5931335e-02, -7.3039982e-02, -1.1760136e-01,
        -1.8453367e-01, -2.6597450e-01, -4.7499921e-01, -7.0966159e-01, -7.4733762e-01, -7.8600795e-01,
        -8.2548394e-01, -9.1173457e-01, -1.0299529e+00, -4.7423029e+00, -1.1794782e+00, -1.9052524e+00,
        -3.1731802e+00, -3.9023798e+00, -3.9231171e+00, -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointUp1 = {
         0.0000000e+00, -1.5830899e-06, -2.1161810e-06, -2.7498435e-06, -5.1479358e-06, -6.5713715e-06,
        -1.1325548e-05, -1.8430984e-05, -2.8895663e-05, -4.5804027e-05, -7.3830086e-05, -1.1240271e-04,
        -1.9061557e-04, -2.8384470e-04, -4.6039436e-04, -7.2498443e-04, -1.1499948e-03, -1.8369315e-03,
        -2.9066219e-03, -4.8039066e-03, -7.1766122e-03, -1.1449341e-02, -1.8828932e-02, -2.9401894e-02,
        -4.5919063e-02, -7.3063787e-02, -1.1783325e-01, -1.8441857e-01, -2.9363624e-01, -4.7507418e-01,
        -7.4719465e-01, -1.0844209e+00, -1.9687874e+00, -2.9991557e+00, -3.1678429e+00, -3.3408237e+00,
        -3.5233291e+00, -3.9220669e+00, -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointUp2 = {
         0.0000000e+00, -3.2376516e-06, -5.2813212e-06, -7.2772930e-06, -1.1540895e-05, -1.6778123e-05,
        -2.8035418e-05, -4.4801620e-05, -7.2106624e-05, -1.1857317e-04, -1.8731521e-04, -2.8503972e-04,
        -4.8743716e-04, -7.3295307e-04, -1.1605211e-03, -1.8540016e-03, -2.9212432e-03, -4.6563262e-03,
        -7.3660288e-03, -1.1884252e-02, -1.8553473e-02, -2.8975872e-02, -4.7645944e-02, -7.4537346e-02,
        -1.1677339e-01, -1.8593632e-01, -3.0108726e-01, -4.7368842e-01, -7.6056343e-01, -1.2489868e+00,
        -2.0099216e+00, -3.0090511e+00, -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointUp3 = {
         0.0000000e+00, -2.8230396e-04, -2.8148971e-05, -3.8790435e-05, -3.2655760e-04, -8.9443851e-05,
        -1.4946193e-04, -2.3885084e-04, -3.8442592e-04, -6.3216648e-04, -9.9867160e-04, -1.5197071e-03,
        -2.5988524e-03, -3.9079472e-03, -6.1878777e-03, -9.8860927e-03, -1.5578368e-02, -2.4834878e-02,
        -3.9296385e-02, -6.3424765e-02, -9.9074156e-02, -1.5486770e-01, -2.5506509e-01, -3.9996034e-01,
        -6.2893611e-01, -1.0077116e+00, -1.6494604e+00, -2.6398568e+00, -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointMicro = {
         0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,
         0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00, -2.8743631e-05,
        -9.6197753e-05, -1.7804341e-04, -3.2055780e-04, -5.5169658e-04, -9.0739160e-04, -1.4856181e-03,
        -2.3885189e-03, -3.8937031e-03, -6.1146992e-03, -9.5837630e-03, -1.5792483e-02, -2.4722667e-02,
        -3.8718694e-02, -6.1558903e-02, -9.9369401e-02, -1.5553763e-01, -2.4755260e-01, -4.0038119e-01,
        -6.2894158e-01, -9.1168067e-01, -1.6491708e+00, -2.5007446e+00, -2.6397050e+00, -2.7827859e+00,
        -2.9309201e+00, -3.2560185e+00, -3.7067765e+00, -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearUnity = {
         0.0000000e+00, -1.9772600e-09, -5.3325766e-10, -5.3325381e-10, -1.9772590e-09, -5.3325670e-10,
        -5.3325188e-10, -5.3325574e-10, -5.3324995e-10, -5.3324802e-10, -5.3326249e-10, -5.3325477e-10,
        -5.3324513e-10, -5.3045726e-10, -5.3043797e-10, -5.3318245e-10, -5.3304358e-10, -5.3029525e-10,
        -5.3021232e-10, -5.2741866e-10, -5.3282082e-10, -5.2770507e-10, -5.2953150e-10, -5.2982369e-10,
        -5.2636369e-10, -5.3142834e-10, -5.2545818e-10, -5.2888540e-10, -5.2436078e-10, -5.2107724e-10,
        -5.0774735e-10, -5.2798954e-10, -4.9616384e-10, -5.1692003e-10, -5.2461536e-10, -5.1789786e-10,
        -5.2736370e-10, -5.2348999e-10, -4.9876946e-10,  0.0000000e+00, -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearDown0 = {
         0.0000000e+00, -1.3560410e-07, -2.2096572e-07, -3.0375984e-07, -4.8106798e-07, -7.0035404e-07,
        -1.1697847e-06, -1.8670036e-06, -3.0073029e-06, -4.9423862e-06, -7.8059448e-06, -1.1877488e-05,
        -2.0311420e-05, -3.0541582e-05, -4.8355041e-05, -7.7248342e-05, -1.2170773e-04, -1.9398439e-04,
        -3.0684081e-04, -4.9496483e-04, -7.7253829e-04, -1.2060302e-03, -1.9817091e-03, -3.0970299e-03,
        -4.8441738e-03, -7.6930799e-03, -1.2403072e-02, -1.9385618e-02, -3.0787663e-02, -4.9624471e-02,
        -7.7560351e-02, -1.1172816e-01, -1.9882133e-01, -2.9574516e-01, -3.1119248e-01, -3.2699047e-01,
        -3.4323223e-01, -3.7847585e-01, -4.2655399e-01, -4.4881934e-01, -4.8720667e-01, -7.7606042e-01,
        -1.2584740e+00, -1.5274720e+00, -1.5345223e+00, -1.7856048e+00, -1.8241810e+00   };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearDown1 = {
         0.0000000e+00, -1.9772600e-09, -5.3325766e-10, -5.3325381e-10, -1.9772590e-09, -5.3325670e-10,
        -5.3325188e-10, -5.3325574e-10, -5.3324995e-10, -5.3324802e-10, -5.3326249e-10, -5.3325477e-10,
        -5.3324513e-10, -5.3045726e-10, -5.3043797e-10, -5.3318245e-10, -5.3304358e-10, -5.3029525e-10,
        -5.3021232e-10, -5.2741866e-10, -5.3282082e-10, -5.2770507e-10, -5.2953150e-10, -5.2982369e-10,
        -5.2636369e-10, -5.3142834e-10, -5.2545818e-10, -5.2888540e-10, -5.2436078e-10, -5.2107724e-10,
        -5.0774735e-10, -5.2798954e-10, -4.9616384e-10, -5.1692003e-10, -5.2461536e-10, -5.1789786e-10,
        -5.2736370e-10, -5.2348999e-10, -4.9876946e-10,  0.0000000e+00, -5.2786707e-10, -5.0713018e-10,
        -5.0078008e-10, -4.8733832e-10, -5.3374176e-10, -4.9920340e-10, -4.8059960e-10   };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearDown2 = {
         0.0000000e+00, -6.3822570e-07, -1.0395043e-06, -1.4355155e-06, -2.2783814e-06, -3.3105243e-06,
        -5.5342438e-06, -8.8452921e-06, -1.4234833e-05, -2.3412091e-05, -3.6983731e-05, -5.6277393e-05,
        -9.6239638e-05, -1.4471410e-04, -2.2913233e-04, -3.6604707e-04, -5.7673735e-04, -9.1923731e-04,
        -1.4540329e-03, -2.3455347e-03, -3.6609235e-03, -5.7152998e-03, -9.3915140e-03, -1.4677821e-02,
        -2.2959819e-02, -3.6467196e-02, -5.8805719e-02, -9.1939220e-02, -1.4608711e-01, -2.3565981e-01,
        -3.6877127e-01, -5.3202083e-01, -9.5038397e-01, -1.4198462e+00, -1.4950525e+00, -1.5720769e+00,
        -1.6513831e+00, -1.8238856e+00, -2.0601325e+00, -2.1699023e+00, -2.3597079e+00, -3.8111231e+00,
        -6.3355155e+00, -7.8050468e+00, -7.8442180e+00, -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearUp1 = {
         0.0000000e+00, -2.5516310e-06, -4.1678491e-06, -5.7439283e-06, -9.1112253e-06, -1.3248613e-05,
        -2.2139066e-05, -3.5381715e-05, -5.6948304e-05, -9.3646159e-05, -1.4793727e-04, -2.2511883e-04,
        -3.8496865e-04, -5.7887223e-04, -9.1654360e-04, -1.4642074e-03, -2.3069974e-03, -3.6770605e-03,
        -5.8164285e-03, -9.3829067e-03, -1.4645559e-02, -2.2865714e-02, -3.7578247e-02, -5.8741085e-02,
        -9.1912259e-02, -1.4605310e-01, -2.3570309e-01, -3.6893407e-01, -5.8733496e-01, -9.5047204e-01,
        -1.4944954e+00, -2.1690869e+00, -3.9376310e+00, -5.9976308e+00, -6.3357708e+00, -6.6845975e+00,
        -7.0464553e+00, -7.8433525e+00, -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearUp2 = {
         0.0000000e+00, -6.4749325e-06, -1.0563305e-05, -1.4552500e-05, -2.3077789e-05, -3.3551619e-05,
        -5.6066745e-05, -8.9601458e-05, -1.4421078e-04, -2.3714242e-04, -3.7462855e-04, -5.7008073e-04,
        -9.7487178e-04, -1.4659017e-03, -2.3210368e-03, -3.7080022e-03, -5.8424840e-03, -9.3126509e-03,
        -1.4732055e-02, -2.3768503e-02, -3.7106945e-02, -5.7951740e-02, -9.5291884e-02, -1.4907470e-01,
        -2.3354677e-01, -3.7187263e-01, -6.0217451e-01, -9.4737683e-01, -1.5211269e+00, -2.4979735e+00,
        -4.0198431e+00, -6.0181021e+00, -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearUp3 = {
         0.0000000e+00, -3.4481570e-05, -5.6307859e-05, -7.7595284e-05, -1.2303802e-04, -1.7892723e-04,
        -2.9899015e-04, -4.7780857e-04, -7.6902606e-04, -1.2646195e-03, -1.9977967e-03, -3.0401078e-03,
        -5.1988894e-03, -7.8176793e-03, -1.2378574e-02, -1.9776681e-02, -3.1163795e-02, -4.9680939e-02,
        -7.8610299e-02, -1.2687738e-01, -1.9819079e-01, -3.0979930e-01, -5.1022835e-01, -8.0005931e-01,
        -1.2580550e+00, -2.0156336e+00, -3.2990811e+00, -5.2796564e+00, -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearMicro = {
         0.0000000e+00, -2.1585212e-06, -3.5194082e-06, -4.8526388e-06, -7.6796668e-06, -1.1183720e-05,
        -1.8689826e-05, -2.9866062e-05, -4.8053723e-05, -7.9048246e-05, -1.2487796e-04, -1.9002901e-04,
        -3.2495089e-04, -4.8863930e-04, -7.7367595e-04, -1.2359665e-03, -1.9473794e-03, -3.1038664e-03,
        -4.9097245e-03, -7.9201843e-03, -1.2362313e-02, -1.9300655e-02, -3.1718474e-02, -4.9579395e-02,
        -7.7572320e-02, -1.2325418e-01, -1.9887759e-01, -3.1121774e-01, -4.9525403e-01, -8.0092262e-01,
        -1.2580628e+00, -1.8235695e+00, -3.2986619e+00, -5.0020980e+00, -5.2801039e+00, -5.5663757e+00,
        -5.8628714e+00, -6.5135504e+00, -7.4187285e+00, -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY        };
// clang-format on

std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadPointUnity = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadPointDown0 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadPointDown1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadPointDown2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadPointUp1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadPointUp2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadPointUp3 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadPointMicro = {NAN};

std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearUnity = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearDown0 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearDown1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearDown2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearUp1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearUp2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearUp3 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearMicro = {NAN};

std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadPointNxN = {-INFINITY};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearNxN = {-INFINITY};

// We test our interpolation fidelity across these six rate-conversion ratios:
// - 1:1 (referred to in these variables and constants as Unity)
// - 2:1, which equates to 96k -> 48k (referred to as Down1)
// - 294:160, which equates to 88.2k -> 48k (Down2)
// - 147:160, which equates to 44.1k -> 48k (Up1)
// - 1:2, which equates to 24k -> 48k, or 48k -> 96k (Up2)
// - 47999:48000, representing small adjustment for multi-device sync (Micro)
//
// For SINAD, higher values (lower noise/artifacts vs. signal) are desired.
// Below you see that for 1:1 and 2:1, our SINAD is near-ideal. For all other
// rates, our performance drops off (lower values) at higher frequencies.
//
// clang-format off
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointUnity = {
         160.00000,  153.71437,  153.74509,  153.74509,  153.71437,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509, -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointDown0 = {
        160.00000,   78.122180,  75.964356,  74.577171,  72.569334,  70.943237,
         68.713396,  66.677416,  64.610535,  62.450383,  60.464500,  58.641176,
         56.311061,  54.539519,  52.543811,  50.509344,  48.534933,  46.510440,
         44.518960,  42.442268,  40.508767,  38.574256,  36.417240,  34.477908,
         32.534728,  30.525208,  28.449757,  26.508483,  24.496655,  22.418787,
         20.472358,  18.878574,  16.353737,  14.604752,  14.379723,  14.160394,
         13.945699,  13.512413,  12.978650,  17.189886,  -0.2435322, -0.3880030,
         -0.6292312, -0.7637285, -0.7672283, -0.8927653, -0.9120527   };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointDown1 = {
         160.00000,  153.71437,  153.74509,  153.74509,  153.71437,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  160.00000,   -0.00001,   -0.00001,
           0.0,        0.0,        0.0,        0.0,        0.0         };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointDown2 = {
        160.00000,   71.336877,  69.207771,  67.815057,  65.812750,  64.186654,
         61.956811,  59.920832,  57.853947,  55.693796,  53.707909,  51.884581,
         49.554461,  47.782913,  45.787171,  43.752690,  41.778220,  39.753644,
         37.762035,  35.684914,  33.751618,  31.816288,  29.658355,  27.717787,
         25.772785,  23.759904,  21.679253,  19.730206,  17.705458,  15.606694,
         13.627759,  11.995243,   9.3697669,  7.5064406,  7.2628203,  7.0245187,
          6.7906798,  6.3146193,  5.7241998,  1.3009572, -1.1796846, -1.9053601,
         -3.1726329, -3.9023801, -3.9231171, -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointUp1 = {
        160.00000,   65.316279,  63.187172,  61.794455,    59.792148,   58.166050,
         55.936206,  53.900223,  51.833335,  49.673175,    47.687276,   45.863930,
         43.533774,  41.762183,  39.766365,  37.731761,    35.757101,   33.732217,
         31.740126,  29.662201,  27.727717,  25.790544,    23.629300,   21.683967,
         19.731484,  17.706396,  15.605467,  13.626191,    11.551686,    9.3692819,
          7.2643203,  5.4720562,  2.4143111,  0.022407171, -0.30957862, -0.6378681,
         -0.9717976, -1.6649323, -INFINITY,  -INFINITY,    -INFINITY,   -INFINITY,
         -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,    -INFINITY     };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointUp2 = {
        160.00000,   61.281148,   59.152040, 57.759321,  55.757015,  54.130912,
         51.901065,  49.865075,   47.798174, 45.637992,  43.652064,  41.828677,
         39.498430,  37.726726,   35.730739, 33.695819,  31.720708,  29.695082,
         27.701826,  25.622181,   23.684311, 21.742982,  19.573739,  17.616782,
         15.645886,  13.590901,   11.439506,  9.3839187,  7.1806586,  4.7728152,
          2.3024022,  0.0024982, -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,   -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,   -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointUp3 = {
        160.00000,   54.018289,  51.882570,  50.489845,   48.488997,  46.861413,
         44.631536,  42.595504,  40.528534,  38.368234,   36.382133,  34.558498,
         32.227739,  30.455414,  28.458345,  26.421669,   24.443854,  22.413830,
         20.413698,  18.322571,  16.367708,  14.399722,   12.182412,  10.155507,
          8.0730151,  5.8305794,  3.3535175,  0.7754679, -INFINITY,  -INFINITY,
        -INFINITY,   -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,   -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,   -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointMicro = {
        160.00000,    66.059499,  63.927625,  62.533706,  60.530232,   58.903508,
         56.673112,   54.636824,  52.569740,  50.409454,  48.423487,   46.600103,
         44.269918,   42.498312,  40.502514,  38.467909,  36.493292,   34.468473,
         32.476480,   30.398944,  28.464189,  26.527730,  24.367222,   22.422864,
         20.471802,   18.449412,  16.352608,  14.379604,  12.315608,   10.150625,
          8.0729832,   6.3152540,  3.3543294,  1.0867921,  0.77564379,  0.4674690,
          0.16010267, -0.4784014, -1.2966582, -INFINITY,  -INFINITY,   -INFINITY,
        -INFINITY,    -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY     };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearUnity = {
         160.00000,  153.71437,  153.74509,  153.74509,  153.71437,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  160.00000, -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearDown0 = {
        160.00000,    150.11065,     149.19638,   148.35386,   146.70743,   144.98731,
        141.99425,    138.82310,     135.28481,   131.32290,   127.56169,   124.04061,
        119.47631,    115.97708,     112.01967,   107.97252,   104.03676,    99.996038,
         96.018321,    91.868146,     88.003019,   84.134935,   79.821129,   75.942074,
         72.054585,    68.033407,     63.878712,   59.990525,   55.957535,   51.786276,
         47.870582,    44.655243,     39.535613,   35.961366,   35.499314,   35.048955,
         34.607125,    33.713178,     32.613373,   29.374669,   -0.0031581,  -0.008696118,
        -0.026062825,  -0.041175638,  -0.0416317,  -0.0600556,  -0.0632814    };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearDown1 = {
        160.00000,  153.71437,  153.74509,  153.74509,  153.71437,  153.74509,
        153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
        153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
        153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
        153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
        153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
        153.74509,  153.74509,  153.74509,  160.00000,   -0.00001,   -0.00001,
          0.0,        0.0,        0.0,        0.0,        0.0         };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearDown2 = {
        160.00000,   145.49338,   142.76626,   140.72251,  137.37211,   134.53647,
        130.42254,   126.53508,   122.51076,   118.26515,  114.33387,   110.71175,
        106.07058,   102.53716,    98.552405,   94.487585,  90.541183,   86.493364,
         82.510726,   78.356875,   74.488568,   70.617160,  66.298521,   62.413089,
         58.516039,   54.479521,   50.299693,   46.374454,  42.281300,   38.011435,
         33.951159,   30.561504,   25.008837,   20.969144,  20.434310,   19.909878,
         19.392276,   18.335140,   17.015322,   14.389380,  -0.1204279,  -0.4278153,
         -1.7444607,  -3.0339471,  -3.0730057, -INFINITY,   -INFINITY     };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearUp1 = {
        160.00000,   136.51568,   132.67958,   130.09359,   126.26617,   123.11086,
        118.73707,   114.71235,   110.60724,   106.30681,   102.34528,    98.704938,
         94.049636,   90.508733,   86.518686,   82.450120,   78.500792,   74.450330,
         70.464653,   66.306477,   62.432229,   58.551831,   54.217369,   50.309328,
         46.377035,   42.283366,   38.009784,   33.947118,   29.633297,   25.007929,
         20.438188,   16.448403,    9.4409550,   3.8414434,   3.0592776,   2.2816549,
          1.5032161,  -0.1228125,  -INFINITY,   -INFINITY,   -INFINITY,   -INFINITY,
        -INFINITY,    -INFINITY,   -INFINITY,   -INFINITY,   -INFINITY     };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearUp2 = {
        160.00000,  122.55222,      118.30004,  115.51772,  111.51357,  108.26232,
        103.80239,   99.730105,      95.596368,  91.276051,  87.304125,  83.657303,
         78.996866,  75.453467,      71.461492,  67.391637,  63.441417,  59.390164,
         55.403653,  51.244363,      47.368622,  43.485964,  39.147478,  35.233563,
         31.291773,  27.181802,      22.879012,  18.767836,  14.361317,   9.5456304,
          4.6048044,  0.0049964955, -INFINITY,  -INFINITY,  -INFINITY,   -INFINITY,
        -INFINITY,   -INFINITY,     -INFINITY,  -INFINITY,  -INFINITY,   -INFINITY,
        -INFINITY,   -INFINITY,     -INFINITY,  -INFINITY,  -INFINITY     };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearUp3 = {
        160.00000,  113.01860,  108.76100,  105.97600,   101.97155,   98.719172,
         94.259468,  90.187341,  86.053267,  81.732501,   77.760038,  74.112390,
         69.450098,  65.904512,  61.908740,  57.832737,   53.873032,  49.806370,
         45.795801,  41.596427,  37.661582,  33.686708,   29.183457,  25.033104,
         20.721753,  16.010185,  10.710873,   5.1025001, -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,   -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,   -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,   -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearMicro = {
        160.00000,   137.77543,   134.01803,    131.46589,   127.68128,   124.54800,
        120.18252,   116.16993,   112.07004,    107.77290,   103.81385,   100.17442,
         95.520355,   91.979876,   87.990125,    83.921932,   79.972951,   75.922907,
         71.937750,   67.780410,   63.907352,    60.028788,   55.697592,   51.794229,
         47.869227,   43.787357,   39.533082,    35.498577,   31.229654,   26.675984,
         22.207908,   18.336999,   11.618540,     6.3382417,   5.6081329,   4.8842446,
          4.1617533,   2.6594494,   0.72947217,  -INFINITY,   -INFINITY,   -INFINITY,
        -INFINITY,    -INFINITY,   -INFINITY,    -INFINITY,   -INFINITY,    };
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
// The subsequent methods are used when updating the kPrev threshold arrays.
// They display the current run's results in an easily-imported format.
//
void AudioResult::DumpThresholdValues() {
  DumpFreqRespValues();

  DumpSinadValues();

  DumpLevelValues();
  DumpLevelToleranceValues();
  DumpNoiseFloorValues();
  DumpDynamicRangeValues();

  printf("\n\n");
}

void AudioResult::DumpFreqRespValues() {
  printf("\n\n Frequency Response");
  printf("\n   (all results given in dB)");

  DumpFreqRespValueSet(AudioResult::FreqRespPointUnity.data(), "FR-PointUnity");
  DumpFreqRespValueSet(AudioResult::FreqRespPointDown0.data(), "FR-PointDown0");
  DumpFreqRespValueSet(AudioResult::FreqRespPointDown1.data(), "FR-PointDown1");
  DumpFreqRespValueSet(AudioResult::FreqRespPointDown2.data(), "FR-PointDown2");
  DumpFreqRespValueSet(AudioResult::FreqRespPointUp1.data(), "FR-PointUp1");
  DumpFreqRespValueSet(AudioResult::FreqRespPointUp2.data(), "FR-PointUp2");
  DumpFreqRespValueSet(AudioResult::FreqRespPointUp3.data(), "FR-PointUp3");
  DumpFreqRespValueSet(AudioResult::FreqRespPointMicro.data(), "FR-PointMicro");

  DumpFreqRespValueSet(AudioResult::FreqRespLinearUnity.data(),
                       "FR-LinearUnity");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearDown0.data(),
                       "FR-LinearDown0");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearDown1.data(),
                       "FR-LinearDown1");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearDown2.data(),
                       "FR-LinearDown2");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearUp1.data(), "FR-LinearUp1");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearUp2.data(), "FR-LinearUp2");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearUp3.data(), "FR-LinearUp3");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearMicro.data(),
                       "FR-LinearMicro");

  DumpFreqRespValueSet(AudioResult::FreqRespPointNxN.data(), "FR-PointNxN");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearNxN.data(), "FR-LinearNxN");
}

void AudioResult::DumpSinadValues() {
  printf("\n\n Signal-to-Noise+Distortion");
  printf("\n   (all results given in dB)");

  DumpSinadValueSet(AudioResult::SinadPointUnity.data(), "SinadPointUnity");
  DumpSinadValueSet(AudioResult::SinadPointDown0.data(), "SinadPointDown0");
  DumpSinadValueSet(AudioResult::SinadPointDown1.data(), "SinadPointDown1");
  DumpSinadValueSet(AudioResult::SinadPointDown2.data(), "SinadPointDown2");
  DumpSinadValueSet(AudioResult::SinadPointUp1.data(), "SinadPointUp1");
  DumpSinadValueSet(AudioResult::SinadPointUp2.data(), "SinadPointUp2");
  DumpSinadValueSet(AudioResult::SinadPointUp3.data(), "SinadPointUp3");
  DumpSinadValueSet(AudioResult::SinadPointMicro.data(), "SinadPointMicro");

  DumpSinadValueSet(AudioResult::SinadLinearUnity.data(), "SinadLinearUnity");
  DumpSinadValueSet(AudioResult::SinadLinearDown0.data(), "SinadLinearDown0");
  DumpSinadValueSet(AudioResult::SinadLinearDown1.data(), "SinadLinearDown1");
  DumpSinadValueSet(AudioResult::SinadLinearDown2.data(), "SinadLinearDown2");
  DumpSinadValueSet(AudioResult::SinadLinearUp1.data(), "SinadLinearUp1");
  DumpSinadValueSet(AudioResult::SinadLinearUp2.data(), "SinadLinearUp2");
  DumpSinadValueSet(AudioResult::SinadLinearUp3.data(), "SinadLinearUp3");
  DumpSinadValueSet(AudioResult::SinadLinearMicro.data(), "SinadLinearMicro");

  DumpSinadValueSet(AudioResult::SinadPointNxN.data(), "SinadPointNxN");
  DumpSinadValueSet(AudioResult::SinadLinearNxN.data(), "SinadLinearNxN");
}

// Display a single frequency response results array, for import and processing.
void AudioResult::DumpFreqRespValueSet(double* freq_resp_vals,
                                       const std::string& arr_name) {
  printf("\n   %s", arr_name.c_str());
  for (size_t freq = 0; freq < FrequencySet::kReferenceFreqs.size(); ++freq) {
    if (freq % 6 == 0) {
      printf("\n      ");
    }
    printf("  %14.7le,", freq_resp_vals[freq]);
  }
  printf("\n");
}

// Display a single sinad results array, for import and processing.
void AudioResult::DumpSinadValueSet(double* sinad_vals,
                                    const std::string& arr_name) {
  printf("\n   %s", arr_name.c_str());
  for (size_t freq = 0; freq < FrequencySet::kReferenceFreqs.size(); ++freq) {
    if (freq % 6 == 0) {
      printf("\n     ");
    }
    printf("   %11.7lf,", ((isinf(sinad_vals[freq]) && sinad_vals[freq] > 0.0)
                               ? 160.0
                               : sinad_vals[freq]));
  }
  printf("\n");
}

void AudioResult::DumpLevelValues() {
  printf("\n\n Level (in dB)");
  printf("\n       8-bit:   Source %15.8le  Mix %15.8le  Output %15.8le",
         AudioResult::LevelSource8, AudioResult::LevelMix8,
         AudioResult::LevelOutput8);

  printf("\n       16-bit:  Source %15.8le  Mix %15.8le  Output %15.8le",
         AudioResult::LevelSource16, AudioResult::LevelMix16,
         AudioResult::LevelOutput16);

  printf("\n       24-bit:  Source %15.8le  Mix %15.8le  Output %15.8le",
         AudioResult::LevelSource24, AudioResult::LevelMix24,
         AudioResult::LevelOutput24);

  printf("\n       Float:   Source %15.8le  Mix %15.8le  Output %15.8le",
         AudioResult::LevelSourceFloat, AudioResult::LevelMixFloat,
         AudioResult::LevelOutputFloat);
  printf("\n       Stereo-to-Mono: %15.8le", AudioResult::LevelStereoMono);

  printf("\n");
}

void AudioResult::DumpLevelToleranceValues() {
  printf("\n\n Level Tolerance (in dB)");
  printf("\n       8-bit:   Source %15.8le  Mix %15.8le  Output %15.8le",
         AudioResult::LevelToleranceSource8, AudioResult::LevelToleranceMix8,
         AudioResult::LevelToleranceOutput8);

  printf("\n       16-bit:  Source %15.8le  Mix %15.8le  Output %15.8le",
         AudioResult::LevelToleranceSource16, AudioResult::LevelToleranceMix16,
         AudioResult::LevelToleranceOutput16);

  printf("\n       24-bit:  Source %15.8le  Mix %15.8le  Output %15.8le",
         AudioResult::LevelToleranceSource24, AudioResult::LevelToleranceMix24,
         AudioResult::LevelToleranceOutput24);

  printf("\n       Float:   Source %15.8le  Mix %15.8le  Output %15.8le",
         AudioResult::LevelToleranceSourceFloat,
         AudioResult::LevelToleranceMixFloat,
         AudioResult::LevelToleranceOutputFloat);

  printf("\n       Stereo-to-Mono: %15.8le               ",
         AudioResult::LevelToleranceStereoMono);
  printf("Interpolation: %15.8le", LevelToleranceInterpolation);

  printf("\n");
}

void AudioResult::DumpNoiseFloorValues() {
  printf("\n\n Noise Floor (in dB)");
  printf("\n       8-bit:   Source %11.7lf  Mix %11.7lf  Output %11.7lf",
         AudioResult::FloorSource8, AudioResult::FloorMix8,
         AudioResult::FloorOutput8);
  printf("\n       16-bit:  Source %11.7lf  Mix %11.7lf  Output %11.7lf",
         AudioResult::FloorSource16, AudioResult::FloorMix16,
         AudioResult::FloorOutput16);
  printf("\n       24-bit:  Source %11.7lf  Mix %11.7lf  Output %11.7lf",
         AudioResult::FloorSource24, AudioResult::FloorMix24,
         AudioResult::FloorOutput24);
  printf("\n       Float:   Source %11.7lf  Mix %11.7lf  Output %11.7lf",
         AudioResult::FloorSourceFloat, AudioResult::FloorMixFloat,
         AudioResult::FloorOutputFloat);
  printf("\n       Stereo-to-Mono: %11.7lf", AudioResult::FloorStereoMono);

  printf("\n");
}

void AudioResult::DumpDynamicRangeValues() {
  printf("\n\n Dynamic Range");
  printf("\n       Epsilon:  %10.7e  (%13.6le dB)", AudioResult::ScaleEpsilon,
         Gain::ScaleToDb(1.0 - AudioResult::ScaleEpsilon));
  printf("  Level: %15.8le dB  Sinad: %10.6lf dB",
         AudioResult::LevelEpsilonDown, AudioResult::SinadEpsilonDown);

  printf("\n       -30 dB down:                                ");
  printf("  Level: %15.8lf dB  Sinad: %10.6lf dB", AudioResult::Level30Down,
         AudioResult::Sinad30Down);

  printf("\n       -60 dB down:                                ");
  printf("  Level: %15.8lf dB  Sinad: %10.6lf dB", AudioResult::Level60Down,
         AudioResult::Sinad60Down);

  printf("\n       -90 dB down:                                ");
  printf("  Level: %15.8lf dB  Sinad: %10.6lf dB", AudioResult::Level90Down,
         AudioResult::Sinad90Down);

  printf("\n       Gain Accuracy:   +/- %12.7le dB",
         AudioResult::DynRangeTolerance);

  printf("\n       MinScale: %10.8f  (%11.8f dB)", AudioResult::MinScaleNonZero,
         Gain::ScaleToDb(AudioResult::MinScaleNonZero));

  printf("\n");
}

}  // namespace media::audio::test
