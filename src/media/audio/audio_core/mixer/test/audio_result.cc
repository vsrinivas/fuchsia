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
         0.0000000e+00, -1.9772600e-09, -5.3325766e-10, -5.3325381e-10, -1.9772590e-09, -5.3325670e-10,
        -5.3325188e-10, -5.3325574e-10, -5.3324995e-10, -5.3324802e-10, -5.3326249e-10, -5.3325477e-10,
        -5.3324513e-10, -5.3045726e-10, -5.3043797e-10, -5.3318245e-10, -5.3304358e-10, -5.3029525e-10,
        -5.3021232e-10, -5.2741866e-10, -5.3282082e-10, -5.2770507e-10, -5.2953150e-10, -5.2982369e-10,
        -5.2636369e-10, -5.3142834e-10, -5.2545818e-10, -5.2888540e-10, -5.2436078e-10, -5.2107724e-10,
        -5.0774735e-10, -5.2798954e-10, -4.9616384e-10, -5.1692003e-10, -5.2461536e-10, -5.1789786e-10,
        -5.2736370e-10, -5.2348999e-10, -4.9876946e-10   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespPointDown0 = {
         0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,
         0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,
         0.0000000e+00,  0.0000000e+00, -7.6167569e-06, -2.2065471e-05, -4.4301451e-05, -8.0447299e-05,
        -1.3688820e-04, -2.3097176e-04, -3.6979005e-04, -5.8658702e-04, -9.7451682e-04, -1.5323031e-03,
        -2.4060773e-03, -3.8308639e-03, -6.1864172e-03, -9.6785288e-03, -1.5380966e-02, -2.4801839e-02,
        -3.8773775e-02, -5.5863219e-02, -9.9429153e-02, -1.4793359e-01, -1.5566899e-01, -1.6358288e-01,
        -1.7172348e-01, -1.8941808e-01, -2.1391896e-01   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespPointDown1 = {
         0.0000000e+00, -1.9772600e-09, -5.3325766e-10, -5.3325381e-10, -1.9772590e-09, -5.3325670e-10,
        -5.3325188e-10, -5.3325574e-10, -5.3324995e-10, -5.3324802e-10, -5.3326249e-10, -5.3325477e-10,
        -5.3324513e-10, -5.3045726e-10, -5.3043797e-10, -5.3318245e-10, -5.3304358e-10, -5.3029525e-10,
        -5.3021232e-10, -5.2741866e-10, -5.3282082e-10, -5.2770507e-10, -5.2953150e-10, -5.2982369e-10,
        -5.2636369e-10, -5.3142834e-10, -5.2545818e-10, -5.2888540e-10, -5.2436078e-10, -5.2107724e-10,
        -5.0774735e-10, -5.2798954e-10, -4.9616384e-10, -5.1692003e-10, -5.2461536e-10, -5.1789786e-10,
        -5.2736370e-10, -5.2348999e-10, -4.9876946e-10   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespPointDown2 = {
         0.0000000e+00, -6.2596902e-07, -4.8486401e-07, -8.3953895e-07, -1.7341983e-06, -1.7054787e-06,
        -2.5086276e-06, -3.6790292e-06, -6.6867260e-06, -1.2715651e-05, -1.8615573e-05, -2.8272419e-05,
        -4.9951775e-05, -7.7895112e-05, -1.1235828e-04, -1.9001507e-04, -2.9166488e-04, -4.6088216e-04,
        -7.2807981e-04, -1.0587223e-03, -1.9767753e-03, -2.8387846e-03, -4.6513902e-03, -7.3007185e-03,
        -1.1510677e-02, -1.8177940e-02, -2.9400390e-02, -4.5992881e-02, -7.3018866e-02, -1.1805297e-01,
        -1.8410856e-01, -2.6593496e-01, -4.7532502e-01, -7.1036816e-01, -7.4753858e-01, -7.8457007e-01,
        -8.2582389e-01, -9.1233341e-01, -1.0311764e+00   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespPointMicro = {
         0.0000000e+00, -6.7346053e-05, -6.8027948e-05, -6.8693208e-05, -7.0113546e-05, -7.1859864e-05,
        -7.5611942e-05, -8.1200142e-05, -9.0300956e-05, -1.0578830e-04, -1.2869999e-04, -1.6127137e-04,
        -2.2872942e-04, -3.1055749e-04, -4.5305886e-04, -6.8417644e-04, -1.0398390e-03, -1.6180125e-03,
        -2.5208306e-03, -4.0258770e-03, -6.2466697e-03, -9.7154158e-03, -1.5923567e-02, -2.4852933e-02,
        -3.8847676e-02, -6.1685799e-02, -9.9492813e-02, -1.5565587e-01, -2.4766233e-01, -4.0047672e-01,
        -6.2901569e-01, -9.1172791e-01, -1.6491462e+00, -2.5006335e+00, -2.6395794e+00, -2.7826453e+00,
        -2.9307638e+00, -3.2558272e+00, -3.7065369e+00   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespPointUp1 = {
         0.0000000e+00, -1.5832376e-06, -2.1171757e-06, -2.7501295e-06, -5.1496270e-06, -6.5730573e-06,
        -1.1328067e-05, -1.8434104e-05, -2.8901436e-05, -4.5812340e-05, -7.3843674e-05, -1.1242401e-04,
        -1.9065096e-04, -2.8389607e-04, -4.6048005e-04, -7.2511074e-04, -1.1502008e-03, -1.8372642e-03,
        -2.9071496e-03, -4.8055019e-03, -7.1764712e-03, -1.1451720e-02, -1.8833487e-02, -2.9408733e-02,
        -4.5925362e-02, -7.3082204e-02, -1.1785402e-01, -1.8444377e-01, -2.9369205e-01, -4.7501296e-01,
        -7.4752475e-01, -1.0846189e+00, -1.9686826e+00, -2.9983602e+00, -3.1678731e+00, -3.3437676e+00,
        -3.5230954e+00, -3.9212857e+00, -INFINITY        };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespPointUp2 = {
         0.0000000e+00, -3.2376516e-06, -5.2813212e-06, -7.2772930e-06, -1.1540895e-05, -1.6778123e-05,
        -2.8035418e-05, -4.4801620e-05, -7.2106624e-05, -1.1857317e-04, -1.8731521e-04, -2.8503972e-04,
        -4.8743716e-04, -7.3295307e-04, -1.1605211e-03, -1.8540016e-03, -2.9212432e-03, -4.6563262e-03,
        -7.3660288e-03, -1.1884252e-02, -1.8553473e-02, -2.8975872e-02, -4.7645944e-02, -7.4537346e-02,
        -1.1677339e-01, -1.8593632e-01, -3.0108726e-01, -4.7368842e-01, -7.6056343e-01, -1.2489868e+00,
        -2.0099216e+00, -3.0090511e+00, -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespPointUp3 = {
         0.0000000e+00, -2.8231344e-04, -2.8154133e-05, -3.8797535e-05, -3.2659139e-04, -8.9460237e-05,
        -1.4948930e-04, -2.3889458e-04, -3.8449631e-04, -6.3228220e-04, -9.9885435e-04, -1.5199851e-03,
        -2.5993273e-03, -3.9086607e-03, -6.1890052e-03, -9.8878885e-03, -1.5581185e-02, -2.4839333e-02,
        -3.9303349e-02, -6.3435778e-02, -9.9090839e-02, -1.5489253e-01, -2.5510240e-01, -4.0001110e-01,
        -6.2899807e-01, -1.0077690e+00, -1.6494605e+00, -2.6398568e+00, -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespLinearUnity = {
         0.0000000e+00, -1.9772600e-09, -5.3325766e-10, -5.3325381e-10, -1.9772590e-09, -5.3325670e-10,
        -5.3325188e-10, -5.3325574e-10, -5.3324995e-10, -5.3324802e-10, -5.3326249e-10, -5.3325477e-10,
        -5.3324513e-10, -5.3045726e-10, -5.3043797e-10, -5.3318245e-10, -5.3304358e-10, -5.3029525e-10,
        -5.3021232e-10, -5.2741866e-10, -5.3282082e-10, -5.2770507e-10, -5.2953150e-10, -5.2982369e-10,
        -5.2636369e-10, -5.3142834e-10, -5.2545818e-10, -5.2888540e-10, -5.2436078e-10, -5.2107724e-10,
        -5.0774735e-10, -5.2798954e-10, -4.9616384e-10, -5.1692003e-10, -5.2461536e-10, -5.1789786e-10,
        -5.2736370e-10, -5.2348999e-10, -4.9876946e-10   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespLinearDown0 = {
         0.0000000e+00, -1.3585336e-07, -2.1995828e-07, -3.0365808e-07, -4.8060721e-07, -6.9727951e-07,
        -1.1666417e-06, -1.8670239e-06, -3.0028625e-06, -4.9403449e-06, -7.8070357e-06, -1.1876400e-05,
        -2.0308398e-05, -3.0537612e-05, -4.8352641e-05, -7.7244708e-05, -1.2170742e-04, -1.9397967e-04,
        -3.0683389e-04, -4.9495770e-04, -7.7252362e-04, -1.2060122e-03, -1.9816808e-03, -3.0969817e-03,
        -4.8440979e-03, -7.6929614e-03, -1.2402883e-02, -1.9385323e-02, -3.0787189e-02, -4.9623713e-02,
        -7.7559173e-02, -1.1172646e-01, -1.9881829e-01, -2.9574063e-01, -3.1118771e-01, -3.2698546e-01,
        -3.4323268e-01, -3.7847005e-01, -4.2654783e-01   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespLinearDown1 = {
         0.0000000e+00, -1.9772600e-09, -5.3325766e-10, -5.3325381e-10, -1.9772590e-09, -5.3325670e-10,
        -5.3325188e-10, -5.3325574e-10, -5.3324995e-10, -5.3324802e-10, -5.3326249e-10, -5.3325477e-10,
        -5.3324513e-10, -5.3045726e-10, -5.3043797e-10, -5.3318245e-10, -5.3304358e-10, -5.3029525e-10,
        -5.3021232e-10, -5.2741866e-10, -5.3282082e-10, -5.2770507e-10, -5.2953150e-10, -5.2982369e-10,
        -5.2636369e-10, -5.3142834e-10, -5.2545818e-10, -5.2888540e-10, -5.2436078e-10, -5.2107724e-10,
        -5.0774735e-10, -5.2798954e-10, -4.9616384e-10, -5.1692003e-10, -5.2461536e-10, -5.1789786e-10,
        -5.2736370e-10, -5.2348999e-10, -4.9876946e-10   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespLinearDown2 = {
         0.0000000e+00, -6.3822570e-07, -1.0395043e-06, -1.4355155e-06, -2.2783814e-06, -3.3105243e-06,
        -5.5342438e-06, -8.8452921e-06, -1.4234833e-05, -2.3412091e-05, -3.6983731e-05, -5.6277393e-05,
        -9.6239638e-05, -1.4471410e-04, -2.2913233e-04, -3.6604707e-04, -5.7673735e-04, -9.1923731e-04,
        -1.4540329e-03, -2.3455347e-03, -3.6609235e-03, -5.7152998e-03, -9.3915140e-03, -1.4677821e-02,
        -2.2959819e-02, -3.6467196e-02, -5.8805719e-02, -9.1939220e-02, -1.4608711e-01, -2.3565981e-01,
        -3.6877127e-01, -5.3202083e-01, -9.5038397e-01, -1.4198462e+00, -1.4950525e+00, -1.5720769e+00,
        -1.6513831e+00, -1.8238856e+00, -2.0601325e+00   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespLinearMicro = {
         0.0000000e+00, -2.1557555e-06, -3.5192588e-06, -4.8508075e-06, -7.6898359e-06, -1.1183542e-05,
        -1.8688780e-05, -2.9864507e-05, -4.8068298e-05, -7.9045339e-05, -1.2486976e-04, -1.9001665e-04,
        -3.2494207e-04, -4.8860973e-04, -7.7362791e-04, -1.2358913e-03, -1.9472599e-03, -3.1036778e-03,
        -4.9094224e-03, -7.9196999e-03, -1.2361559e-02, -1.9299475e-02, -3.1716536e-02, -4.9576367e-02,
        -7.7567580e-02, -1.2324665e-01, -1.9886543e-01, -3.1119868e-01, -4.9522362e-01, -8.0087328e-01,
        -1.2579849e+00, -1.8234558e+00, -3.2984523e+00, -5.0017731e+00, -5.2797597e+00, -5.5660114e+00,
        -5.8625304e+00, -6.5131186e+00, -7.4182303e+00   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespLinearUp1 = {
         0.0000000e+00, -2.5516310e-06, -4.1678491e-06, -5.7439283e-06, -9.1112253e-06, -1.3248613e-05,
        -2.2139066e-05, -3.5381715e-05, -5.6948304e-05, -9.3646159e-05, -1.4793727e-04, -2.2511883e-04,
        -3.8496865e-04, -5.7887223e-04, -9.1654360e-04, -1.4642074e-03, -2.3069974e-03, -3.6770605e-03,
        -5.8164285e-03, -9.3829067e-03, -1.4645559e-02, -2.2865714e-02, -3.7578247e-02, -5.8741085e-02,
        -9.1912259e-02, -1.4605310e-01, -2.3570309e-01, -3.6893407e-01, -5.8733496e-01, -9.5047204e-01,
        -1.4944954e+00, -2.1690869e+00, -3.9376310e+00, -5.9976308e+00, -6.3357708e+00, -6.6845975e+00,
        -7.0464553e+00, -7.8433525e+00, -INFINITY        };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespLinearUp2 = {
         0.0000000e+00, -6.4749325e-06, -1.0563305e-05, -1.4552500e-05, -2.3077789e-05, -3.3551619e-05,
        -5.6066745e-05, -8.9601458e-05, -1.4421078e-04, -2.3714242e-04, -3.7462855e-04, -5.7008073e-04,
        -9.7487178e-04, -1.4659017e-03, -2.3210368e-03, -3.7080022e-03, -5.8424840e-03, -9.3126509e-03,
        -1.4732055e-02, -2.3768503e-02, -3.7106945e-02, -5.7951740e-02, -9.5291884e-02, -1.4907470e-01,
        -2.3354677e-01, -3.7187263e-01, -6.0217451e-01, -9.4737683e-01, -1.5211269e+00, -2.4979735e+00,
        -4.0198431e+00, -6.0181021e+00, -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespLinearUp3 = {
         0.0000000e+00, -3.4481570e-05, -5.6307859e-05, -7.7595284e-05, -1.2303802e-04, -1.7892723e-04,
        -2.9899015e-04, -4.7780857e-04, -7.6902606e-04, -1.2646195e-03, -1.9977967e-03, -3.0401078e-03,
        -5.1988894e-03, -7.8176793e-03, -1.2378574e-02, -1.9776681e-02, -3.1163795e-02, -4.9680939e-02,
        -7.8610299e-02, -1.2687738e-01, -1.9819079e-01, -3.0979930e-01, -5.1022835e-01, -8.0005931e-01,
        -1.2580550e+00, -2.0156336e+00, -3.2990811e+00, -5.2796564e+00, -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespSincUnity = {
         0.0000000e+00, -1.9772600e-09, -5.3325766e-10, -5.3325381e-10, -1.9772590e-09, -5.3325670e-10,
        -5.3325188e-10, -5.3325574e-10, -5.3324995e-10, -5.3324802e-10, -5.3326249e-10, -5.3325477e-10,
        -5.3324513e-10, -5.3045726e-10, -5.3043797e-10, -5.3318245e-10, -5.3304358e-10, -5.3029525e-10,
        -5.3021232e-10, -5.2741866e-10, -5.3282082e-10, -5.2770507e-10, -5.2953150e-10, -5.2982369e-10,
        -5.2636369e-10, -5.3142834e-10, -5.2545818e-10, -5.2888540e-10, -5.2436078e-10, -5.2107724e-10,
        -5.0774735e-10, -5.2798954e-10, -4.9616384e-10, -5.1692003e-10, -5.2461536e-10, -5.1789786e-10,
        -5.2736370e-10, -5.2348999e-10, -4.9876946e-10   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespSincDown0 = {
         0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,
        -1.7404081e-07, -6.9244134e-07, -1.4743216e-06, -2.8257302e-06, -4.8199611e-06, -7.6678763e-06,
        -1.3474435e-05, -2.0494837e-05, -3.2589669e-05, -5.1873183e-05, -8.0820271e-05, -1.2582790e-04,
        -1.9141539e-04, -2.8857590e-04, -4.0631075e-04, -5.3697990e-04, -6.4301944e-04, -6.0592097e-04,
        -3.5519330e-04, -3.9806080e-06, -1.4540463e-04, -7.2541146e-04, -4.1218586e-05, -6.6741514e-04,
         0.0000000e+00, -1.6457271e-03, -1.7518671e-03, -1.4497144e-03,  0.0000000e+00,  0.0000000e+00,
         0.0000000e+00, -5.4991612e-01, -3.4730172e+00   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespSincDown1 = {
         0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00, -1.5093121e-07,
        -4.6748614e-07, -9.4027965e-07, -1.7056479e-06, -3.0173098e-06, -4.9382432e-06, -7.6789531e-06,
        -1.3318045e-05, -2.0120742e-05, -3.1841177e-05, -5.0532158e-05, -7.8544758e-05, -1.2215923e-04,
        -1.8568906e-04, -2.7978260e-04, -3.9386611e-04, -5.2047789e-04, -6.2321960e-04, -5.8719850e-04,
        -3.4402141e-04, -3.0691059e-06, -1.4065106e-04, -7.0545058e-04, -3.7001053e-05, -6.5207005e-04,
         0.0000000e+00, -1.6238332e-03, -1.7349716e-03, -1.4322207e-03,  0.0000000e+00,  0.0000000e+00,
         0.0000000e+00, -5.4992795e-01, -3.4731415e+00   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespSincDown2 = {
        -2.7786342e-06, -2.8643108e-06, -2.9336074e-06, -2.9829440e-06, -3.0969980e-06, -3.2465202e-06,
        -3.6016495e-06, -4.0806446e-06, -4.8543438e-06, -6.2332748e-06, -8.2131584e-06, -1.1034815e-05,
        -1.6861248e-05, -2.3878210e-05, -3.5976578e-05, -5.5265081e-05, -8.4173285e-05, -1.2918967e-04,
        -1.9476016e-04, -2.9187150e-04, -4.0956566e-04, -5.4020276e-04, -6.4618804e-04, -6.0909813e-04,
        -3.5846301e-04, -7.3386291e-06, -1.4873730e-04, -7.2859619e-04, -4.4525496e-05, -6.7068610e-04,
         0.0000000e+00, -1.6488988e-03, -1.7545839e-03, -1.4486218e-03,  0.0000000e+00,  0.0000000e+00,
         0.0000000e+00, -5.5003017e-01, -3.4734047e+00   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespSincMicro = {
         0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,
         0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,
         0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,
         0.0000000e+00,  0.0000000e+00, -8.6360272e-05, -2.1698221e-04, -3.2294316e-04, -2.8583485e-04,
        -3.5228817e-05,  0.0000000e+00,  0.0000000e+00, -4.0536908e-04,  0.0000000e+00, -3.4749668e-04,
         0.0000000e+00, -1.3256803e-03, -1.4309763e-03, -1.1227815e-03,  0.0000000e+00,  0.0000000e+00,
         0.0000000e+00, -5.4976541e-01, -3.4732707e+00   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespSincUp1 = {
         0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,
         0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,
         0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,
         0.0000000e+00, -1.0633331e-05, -1.3605120e-04, -2.6234479e-04, -3.2995479e-04, -2.1707738e-04,
         0.0000000e+00,  0.0000000e+00, -6.6785200e-05, -3.5600251e-04,  0.0000000e+00, -6.4548144e-04,
         0.0000000e+00,  0.0000000e+00,  0.0000000e+00, -2.3861505e-02, -2.9653185e-01, -8.8041635e-01,
        -1.9014571e+00, -6.0104679e+00, -INFINITY        };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespSincUp2 = {
         0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,
         0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,
         0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00, -8.4918536e-05,
        -2.1509513e-04, -3.1296239e-04, -2.7769224e-04, -3.6950990e-05,  0.0000000e+00,  0.0000000e+00,
        -3.9516209e-04,  0.0000000e+00, -3.4263731e-04,  0.0000000e+00, -4.9772948e-04, -1.4193135e-03,
         0.0000000e+00, -5.9983626e+00, -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx>
    AudioResult::kPrevFreqRespSincUp3 = {
         0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,
         0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,  0.0000000e+00,
         0.0000000e+00,  0.0000000e+00, -8.6755175e-05, -2.2384099e-04, -3.2105225e-04, -2.8518889e-04,
        -2.4759288e-05,  0.0000000e+00,  0.0000000e+00, -4.0305749e-04,  0.0000000e+00, -3.4344110e-04,
         0.0000000e+00, -5.0889983e-04, -1.4224909e-03,  0.0000000e+00, -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY        };
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
         160.00000,  153.71437,  153.74509,  153.74509,  153.71437,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509, -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadPointDown0 = {
        160.00000,    78.100700,   75.968814,   74.574907,   72.571433,  70.944708,
         68.714318,   66.678031,   64.610952,   62.450676,   60.464723,  58.641356,
         56.311209,   54.539649,   52.543931,   50.509457,   48.535039,  46.510546,
         44.519061,   42.442371,   40.508865,   38.574359,   36.417344,  34.478013,
         32.534833,   30.525314,   28.449845,   26.508594,   24.496768,  22.418858,
         20.472419,   18.878621,   16.353740,   14.604762,   14.379740,  14.160676,
         13.946021,   13.512504,   12.980117,    0.21968030, -2.8956865, -2.8905969,
         -2.8807074,  -2.8601013,  -2.8206616,  -2.7070291,  -2.6450671, -2.6434551,
         -2.5855444,  -1.1168558,   0.1662408,   1.1078607    };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadPointDown1 = {
        160.00000,   153.71437,   153.74509,   153.74509,   153.71437,   153.74509,
        153.74509,   153.74509,   153.74509,   153.74509,   153.74509,   153.74509,
        153.74509,   153.74509,   153.74509,   153.74509,   153.74509,   153.74509,
        153.74509,   153.74509,   153.74509,   153.74509,   153.74509,   153.74509,
        153.74509,   153.74509,   153.74509,   153.74509,   153.74509,   153.74509,
        153.74509,   153.74509,   153.74509,   153.74509,   153.74509,   153.74509,
        153.74509,   153.74509,   153.74509,    -0.0000000,  -3.0103000,  -3.0103000,
         -3.0103000,  -3.0103000,  -3.0103000,  -3.0103000,  -3.0103000,  -3.0103000,
         -3.0103000,  -3.0103000,  -INFINITY,   -INFINITY     };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadPointDown2 = {
        160.00000,   71.336883,  69.207779,  67.815057,   65.812756,  64.1866540,
         61.956815,  59.920833,  57.853950,  55.693792,   53.707908,  51.8845804,
         49.554454,  47.782891,  45.787179,  43.752662,   41.778206,  39.7536393,
         37.762030,  35.685358,  33.751020,  31.816361,   29.658528,  27.7179334,
         25.772655,  23.760110,  21.679242,  19.730083,   17.705500,  15.6057957,
         13.628602,  11.995321,   9.3691293,  7.5050725,   7.2624319,  7.0272922,
          6.7900251,  6.3134705,  5.7218652,  0.96738425, -2.4810757, -2.4608342,
         -2.4180658, -2.3286644, -2.1612791, -1.7067867,  -1.4834253, -1.4774358,
         -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY     };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadPointMicro = {
        160.00000,    66.059498,   63.927622,   62.533706,  60.530232,   58.903507,
         56.673113,   54.636823,   52.569740,   50.409454,  48.423488,   46.600103,
         44.269918,   42.498312,   40.502513,   38.467909,  36.493292,   34.468473,
         32.476480,   30.398945,   28.464189,   26.527731,  24.367223,   22.422867,
         20.471807,   18.449418,   16.352619,   14.379621,  12.315635,   10.150669,
          8.0730525,   6.3153546,   3.3545122,   1.0870714,  0.77593904,  0.46778072,
          0.16043152, -0.47803470, -1.2962395,   2.2568880, -INFINITY,   -INFINITY,
         -INFINITY,   -INFINITY,   -INFINITY,   -INFINITY,  -INFINITY,   -INFINITY,
         -INFINITY,   -INFINITY,   -INFINITY,   -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadPointUp1 = {
        160.00000,   65.316279,  63.187172,  61.794455,    59.792148,   58.166050,
         55.936206,  53.900223,  51.833335,  49.673175,    47.687276,   45.863930,
         43.533774,  41.762183,  39.766365,  37.731761,    35.757101,   33.732217,
         31.740125,  29.662198,  27.727717,  25.790540,    23.629291,   21.683953,
         19.731471,  17.706360,  15.605426,  13.626141,    11.551576,    9.3692819,
          7.2636824,  5.4716792,  2.4143111,  0.022407171, -0.30963110, -0.64294050,
         -0.9717976, -1.6649323, -INFINITY,  -INFINITY,    -INFINITY,   -INFINITY,
         -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,    -INFINITY,   -INFINITY,
         -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY      };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadPointUp2 = {
        160.00000,   61.281148,   59.152040, 57.759321,  55.757015,  54.130912,
         51.901065,  49.865075,   47.798174, 45.637992,  43.652064,  41.828677,
         39.498430,  37.726726,   35.730739, 33.695819,  31.720708,  29.695082,
         27.701826,  25.622181,   23.684311, 21.742982,  19.573739,  17.616782,
         15.645886,  13.590901,   11.439506,  9.3839187,  7.1806586,  4.7728152,
          2.3024022,  0.0024982, -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
         -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
         -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
         -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadPointUp3 = {
        160.00000,   54.018289,  51.882570,  50.489845,    48.488997,  46.861413,
         44.631536,  42.595504,  40.528534,  38.368234,    36.382133,  34.558497,
         32.227739,  30.455413,  28.458343,  26.421665,    24.443848,  22.413821,
         20.413684,  18.322549,  16.367675,  14.399672,    12.182339,  10.155407,
          8.0728947,  5.8304698,  3.3535174,  0.77575507, -INFINITY,  -INFINITY,
         -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,   -INFINITY,  -INFINITY,
         -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,   -INFINITY,  -INFINITY,
         -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,   -INFINITY,  -INFINITY,
         -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY     };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadLinearUnity = {
         160.00000,  153.71437,  153.74509,  153.74509,  153.71437,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509, -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadLinearDown0 = {
        160.00000,   150.09451,   149.18729,    148.35118,    146.70671,   144.99044,
        142.02044,   138.82596,   135.26922,    131.31431,    127.56382,   124.03645,
        119.47721,   115.97714,   112.01948,    107.97295,    104.03684,    99.996249,
         96.018517,   91.868368,   88.003137,    84.135081,    79.821268,   75.942206,
         72.054711,   68.033531,   63.878842,    59.990656,    55.957665,   51.786410,
         47.870716,   44.655379,   39.535750,    35.961505,    35.499453,   35.049095,
         34.608562,   33.713319,   32.613549,     0.44379996,  -2.5439282,    -2.5246799,
         -2.4850111,  -2.4009194,  -2.2386017,   -1.7648961,   -1.5034877,  -1.4966666,
         -1.2548539,  -1.3929845,   0.49493586,   2.4070414     };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadLinearDown1 = {
        160.00000,   153.71437,   153.74509,   153.74509,   153.71437,   153.74509,
        153.74509,   153.74509,   153.74509,   153.74509,   153.74509,   153.74509,
        153.74509,   153.74509,   153.74509,   153.74509,   153.74509,   153.74509,
        153.74509,   153.74509,   153.74509,   153.74509,   153.74509,   153.74509,
        153.74509,   153.74509,   153.74509,   153.74509,   153.74509,   153.74509,
        153.74509,   153.74509,   153.74509,   153.74509,   153.74509,   153.74509,
        153.74509,   153.74509,   153.74509,    -0.000000,   -3.0103000,  -3.0103000,
         -3.0103000,  -3.0103000,  -3.0103000,  -3.0103000,  -3.0103000,  -3.0103000,
         -3.0103000,  -3.0103000,  -INFINITY,   -INFINITY     };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadLinearDown2 = {
        160.00000,   145.49338,   142.76626,   140.72251,   137.37211,   134.53647,
        130.42254,   126.53508,   122.51076,   118.26515,   114.33387,   110.71175,
        106.07058,   102.53716,    98.552405,   94.487585,   90.541183,   86.493364,
         82.510726,   78.356875,   74.488568,   70.617160,   66.298521,   62.413089,
         58.516039,   54.479521,   50.299693,   46.374454,   42.281300,   38.011435,
         33.951159,   30.561504,   25.008837,   20.969144,   20.434310,   19.909878,
         19.392276,   18.335140,   17.015322,    2.0146420,  -0.80102784, -0.7112235,
         -0.52695943, -0.14071615,  0.5816496,   2.3659791,   3.0180552,   3.0311011,
         -INFINITY,   -INFINITY,   -INFINITY,   -INFINITY     };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadLinearMicro = {
        160.00000,   137.76988,   134.03232,    131.46815,   127.67886,   124.54456,
        120.18273,   116.16898,   112.07060,    107.77343,   103.81402,   100.17507,
         95.520847,   91.980397,   87.990685,    83.922513,   79.973503,   75.923435,
         71.938287,   67.780938,   63.907889,    60.029323,   55.698125,   51.794764,
         47.869763,   43.787898,   39.533629,    35.499134,   31.230225,   26.676579,
         22.208537,   18.337668,   11.619305,     6.3391116,   5.6090198,   4.8851490,
          4.1629400,   2.6604117,   0.73049057,   4.2978495,  -INFINITY,   -INFINITY,
         -INFINITY,   -INFINITY,   -INFINITY,    -INFINITY,   -INFINITY,   -INFINITY,
         -INFINITY,   -INFINITY,   -INFINITY,    -INFINITY     };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadLinearUp1 = {
        160.00000,   136.51568,   132.67958,   130.09359,   126.26617,   123.11086,
        118.73707,   114.71235,   110.60724,   106.30681,   102.34528,    98.704938,
         94.049636,   90.508733,   86.518686,   82.450120,   78.500792,   74.450330,
         70.464653,   66.306477,   62.432229,   58.551831,   54.217369,   50.309328,
         46.377035,   42.283366,   38.009784,   33.947118,   29.633297,   25.007929,
         20.438188,   16.448403,    9.4409550,   3.8414434,   3.0592776,   2.2816549,
          1.5032161,  -0.1228125,  -INFINITY,   -INFINITY,   -INFINITY,   -INFINITY,
         -INFINITY,   -INFINITY,   -INFINITY,   -INFINITY,   -INFINITY,   -INFINITY,
         -INFINITY,   -INFINITY,   -INFINITY,   -INFINITY     };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadLinearUp2 = {
        160.00000,  122.55222,      118.30004,  115.51772,  111.51357,  108.26232,
        103.80239,   99.730105,      95.596368,  91.276051,  87.304125,  83.657303,
         78.996866,  75.453467,      71.461492,  67.391637,  63.441417,  59.390164,
         55.403653,  51.244363,      47.368622,  43.485964,  39.147478,  35.233563,
         31.291773,  27.181802,      22.879012,  18.767836,  14.361317,   9.5456304,
          4.6048044,  0.0049964955, -INFINITY,  -INFINITY,  -INFINITY,   -INFINITY,
         -INFINITY,  -INFINITY,     -INFINITY,  -INFINITY,  -INFINITY,   -INFINITY,
         -INFINITY,  -INFINITY,     -INFINITY,  -INFINITY,  -INFINITY,   -INFINITY,
         -INFINITY,  -INFINITY,     -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadLinearUp3 = {
        160.00000,  114.92278,  110.70576,  107.93401,   103.93920,  100.69688,
         96.243230,  92.174520,  88.042533,  83.722921,   79.750914,  76.103270,
         71.440342,  67.893700,  63.895919,  59.816535,   55.851565,  51.776324,
         47.752407,  43.530968,  39.564021,  35.540074,   30.952111,  26.686404,
         22.207971,  17.253978,  11.617010,   5.6092858, -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,   -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,   -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,   -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,   -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadSincUnity = {
         160.00000,  153.71437,  153.74509,  153.74509,  153.71437,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509,  153.74509,  153.74509,  153.74509,
         153.74509,  153.74509,  153.74509, -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadSincDown0 = {
        129.40270,  133.96650,  133.94496,  133.89234,   133.85668,   133.82565,
        133.81817,  133.60020,  133.47611,  133.16268,   132.76218,   132.21628,
        131.28687,  130.38314,  129.15577,  127.70545,   126.11829,   124.35822,
        122.54697,  120.58278,  118.71494,  116.82204,   114.70199,   112.78231,
        110.85191,  108.85049,  106.78283,  104.84564,   102.83767,   100.76739,
         98.830811,  97.247906,  94.749129,  93.029757,   92.809322,   92.595116,
         92.382052,  91.962843,  91.445770,   6.0207099,   5.9429246,   9.8067508,
         22.024291,  46.316996,  66.091794,  85.841319,   94.568624,   91.957147,
         96.610119,  92.000826,  110.85360, 120.63966     };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadSincDown1 = {
        160.00000,  138.94278,  138.94904,  138.93526,   138.82692,   138.93626,
        138.91203,  138.86617,  138.94406,  138.88925,   138.90580,   138.93444,
        138.88587,  138.94829,  138.86294,  138.90247,   138.96607,   138.92467,
        138.87909,  139.00977,  138.98683,  138.94268,   138.94472,   138.93787,
        138.96868,  139.01499,  138.84918,  139.02813,   138.96332,   138.88298,
        138.96159,  139.03214,  138.95589,  138.97182,   138.78805,   138.79253,
        138.91090,  138.85241,  136.57009,    6.0209100,   5.9431680,   9.8069955,
         22.024451,  46.313985,  66.060119,  85.051558,   91.228431,   88.775633,
         90.284568,  85.926092, -INFINITY,  -INFINITY     };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadSincDown2 = {
        128.76242,  132.04750,  131.92380,  131.86005,   131.67534,  131.55245,
        131.20737,  130.70376,  130.03135,  129.08289,   127.98096,  126.79067,
        125.01781,  123.57465,  121.84289,  120.02417,   118.25536,  116.45328,
        114.73674,  113.04134,  111.54974,  110.05367,   108.18163,  106.19899,
        104.18558,  102.29820,  100.19539,   98.302868,   96.279814,  94.212402,
         92.284728,  90.702953,  88.194570,  86.473244,   86.257589,  86.050746,
         85.831642,  85.417157,  84.879789,   6.0212869,   5.9437058,  9.8078011,
         22.026403,  46.319603,  66.094843,  85.407696,   90.817897,  90.169624,
        -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY     };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadSincMicro = {
         88.988685,  92.000979,  92.003641,   92.004130,   92.008494,  92.012907,
         92.021241,  92.034311,  92.055562,   92.092006,   92.146234,  92.223460,
         92.385667,  92.584517,  92.937835,   93.532492,   94.503879,  96.257847,
         99.576189, 106.67421,  101.35888,    94.706216,   91.390190,  92.145062,
         98.565646,  90.301702,  92.595062,   87.213007,   87.999280,  85.787849,
         80.329459,  76.134898,  74.969840,   75.913880,   49.400425,  44.095717,
         50.769155,  23.713435,   6.1741306,   3.0128109, -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,   -INFINITY,   -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,   -INFINITY,   -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadSincUp1 = {
         88.989013,   92.001506,     92.003789,  92.005359,  92.009287,  92.014338,
         92.024739,   92.040198,     92.065898,  92.108939,  92.173287,  92.265262,
         92.457558,   92.694946,     93.118365,  93.835848,  95.023947,  97.223204,
        101.58780,   106.99058,      98.288291,  93.152582,  91.160304,  93.974547,
         96.174690,   89.221429,     93.525879,  87.749931,  84.539966,  82.209238,
         83.988193,   78.889572,     63.366043,  51.199426,  29.186838,  19.439013,
         12.226592,    0.020195688, -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,    -INFINITY,    -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,    -INFINITY,    -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadSincUp2 = {
         88.931683, 88.945019,     88.951241,  88.957641,  88.970971,  88.987795,
         89.023302, 89.076953,     89.164234,  89.314311,  89.539625,  89.867246,
         90.574332, 91.490434,     93.276805,  96.953690, 108.10133,  100.19615,
         92.123609, 88.866323,     89.904911, 107.42293,   88.653256,  94.128118,
         86.840615, 90.064180,     88.079455,  80.904034,  84.836196,  75.734021,
         49.454554,  0.044531786, -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,    -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,    -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,    -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs> AudioResult::kPrevSinadSincUp3 = {
         88.989013,  92.038818,  92.064803,  92.089907,  92.143298,  92.210339,
         92.353547,  92.570912,  92.931860,  93.569959,  94.575451,  96.154324,
        100.18881,  106.45290,  101.31335,   94.449670,  91.441993,  92.165018,
         98.614619,  89.985872,  92.530313,  87.251558,  86.964321,  85.827088,
         80.321554,  81.931405,  75.049416,  49.392045, -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY    };
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
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhasePointDown0 = {
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhasePointDown1 = {
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhasePointDown2 = {
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00001,   0.00001,   0.00001,   0.00001,   0.00001,   0.00002,
         0.00002,   0.00003,   0.00003,   0.00004,   0.00004,   0.00004,
         0.00005,   0.00005,   0.00005    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhasePointMicro = {
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhasePointUp1 = {
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,  -INFINITY   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhasePointUp2 = {
         0.00000,   0.00086,   0.00110,   0.00129,   0.00163,   0.00197,
         0.00254,   0.00321,   0.00407,   0.00523,   0.00657,   0.00810,
         0.01059,   0.01299,   0.01635,   0.02066,   0.02593,   0.03274,
         0.04118,   0.05230,   0.06534,   0.08164,   0.10465,   0.13082,
         0.16361,   0.20618,   0.26178,   0.32727,   0.41240,   0.52352,
         0.65439,   0.78525,  -INFINITY, -INFINITY, -INFINITY, -INFINITY,
        -INFINITY, -INFINITY, -INFINITY   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhasePointUp3 = {
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,  -INFINITY, -INFINITY,
        -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
        -INFINITY, -INFINITY, -INFINITY   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseLinearUnity = {
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseLinearDown0 = {
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00001,
        -0.00002,  -0.00002,  -0.00003,  -0.00003,  -0.00003,  -0.00004,
        -0.00004,  -0.00004,  -0.00004    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseLinearDown1 = {
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseLinearDown2 = {
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00001,  -0.00001,
        -0.00001,  -0.00001,  -0.00001,  -0.00002,  -0.00002,  -0.00003,
        -0.00003,  -0.00004,  -0.00005,  -0.00006,  -0.00007,  -0.00007,
        -0.00007,  -0.00007,  -0.00008    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseLinearMicro = {
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00001,
        -0.00002,  -0.00002,  -0.00003,  -0.00003,  -0.00004,  -0.00006,
        -0.00007,  -0.00008,  -0.00011,  -0.00014,  -0.00014,  -0.00014,
        -0.00014,  -0.00015,  -0.00016    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseLinearUp1 = {
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00002,
        -0.00002,  -0.00002,  -0.00003,  -0.00004,  -0.00005,  -0.00006,
        -0.00008,  -0.00009,  -0.00012,  -0.00015,  -0.00015,  -0.00016,
        -0.00016,  -0.00017,  -INFINITY   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseLinearUp2 = {
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,  -INFINITY, -INFINITY, -INFINITY, -INFINITY,
        -INFINITY, -INFINITY, -INFINITY   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseLinearUp3 = {
         0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00001,
        -0.00002,  -0.00002,  -0.00003,  -0.00003,  -0.00004,  -0.00006,
        -0.00007,  -0.00009,  -0.00011,  -0.00014,  -INFINITY, -INFINITY,
        -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
        -INFINITY, -INFINITY, -INFINITY   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseSincUnity = {
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseSincDown0 = {
         0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000, -0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000, -0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000, -0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000, -0.00000,
        -0.00000,  -0.00001,  -0.00001,  -0.00001,  -0.00001, -0.00001,
        -0.00002,  -0.00002,  -0.00003,  -0.00003,  -0.00003, -0.00004,
        -0.00004,  -0.00004,  -0.00004    };


const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseSincDown1 = {
         0.00000,   0.00000,  -0.00000,  -0.00000,   0.00000,  -0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00000,   0.00000,  -0.00000,   0.00000,   0.00000,
        -0.00000,   0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,   0.00000,   0.00000    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseSincDown2 = {
         0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00001,  -0.00001,
        -0.00001,  -0.00001,  -0.00001,  -0.00002,  -0.00002,  -0.00003,
        -0.00003,  -0.00004,  -0.00005,  -0.00006,  -0.00007,  -0.00007,
        -0.00007,  -0.00007,  -0.00008    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseSincMicro = {
         0.00000,   0.00000,   0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00001,
        -0.00002,  -0.00002,  -0.00003,  -0.00003,  -0.00004,  -0.00006,
        -0.00007,  -0.00008,  -0.00011,  -0.00014,  -0.00014,  -0.00014,
        -0.00015,  -0.00015,  -0.00016    };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseSincUp1 = {
         0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00002,
        -0.00002,  -0.00002,  -0.00003,  -0.00004,  -0.00005,  -0.00006,
        -0.00008,  -0.00009,  -0.00012,  -0.00015,  -0.00015,  -0.00016,
        -0.00016,  -0.00017,  -INFINITY   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseSincUp2 = {
         0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,   0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,  -0.00000,
        -0.00000,  -0.00000,   0.00000,  -0.00000,   0.00000,   0.00000,
         0.00000,   0.00000,   0.00000,   0.00000,   0.00000,  -0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,   0.00000,   0.00000,
        -0.00000,  -0.00000,  -INFINITY, -INFINITY, -INFINITY, -INFINITY,
        -INFINITY, -INFINITY, -INFINITY   };

const std::array<double, FrequencySet::kFirstOutBandRefFreqIdx> AudioResult::kPrevPhaseSincUp3 = {
         0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,  -0.00000,
        -0.00000,  -0.00001,  -0.00001,  -0.00001,  -0.00001,  -0.00001,
        -0.00002,  -0.00002,  -0.00003,  -0.00003,  -0.00004,  -0.00006,
        -0.00007,  -0.00009,  -0.00011,  -0.00014,  -INFINITY, -INFINITY,
        -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
        -INFINITY, -INFINITY, -INFINITY   };
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

  DumpFreqRespValueSet(AudioResult::FreqRespPointUnity.data(), "FR-PointUnity");
  DumpFreqRespValueSet(AudioResult::FreqRespPointDown0.data(), "FR-PointDown0");
  DumpFreqRespValueSet(AudioResult::FreqRespPointDown1.data(), "FR-PointDown1");
  DumpFreqRespValueSet(AudioResult::FreqRespPointDown2.data(), "FR-PointDown2");
  DumpFreqRespValueSet(AudioResult::FreqRespPointMicro.data(), "FR-PointMicro");
  DumpFreqRespValueSet(AudioResult::FreqRespPointUp1.data(), "FR-PointUp1");
  DumpFreqRespValueSet(AudioResult::FreqRespPointUp2.data(), "FR-PointUp2");
  DumpFreqRespValueSet(AudioResult::FreqRespPointUp3.data(), "FR-PointUp3");

  DumpFreqRespValueSet(AudioResult::FreqRespLinearUnity.data(), "FR-LinearUnity");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearDown0.data(), "FR-LinearDown0");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearDown1.data(), "FR-LinearDown1");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearDown2.data(), "FR-LinearDown2");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearMicro.data(), "FR-LinearMicro");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearUp1.data(), "FR-LinearUp1");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearUp2.data(), "FR-LinearUp2");
  DumpFreqRespValueSet(AudioResult::FreqRespLinearUp3.data(), "FR-LinearUp3");

  DumpFreqRespValueSet(AudioResult::FreqRespSincUnity.data(), "FR-SincUnity");
  DumpFreqRespValueSet(AudioResult::FreqRespSincDown0.data(), "FR-SincDown0");
  DumpFreqRespValueSet(AudioResult::FreqRespSincDown1.data(), "FR-SincDown1");
  DumpFreqRespValueSet(AudioResult::FreqRespSincDown2.data(), "FR-SincDown2");
  DumpFreqRespValueSet(AudioResult::FreqRespSincMicro.data(), "FR-SincMicro");
  DumpFreqRespValueSet(AudioResult::FreqRespSincUp1.data(), "FR-SincUp1");
  DumpFreqRespValueSet(AudioResult::FreqRespSincUp2.data(), "FR-SincUp2");
  DumpFreqRespValueSet(AudioResult::FreqRespSincUp3.data(), "FR-SincUp3");

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

  DumpPhaseValueSet(AudioResult::PhasePointUnity.data(), "Phase-PointUnity");
  DumpPhaseValueSet(AudioResult::PhasePointDown0.data(), "Phase-PointDown0");
  DumpPhaseValueSet(AudioResult::PhasePointDown1.data(), "Phase-PointDown1");
  DumpPhaseValueSet(AudioResult::PhasePointDown2.data(), "Phase-PointDown2");
  DumpPhaseValueSet(AudioResult::PhasePointMicro.data(), "Phase-PointMicro");
  DumpPhaseValueSet(AudioResult::PhasePointUp1.data(), "Phase-PointUp1");
  DumpPhaseValueSet(AudioResult::PhasePointUp2.data(), "Phase-PointUp2");
  DumpPhaseValueSet(AudioResult::PhasePointUp3.data(), "Phase-PointUp3");

  DumpPhaseValueSet(AudioResult::PhaseLinearUnity.data(), "Phase-LinearUnity");
  DumpPhaseValueSet(AudioResult::PhaseLinearDown0.data(), "Phase-LinearDown0");
  DumpPhaseValueSet(AudioResult::PhaseLinearDown1.data(), "Phase-LinearDown1");
  DumpPhaseValueSet(AudioResult::PhaseLinearDown2.data(), "Phase-LinearDown2");
  DumpPhaseValueSet(AudioResult::PhaseLinearMicro.data(), "Phase-LinearMicro");
  DumpPhaseValueSet(AudioResult::PhaseLinearUp1.data(), "Phase-LinearUp1");
  DumpPhaseValueSet(AudioResult::PhaseLinearUp2.data(), "Phase-LinearUp2");
  DumpPhaseValueSet(AudioResult::PhaseLinearUp3.data(), "Phase-LinearUp3");

  DumpPhaseValueSet(AudioResult::PhaseSincUnity.data(), "Phase-SincUnity");
  DumpPhaseValueSet(AudioResult::PhaseSincDown0.data(), "Phase-SincDown0");
  DumpPhaseValueSet(AudioResult::PhaseSincDown1.data(), "Phase-SincDown1");
  DumpPhaseValueSet(AudioResult::PhaseSincDown2.data(), "Phase-SincDown2");
  DumpPhaseValueSet(AudioResult::PhaseSincMicro.data(), "Phase-SincMicro");
  DumpPhaseValueSet(AudioResult::PhaseSincUp1.data(), "Phase-SincUp1");
  DumpPhaseValueSet(AudioResult::PhaseSincUp2.data(), "Phase-SincUp2");
  DumpPhaseValueSet(AudioResult::PhaseSincUp3.data(), "Phase-SincUp3");

  DumpPhaseValueSet(AudioResult::PhasePointNxN.data(), "Phase-PointNxN");
  DumpPhaseValueSet(AudioResult::PhaseLinearNxN.data(), "Phase-LinearNxN");
}

// Display a single frequency response results array, for import and processing.
void AudioResult::DumpFreqRespValueSet(double* freq_resp_vals, const std::string& arr_name) {
  printf("\n   %s", arr_name.c_str());
  for (size_t freq = 0; freq < FrequencySet::kFirstOutBandRefFreqIdx; ++freq) {
    if (freq % 6 == 0) {
      printf("\n      ");
    }
    if (freq >= FrequencySet::kFirstInBandRefFreqIdx) {
      // For positive values, display 0 as threshold (we also bump up LevelTolerance, which limits
      // future values from being FURTHER above 0 dB)
      printf(" %14.7le,", (freq_resp_vals[freq] < 0.0f) ? freq_resp_vals[freq] : 0.0f);
    } else {
      printf("                ");
    }
  }
  printf("\n");
}

// Display a single sinad results array, for import and processing.
void AudioResult::DumpSinadValueSet(double* sinad_vals, const std::string& arr_name) {
  printf("\n   %s", arr_name.c_str());
  for (size_t freq = 0; freq < FrequencySet::kNumReferenceFreqs; ++freq) {
    if (freq % 6 == 0) {
      printf("\n     ");
    }
    printf("   %11.7lf,",
           ((isinf(sinad_vals[freq]) && sinad_vals[freq] > 0.0) ? 160.0 : sinad_vals[freq]));
  }
  printf("\n");
}

// Display a single phase results array, for import and processing.
void AudioResult::DumpPhaseValueSet(double* phase_vals, const std::string& arr_name) {
  printf("\n   %s", arr_name.c_str());
  for (size_t freq = 0; freq < FrequencySet::kFirstOutBandRefFreqIdx; ++freq) {
    if (freq % 6 == 0) {
      printf("\n       ");
    }
    if (freq >= FrequencySet::kFirstInBandRefFreqIdx) {
      printf(" %9.6lf,", phase_vals[freq]);
    } else {
      printf("           ");
    }
  }
  printf("\n");
}

void AudioResult::DumpLevelValues() {
  printf("\n\n Level (in dB)");
  printf("\n       8-bit:   Source %15.8le  Mix %15.8le  Output %15.8le", AudioResult::LevelSource8,
         AudioResult::LevelMix8, AudioResult::LevelOutput8);

  printf("\n       16-bit:  Source %15.8le  Mix %15.8le  Output %15.8le",
         AudioResult::LevelSource16, AudioResult::LevelMix16, AudioResult::LevelOutput16);

  printf("\n       24-bit:  Source %15.8le  Mix %15.8le  Output %15.8le",
         AudioResult::LevelSource24, AudioResult::LevelMix24, AudioResult::LevelOutput24);

  printf("\n       Float:   Source %15.8le  Mix %15.8le  Output %15.8le",
         AudioResult::LevelSourceFloat, AudioResult::LevelMixFloat, AudioResult::LevelOutputFloat);
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
         AudioResult::LevelToleranceSourceFloat, AudioResult::LevelToleranceMixFloat,
         AudioResult::LevelToleranceOutputFloat);

  printf("\n       Stereo-to-Mono: %15.8le               ", AudioResult::LevelToleranceStereoMono);
  printf("Interpolation: %15.8le", LevelToleranceInterpolation);

  printf("\n");
}

void AudioResult::DumpNoiseFloorValues() {
  printf("\n\n Noise Floor (in dB)");
  printf("\n       8-bit:   Source %11.7lf  Mix %11.7lf  Output %11.7lf", AudioResult::FloorSource8,
         AudioResult::FloorMix8, AudioResult::FloorOutput8);
  printf("\n       16-bit:  Source %11.7lf  Mix %11.7lf  Output %11.7lf",
         AudioResult::FloorSource16, AudioResult::FloorMix16, AudioResult::FloorOutput16);
  printf("\n       24-bit:  Source %11.7lf  Mix %11.7lf  Output %11.7lf",
         AudioResult::FloorSource24, AudioResult::FloorMix24, AudioResult::FloorOutput24);
  printf("\n       Float:   Source %11.7lf  Mix %11.7lf  Output %11.7lf",
         AudioResult::FloorSourceFloat, AudioResult::FloorMixFloat, AudioResult::FloorOutputFloat);
  printf("\n       Stereo-to-Mono: %11.7lf", AudioResult::FloorStereoMono);

  printf("\n");
}

void AudioResult::DumpDynamicRangeValues() {
  printf("\n\n Dynamic Range");
  printf("\n       Epsilon:  %10.7e  (%13.6le dB)", AudioResult::ScaleEpsilon,
         Gain::ScaleToDb(1.0 - AudioResult::ScaleEpsilon));
  printf("  Level: %15.8le dB  Sinad: %10.6lf dB", AudioResult::LevelEpsilonDown,
         AudioResult::SinadEpsilonDown);

  printf("\n       -30 dB down:                                ");
  printf("  Level: %15.8lf dB  Sinad: %10.6lf dB", AudioResult::Level30Down,
         AudioResult::Sinad30Down);

  printf("\n       -60 dB down:                                ");
  printf("  Level: %15.8lf dB  Sinad: %10.6lf dB", AudioResult::Level60Down,
         AudioResult::Sinad60Down);

  printf("\n       -90 dB down:                                ");
  printf("  Level: %15.8lf dB  Sinad: %10.6lf dB", AudioResult::Level90Down,
         AudioResult::Sinad90Down);

  printf("\n       Gain Accuracy:   +/- %12.7le dB", AudioResult::DynRangeTolerance);

  printf("\n       MinScale: %10.8f  (%11.8f dB)", AudioResult::MinScaleNonZero,
         Gain::ScaleToDb(AudioResult::MinScaleNonZero));

  printf("\n");
}

}  // namespace media::audio::test
