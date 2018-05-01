// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/test/audio_result.h"

namespace media {
namespace audio {
namespace test {

// See audio_result.h for in-depth descriptions of these class members/consts.

//
//
// Input
//
constexpr double AudioResult::kPrevLevelToleranceSource8;
constexpr double AudioResult::kPrevLevelToleranceSource16;
constexpr double AudioResult::kPrevLevelToleranceSourceFloat;

double AudioResult::FloorSource8 = -INFINITY;
double AudioResult::FloorSource16 = -INFINITY;
double AudioResult::FloorSourceFloat = -INFINITY;

constexpr double AudioResult::kPrevFloorSource8;
constexpr double AudioResult::kPrevFloorSource16;
constexpr double AudioResult::kPrevFloorSourceFloat;

//
//
// Rechannel
//
constexpr double AudioResult::kPrevLevelToleranceStereoMono;
constexpr double AudioResult::kPrevLevelStereoMono;

double AudioResult::FloorStereoMono = -INFINITY;
constexpr double AudioResult::kPrevFloorStereoMono;

//
//
// Interpolate
//
constexpr double AudioResult::kPrevLevelToleranceInterpolation;

std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespPointUnity = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespPointDown1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespPointDown2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespPointUp1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespPointUp2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespPointMicro = {NAN};

std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearUnity = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearDown1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearDown2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearUp1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearUp2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearMicro = {NAN};

// clang-format off
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointUnity = {
         0.0000000e+00, -5.1349009e-07, -1.7111923e-07, -1.7111923e-07, -5.1349009e-07, -1.7111923e-07,
        -1.7111923e-07, -1.7111923e-07, -1.7111923e-07, -1.7111922e-07, -1.7111924e-07, -1.7111923e-07,
        -1.7111922e-07, -1.7111918e-07, -1.7111916e-07, -1.7111916e-07, -1.7111902e-07, -1.7111901e-07,
        -1.7111893e-07, -1.7111888e-07, -1.7111880e-07, -1.7111866e-07, -1.7111825e-07, -1.7111803e-07,
        -1.7111782e-07, -1.7111740e-07, -1.7111698e-07, -1.7111620e-07, -1.7111697e-07, -1.7111587e-07,
        -1.7111484e-07, -1.7111364e-07, -1.7111551e-07, -1.7111493e-07, -1.7111438e-07, -1.7111419e-07,
        -1.7111378e-07, -1.7111335e-07, -1.7111245e-07,  0.0000000e+00, -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointDown1 = {
         0.0000000e+00, -5.1349009e-07, -1.7111923e-07, -1.7111923e-07, -5.1349009e-07, -1.7111923e-07,
        -1.7111923e-07, -1.7111923e-07, -1.7111923e-07, -1.7111922e-07, -1.7111924e-07, -1.7111923e-07,
        -1.7111922e-07, -1.7111918e-07, -1.7111916e-07, -1.7111916e-07, -1.7111902e-07, -1.7111901e-07,
        -1.7111893e-07, -1.7111888e-07, -1.7111880e-07, -1.7111866e-07, -1.7111825e-07, -1.7111803e-07,
        -1.7111782e-07, -1.7111740e-07, -1.7111698e-07, -1.7111620e-07, -1.7111697e-07, -1.7111587e-07,
        -1.7111484e-07, -1.7111364e-07, -1.7111551e-07, -1.7111493e-07, -1.7111438e-07, -1.7111419e-07,
        -1.7111378e-07, -1.7111335e-07, -1.7111245e-07,  0.0000000e+00, -1.7111259e-07, -1.7111495e-07,
        -1.7111587e-07, -1.7111698e-07, -1.7111704e-07, -1.7111901e-07, -1.7111924e-08   };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointDown2 = {
         0.0000000e+00, -1.5661948e-06, -2.1304830e-06, -4.1508399e-06, -1.0573127e-05, -3.1327856e-06,
        -3.7360669e-06, -1.3521252e-05, -8.9959366e-06, -1.4731346e-05, -2.0321179e-05, -3.1731982e-05,
        -5.2764227e-05, -8.4389531e-05, -1.5833520e-04, -1.8590343e-04, -3.2025470e-04, -4.6360090e-04,
        -7.3919056e-04, -2.6341508e-03, -2.0523154e-03, -2.8686004e-03, -5.3773101e-03, -7.9726975e-03,
        -1.1529201e-02, -1.8264152e-02, -2.9759909e-02, -4.5993385e-02, -7.3353496e-02, -1.1946812e-01,
        -1.8829853e-01, -2.6824053e-01, -4.7926478e-01, -7.1041615e-01, -7.5146118e-01, -8.0028563e-01,
        -8.3189558e-01, -9.1923047e-01, -1.0450937e+00, -5.6153011e+00, -1.1874883e+00, -1.9197369e+00,
        -3.2716331e+00, -3.9317734e+00, -3.9530068e+00, -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointUp1 = {
         0.0000000e+00, -2.3075525e-06, -3.2001429e-06, -6.4414709e-06, -1.3938824e-05, -8.2058602e-06,
        -1.2004771e-05, -2.6854693e-05, -2.9915955e-05, -4.9498439e-05, -7.5621557e-05, -1.1651375e-04,
        -1.9729576e-04, -3.0063578e-04, -5.0162881e-04, -7.3492998e-04, -1.1847081e-03, -1.8418967e-03,
        -2.9203791e-03, -6.1792698e-03, -7.4153809e-03, -1.1445013e-02, -1.9475496e-02, -2.9997351e-02,
        -4.6002219e-02, -7.3055332e-02, -1.1804769e-01, -1.8448649e-01, -2.9385422e-01, -4.7618537e-01,
        -7.5031773e-01, -1.0864645e+00, -1.9729293e+00, -3.0035901e+00, -3.1751831e+00, -3.3423374e+00,
        -3.5326071e+00, -3.9292783e+00, -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointUp2 = {
         0.0000000e+00, -4.1304954e-06, -5.7928341e-06, -7.7888059e-06, -1.2433739e-05, -1.7289636e-05,
        -2.8546931e-05, -4.5313133e-05, -7.2618137e-05, -1.1908468e-04, -1.8782672e-04, -2.8555123e-04,
        -4.8794867e-04, -7.3346458e-04, -1.1610326e-03, -1.8545131e-03, -2.9217547e-03, -4.6568377e-03,
        -7.3665403e-03, -1.1884764e-02, -1.8553984e-02, -2.8976383e-02, -4.7646455e-02, -7.4537857e-02,
        -1.1677390e-01, -1.8593683e-01, -3.0108777e-01, -4.7368893e-01, -7.6056394e-01, -1.2489873e+00,
        -2.0099221e+00, -3.0090516e+00, -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointMicro = {
         0.0000000e+00, -2.3612645e-06, -1.3927273e-05, -1.8642120e-05, -4.2228237e-06, -1.1962984e-05,
        -2.0634587e-05, -2.3007928e-04, -3.8848628e-05, -6.0444576e-05, -6.2051777e-05, -3.3715148e-04,
        -2.4175056e-04, -2.9308358e-04, -3.9206194e-04, -6.5556293e-04, -1.1589970e-03, -1.5518015e-03,
        -2.7006828e-03, -4.3368956e-03, -6.4373030e-03, -1.0294539e-02, -1.6969501e-02, -2.6374645e-02,
        -3.9940477e-02, -6.4007959e-02, -1.0382682e-01, -1.6320430e-01, -2.6133692e-01, -4.2225203e-01,
        -6.6278381e-01, -9.5995141e-01, -1.7360425e+00, -2.6258454e+00, -2.7733570e+00, -2.9193846e+00,
        -3.0841866e+00, -3.4208931e+00, -3.8745303e+00, -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearUnity = {
         0.0000000e+00, -5.1349009e-07, -1.7111923e-07, -1.7111923e-07, -5.1349009e-07, -1.7111923e-07,
        -1.7111923e-07, -1.7111923e-07, -1.7111923e-07, -1.7111922e-07, -1.7111924e-07, -1.7111923e-07,
        -1.7111922e-07, -1.7111918e-07, -1.7111916e-07, -1.7111916e-07, -1.7111902e-07, -1.7111901e-07,
        -1.7111893e-07, -1.7111888e-07, -1.7111880e-07, -1.7111866e-07, -1.7111825e-07, -1.7111803e-07,
        -1.7111782e-07, -1.7111740e-07, -1.7111698e-07, -1.7111620e-07, -1.7111697e-07, -1.7111587e-07,
        -1.7111484e-07, -1.7111364e-07, -1.7111551e-07, -1.7111493e-07, -1.7111438e-07, -1.7111419e-07,
        -1.7111378e-07, -1.7111335e-07, -1.7111245e-07,  0.0000000e+00, -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearDown1 = {
         0.0000000e+00, -5.1349009e-07, -1.7111923e-07, -1.7111923e-07, -5.1349009e-07, -1.7111923e-07,
        -1.7111923e-07, -1.7111923e-07, -1.7111923e-07, -1.7111922e-07, -1.7111924e-07, -1.7111923e-07,
        -1.7111922e-07, -1.7111918e-07, -1.7111916e-07, -1.7111916e-07, -1.7111902e-07, -1.7111901e-07,
        -1.7111893e-07, -1.7111888e-07, -1.7111880e-07, -1.7111866e-07, -1.7111825e-07, -1.7111803e-07,
        -1.7111782e-07, -1.7111740e-07, -1.7111698e-07, -1.7111620e-07, -1.7111697e-07, -1.7111587e-07,
        -1.7111484e-07, -1.7111364e-07, -1.7111551e-07, -1.7111493e-07, -1.7111438e-07, -1.7111419e-07,
        -1.7111378e-07, -1.7111335e-07, -1.7111245e-07,  0.0000000e+00, -1.7111259e-07, -1.7111495e-07,
        -1.7111587e-07, -1.7111698e-07, -1.7111704e-07, -1.7111901e-07, -1.7111924e-07   };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearDown2 = {
         0.0000000e+00, -1.1542722e-06, -1.4072450e-06, -2.4587243e-06, -3.4090434e-06, -5.4440652e-06,
        -6.1476093e-06, -9.2342191e-06, -1.5710112e-05, -2.4453233e-05, -3.7277633e-05, -5.7571858e-05,
        -9.7519893e-05, -1.4563539e-04, -2.3024045e-04, -3.6709771e-04, -5.7884007e-04, -9.2262815e-04,
        -1.4598839e-03, -2.3551408e-03, -3.6760487e-03, -5.7397382e-03, -9.4320657e-03, -1.4741397e-02,
        -2.3060146e-02, -3.6596630e-02, -5.9063518e-02, -9.2341897e-02, -1.4672583e-01, -2.3669268e-01,
        -3.7038524e-01, -5.3434407e-01, -9.5451450e-01, -1.4259657e+00, -1.5015095e+00, -1.5788025e+00,
        -1.6584801e+00, -1.8317158e+00, -2.0687016e+00, -2.2070709e+00, -2.3697851e+00, -3.8270585e+00,
        -6.3603520e+00, -7.8362922e+00, -7.8756305e+00, -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearUp1 = {
         0.0000000e+00, -3.7128363e-06, -5.1600182e-06, -6.4492483e-06, -1.0397929e-05, -1.5090537e-05,
        -2.2908902e-05, -3.5883489e-05, -5.8330688e-05, -9.4345028e-05, -1.4893645e-04, -2.2633894e-04,
        -3.8578929e-04, -5.8004940e-04, -9.1793307e-04, -1.4652632e-03, -2.3088631e-03, -3.6805640e-03,
        -5.8223139e-03, -9.3925881e-03, -1.4660489e-02, -2.2890054e-02, -3.7618852e-02, -5.8804727e-02,
        -9.2012448e-02, -1.4618240e-01, -2.3596102e-01, -3.6933699e-01, -5.8797335e-01, -9.5150485e-01,
        -1.4961090e+00, -2.1714101e+00, -3.9417612e+00, -6.0037294e+00, -6.3422275e+00, -6.6913233e+00,
        -7.0535523e+00, -7.8511827e+00, -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearUp2 = {
         0.0000000e+00, -8.7311165e-07, -4.9519936e-06, -8.9110096e-06, -1.7573716e-05, -2.7910958e-05,
        -5.0481225e-05, -8.4058937e-05, -1.3865244e-04, -2.3144475e-04, -3.6899251e-04, -5.6457206e-04,
        -9.6912705e-04, -1.4603634e-03, -2.3153749e-03, -3.7024434e-03, -5.8369291e-03, -9.3069629e-03,
        -1.4726474e-02, -2.3762847e-02, -3.7101303e-02, -5.7946089e-02, -9.5286206e-02, -1.4906893e-01,
        -2.3354088e-01, -3.7186675e-01, -6.0216865e-01, -9.4737056e-01, -1.5211202e+00, -2.4979661e+00,
        -4.0198343e+00, -6.0180912e+00, -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearMicro = {
         0.0000000e+00, -2.2294509e-06, -3.4930562e-06, -4.8749562e-06, -7.4787165e-06, -1.1397148e-05,
        -1.8925543e-05, -3.0434620e-05, -4.7744450e-05, -8.1041148e-05, -1.2769346e-04, -1.9481191e-04,
        -3.3341719e-04, -5.0162696e-04, -7.9464478e-04, -1.2698768e-03, -2.0004111e-03, -3.1887139e-03,
        -5.0442906e-03, -8.1376035e-03, -1.2701520e-02, -1.9830443e-02, -3.2589023e-02, -5.0940216e-02,
        -7.9700349e-02, -1.2663432e-01, -2.0432751e-01, -3.1973653e-01, -5.0878121e-01, -8.2272839e-01,
        -1.2921429e+00, -1.8726496e+00, -3.3860101e+00, -5.1321097e+00, -5.4167922e+00, -5.7100048e+00,
        -6.0136635e+00, -6.6797938e+00, -7.6059453e+00, -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY        };
// clang-format on

std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadPointUnity = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadPointDown1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadPointDown2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadPointUp1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadPointUp2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadPointMicro = {NAN};

std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearUnity = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearDown1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearDown2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearUp1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearUp2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearMicro = {NAN};

// clang-format off
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointUnity = {
        98.104753,  98.092846,  98.104753,  98.104753,  98.092846,  98.104753,
        98.104753,  98.104753,  98.104753,  98.104753,  98.104753,  98.104753,
        98.104753,  98.104753,  98.104753,  98.104753,  98.104753,  98.104753,
        98.104753,  98.104753,  98.104753,  98.104753,  98.104753,  98.104753,
        98.104753,  98.104753,  98.104753,  98.104753,  98.104753,  98.104753,
        98.104753,  98.104753,  98.104753,  98.104753,  98.104753,  98.104753,
        98.104753,  98.104753,  98.104753,  98.104753, -INFINITY,  -INFINITY,
       -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointDown1 = {
        98.104753,      98.092846,      98.104753,      98.104753,      98.092846,      98.104753,
        98.104753,      98.104753,      98.104753,      98.104753,      98.104753,      98.104753,
        98.104753,      98.104753,      98.104753,      98.104753,      98.104753,      98.104753,
        98.104753,      98.104753,      98.104753,      98.104753,      98.104753,      98.104753,
        98.104753,      98.104753,      98.104753,      98.104753,      98.104753,      98.104753,
        98.104753,      98.104753,      98.104753,      98.104753,      98.104753,      98.104753,
        98.104753,      98.104753,      98.104753,      98.104753,     -67.190481e-11, -67.187492e-11,
       -67.185563e-11, -67.184599e-11, -67.185852e-11, -67.184695e-11, -67.184599e-11   };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointDown2 = {
        98.104753,  71.299175,  69.169300,  67.776657,     65.775634,  64.148982,
        61.919120,  59.883222,  57.816272,  55.655925,     53.670273,  51.847077,
        49.516704,  47.746730,  45.749804,  43.715323,     41.740475,  39.716312,
        37.724270,  35.646655,  33.720153,  31.778712,     29.620609,  27.679775,
        25.735560,  23.722886,  21.641339,  19.694034,     17.669284,  15.567729,
        13.589258,  11.956639,   9.3302950,  7.4756053,     7.2269554,  6.9698739,
         6.7513475,  6.2743148,  5.6707363, -1.1195153e-1, -1.1888207, -1.9203807,
        -3.2637490, -3.9317789, -3.9530074, -INFINITY,     -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointUp1 = {
        98.104753,   65.306973,  63.177736,  61.784831,     59.782842,      58.156537,
        55.926699,   53.890733,  51.823894,  49.663638,     47.677911,      45.854528,
        43.524301,   41.753095,  39.756939,  37.722366,     35.747628,      33.722923,
        31.730650,   29.653165,  27.719950,  25.781160,     23.619992,      21.674609,
        19.722759,   17.697746,  15.596001,  13.618049,     11.542905,       9.3592177,
         7.2546706,   5.4621243,  2.4029801, 14.632180e-03, -3.2357044e-01, -64.047954e-02,
        -0.98829195, -1.6770528, -INFINITY,  -INFINITY,     -INFINITY,      -INFINITY,
        -INFINITY,   -INFINITY,  -INFINITY,  -INFINITY,     -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointUp2 = {
        98.104753,     61.280235,     59.151486,  57.758918,     55.756759,     54.130738,
        51.900960,     49.865009,     47.798133,  45.637967,     43.652049,     41.828666,
        39.498424,     37.726722,     35.730737,  33.695818,     31.720707,     29.695082,
        27.701826,     25.622181,     23.684311,  21.742982,     19.573739,     17.616782,
        15.645886,     13.590901,     11.439506,  93.839187e-01, 71.806586e-01, 47.728152e-01,
        23.024022e-01, 24.982464e-04, -INFINITY,  -INFINITY,     -INFINITY,     -INFINITY,
       -INFINITY,      -INFINITY,     -INFINITY,  -INFINITY,     -INFINITY,     -INFINITY,
       -INFINITY,      -INFINITY,     -INFINITY,  -INFINITY,     -INFINITY       };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointMicro = {
        98.104753,      65.821448,      63.687800,  62.297201,     60.467865,      58.665160,
        56.436804,      54.478922,      52.334176,  50.170301,     48.232618,      46.399711,
        44.038785,      42.286157,      40.269771,  38.238472,     36.262554,      34.243990,
        32.244618,      30.169613,      28.236187,  26.297498,     24.137589,      22.192993,
        20.241607,      18.219773,      16.121433,  14.147449,     12.078366,       9.9109028,
         7.8285500,      6.0649103,      3.0854212,  8.0408940e-01, 4.8670494e-01,  1.8161314e-01,
        -1.4590283e-01, -7.8569200e-01, -1.5854277, -INFINITY,     -INFINITY,      -INFINITY,
        -INFINITY,      -INFINITY,      -INFINITY,  -INFINITY,     -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearUnity = {
        98.104753,  98.092846,  98.104753,  98.104753,  98.092846,  98.104753,
        98.104753,  98.104753,  98.104753,  98.104753,  98.104753,  98.104753,
        98.104753,  98.104753,  98.104753,  98.104753,  98.104753,  98.104753,
        98.104753,  98.104753,  98.104753,  98.104753,  98.104753,  98.104753,
        98.104753,  98.104753,  98.104753,  98.104753,  98.104753,  98.104753,
        98.104753,  98.104753,  98.104753,  98.104753,  98.104753,  98.104753,
        98.104753,  98.104753,  98.104753,  98.104753,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearDown1 = {
        98.104753,      98.092846,      98.104753,      98.104753,      98.092846,      98.104753,
        98.104753,      98.104753,      98.104753,      98.104753,      98.104753,      98.104753,
        98.104753,      98.104753,      98.104753,      98.104753,      98.104753,      98.104753,
        98.104753,      98.104753,      98.104753,      98.104753,      98.104753,      98.104753,
        98.104753,      98.104753,      98.104753,      98.104753,      98.104753,      98.104753,
        98.104753,      98.104753,      98.104753,      98.104753,      98.104753,      98.104753,
        98.104753,      98.104753,      98.104753,      98.104753,     -67.190481e-11, -67.187492e-11,
       -67.185563e-11, -67.184599e-11, -67.185852e-11, -67.184695e-11, -67.184599e-11   };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearDown2 = {
        98.104753,  91.778201,  89.708983,  88.319889,  86.339508,      84.727432,
        82.505015,  80.475544,  78.408218,  76.251503,  74.268564,      72.442839,
        70.112279,  68.342138,  66.345204,  64.309762,  62.332627,      60.303995,
        58.306453,  56.219738,  54.273059,  52.313727,  50.115917,      48.118094,
        46.084713,  43.905792,  41.624128,  39.355940,  36.845869,      34.023637,
        31.110184,  28.494272,  23.861015,  20.256505,  19.767490,      19.284213,
        18.806580,  17.823134,  16.580759,  14.376645,  -1.3052335e-01, -4.4388097e-01,
        -1.7706740, -3.0652297, -3.1044190, -INFINITY,  -INFINITY        };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearUp1 = {
        98.104753,  91.773822,      89.710987,  88.324831,  86.339658,  84.730293,
        82.501584,  80.473162,      78.405319,  76.245716,  74.262880,  72.431747,
        70.096930,  68.316773,      66.305770,  64.247515,  62.235217,  60.150296,
        58.065689,  55.837860,      53.691521,  51.439385,  48.765049,  46.168574,
        43.348207,  40.163256,      36.623257,  33.045510,  29.080262,  24.691499,
        20.261678,  16.344725,       9.4006715,  3.8204101,  3.0397858,  2.2633626,
         1.4860378, -1.3828181e-01, -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,      -INFINITY,  -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearUp2 = {
         98.104753,     96.792502,     97.064182,  97.076402,  97.101973,  97.277858,
         97.222941,     96.662384,     94.903830,  91.456542,  87.420809,  83.733385,
         79.044123,     75.483861,     71.481244,  67.403795,  63.449142,  59.395142,
         55.406740,     51.246306,     47.369864,  43.486762,  39.147967,  35.233882,
         31.291983,     27.181934,     22.879094,  18.767894,  14.361357,  95.456587e-01,
         46.048270e-01, 50.176275e-04, -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,      -INFINITY,     -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,      -INFINITY,     -INFINITY,  -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearMicro = {
        98.104753,   78.650554,  76.521477,      75.130360,  73.128046,  71.502217,
        69.2732561,  67.237875,  65.170760,      63.010737,  61.024343,  59.201359,
        56.8710541,  55.099169,  53.103006,      51.067655,  49.091923,  47.065355,
        45.0706215,  42.988410,  41.046844,      39.099634,  36.919932,  34.947863,
        32.9530867,  30.858770,  28.641354,      26.485990,  24.116335,  21.430081,
        18.5572992,  15.816889,  10.428496,       5.6901385,  5.0075941,  4.325998,
         3.64079697,  2.2022842,  3.3042234e-01, -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,   -INFINITY,  -INFINITY,      -INFINITY,  -INFINITY,   };
// clang-format on

//
//
// Scale
//
constexpr double AudioResult::kPrevGainToleranceMultiplier;

constexpr uint32_t AudioResult::kPrevScaleEpsilon;
constexpr uint32_t AudioResult::kPrevMinScaleNonZero;

constexpr double AudioResult::kPrevDynRangeTolerance;

double AudioResult::LevelEpsilonDown = -INFINITY;
constexpr double AudioResult::kPrevLevelEpsilonDown;

double AudioResult::SinadEpsilonDown = -INFINITY;
constexpr double AudioResult::kPrevSinadEpsilonDown;

double AudioResult::Level60Down = -INFINITY;

double AudioResult::Sinad60Down = -INFINITY;
constexpr double AudioResult::kPrevSinad60Down;

//
//
// Sum
//
constexpr double AudioResult::kPrevLevelToleranceMix8;
constexpr double AudioResult::kPrevLevelToleranceMix16;
constexpr double AudioResult::kPrevLevelToleranceMixFloat;

double AudioResult::LevelMix8 = -INFINITY;
double AudioResult::LevelMix16 = -INFINITY;
double AudioResult::LevelMixFloat = -INFINITY;

double AudioResult::FloorMix8 = -INFINITY;
double AudioResult::FloorMix16 = -INFINITY;
double AudioResult::FloorMixFloat = -INFINITY;

constexpr double AudioResult::kPrevFloorMix8;
constexpr double AudioResult::kPrevFloorMix16;
constexpr double AudioResult::kPrevFloorMixFloat;

//
//
// Output
//
constexpr double AudioResult::kPrevLevelToleranceOutput8;
constexpr double AudioResult::kPrevLevelToleranceOutput16;
constexpr double AudioResult::kPrevLevelToleranceOutputFloat;

double AudioResult::FloorOutput8 = -INFINITY;
double AudioResult::FloorOutput16 = -INFINITY;
double AudioResult::FloorOutputFloat = -INFINITY;

constexpr double AudioResult::kPrevFloorOutput8;
constexpr double AudioResult::kPrevFloorOutput16;
constexpr double AudioResult::kPrevFloorOutputFloat;

}  // namespace test
}  // namespace audio
}  // namespace media
