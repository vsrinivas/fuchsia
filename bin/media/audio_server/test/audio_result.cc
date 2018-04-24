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
    AudioResult::FreqRespLinearUnity = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearDown1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearDown2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearUp1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::FreqRespLinearUp2 = {NAN};

// clang-format off
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointUnity = {
          0.0,         -51.349009e-8, -17.111923e-8, -17.111923e-8, -51.349009e-8, -17.111923e-8,
        -17.111923e-8, -17.111923e-8, -17.111923e-8, -17.111922e-8, -17.111924e-8, -17.111923e-8,
        -17.111922e-8, -17.111918e-8, -17.111916e-8, -17.111916e-8, -17.111902e-8, -17.111901e-8,
        -17.111893e-8, -17.111888e-8, -17.111880e-8, -17.111866e-8, -17.111825e-8, -17.111803e-8,
        -17.111782e-8, -17.111740e-8, -17.111698e-8, -17.111620e-8, -17.111697e-8, -17.111587e-8,
        -17.111484e-8, -17.111364e-8, -17.111551e-8, -17.111493e-8, -17.111438e-8, -17.111419e-8,
        -17.111378e-8, -17.111335e-8, -17.111245e-8,   0.0,         -INFINITY,     -INFINITY,
        -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY       };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointDown1 = {
         0.0,          -51.349009e-8, -17.111923e-8, -17.111923e-8, -51.349009e-8, -17.111923e-8,
        -17.111923e-8, -17.111923e-8, -17.111923e-8, -17.111922e-8, -17.111924e-8, -17.111923e-8,
        -17.111922e-8, -17.111918e-8, -17.111916e-8, -17.111916e-8, -17.111902e-8, -17.111901e-8,
        -17.111893e-8, -17.111888e-8, -17.111880e-8, -17.111866e-8, -17.111825e-8, -17.111803e-8,
        -17.111782e-8, -17.111740e-8, -17.111698e-8, -17.111620e-8, -17.111697e-8, -17.111587e-8,
        -17.111484e-8, -17.111364e-8, -17.111551e-8, -17.111493e-8, -17.111438e-8, -17.111419e-8,
        -17.111378e-8, -17.111335e-8, -17.111245e-8,  0.0,          -17.111259e-8, -17.111495e-8,
        -17.111587e-8, -17.111698e-8, -17.111704e-8, -17.111901e-8, -17.111924e-8   };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointDown2 = {
         0.0,          -15.661948e-7, -21.304830e-7, -41.508399e-7, -23.143243e-7, -31.327856e-7,
        -37.360669e-7, -54.763505e-7, -89.959366e-7, -14.731346e-6, -20.321179e-6, -31.731982e-6,
        -52.764227e-6, -75.010333e-6, -11.669687e-5, -18.422096e-5, -32.025470e-5, -46.360090e-5,
        -73.919056e-5, -26.341508e-4, -18.324744e-4, -28.623225e-4, -53.773101e-4, -79.726975e-4,
        -11.529201e-3, -18.264152e-3, -29.403342e-3, -45.993385e-3, -73.353496e-3, -11.787925e-2,
        -18.829853e-2, -26.586876e-2, -47.550260e-2, -70.996558e-2, -74.762418e-2, -78.798215e-2,
        -82.556167e-2, -91.611536e-2, -10.304170e-1, -49.421652e-1, -11.800062e-1, -19.051857e-1,
        -32.568268e-1, -39.035274e-1, -39.357721e-1, -INFINITY,     -INFINITY       };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointUp1 = {
         0.0,          -23.075525e-7, -32.001429e-7, -64.414709e-7, -54.977887e-7, -82.058602e-7,
        -12.004771e-6, -18.551996e-6, -29.915955e-6, -49.498439e-6, -75.621557e-6, -11.651375e-5,
        -19.729576e-5, -29.205186e-5, -45.999485e-5, -73.347275e-5, -11.847081e-4, -18.418967e-4,
        -29.203791e-4, -61.792698e-4, -73.249332e-4, -11.436950e-3, -19.475496e-3, -29.997351e-3,
        -46.002219e-3, -73.055332e-3, -11.785222e-2, -18.448649e-2, -29.379399e-2, -47.528470e-2,
        -74.972919e-2, -10.844994e-1, -19.689696e-1, -30.035901e-1, -31.680521e-1, -33.423374e-1,
        -35.235648e-1, -39.218944e-1, -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,
        -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY       };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointUp2 = {
          0.0,         -41.304954e-7, -57.928341e-7, -77.888059e-7, -12.433739e-6, -17.289636e-6,
        -28.546931e-6, -45.313133e-6, -72.618137e-6, -11.908468e-5, -18.782672e-5, -28.555123e-5,
        -48.794867e-5, -73.346458e-5, -11.610326e-4, -18.545131e-4, -29.217547e-4, -46.568377e-4,
        -73.665403e-4, -11.884764e-3, -18.553984e-3, -28.976383e-3, -47.646455e-3, -74.537857e-3,
        -11.677390e-2, -18.593683e-2, -30.108777e-2, -47.368893e-2, -76.056394e-2, -12.489873e-1,
        -20.099221e-1, -30.090516e-1, -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,
        -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,
        -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY       };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearUnity = {
          0.0,         -51.349009e-8, -17.111923e-8, -17.111923e-8, -51.349009e-8, -17.111923e-8,
        -17.111923e-8, -17.111923e-8, -17.111923e-8, -17.111922e-8, -17.111924e-8, -17.111923e-8,
        -17.111922e-8, -17.111918e-8, -17.111916e-8, -17.111916e-8, -17.111902e-8, -17.111901e-8,
        -17.111893e-8, -17.111888e-8, -17.111880e-8, -17.111866e-8, -17.111825e-8, -17.111803e-8,
        -17.111782e-8, -17.111740e-8, -17.111698e-8, -17.111620e-8, -17.111697e-8, -17.111587e-8,
        -17.111484e-8, -17.111364e-8, -17.111551e-8, -17.111493e-8, -17.111438e-8, -17.111419e-8,
        -17.111378e-8, -17.111335e-8, -17.111245e-8,   0.0,         -INFINITY,     -INFINITY,
        -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY       };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearDown1 = {
         0.0,          -51.349009e-8, -17.111923e-8, -17.111923e-8, -51.349009e-8, -17.111923e-8,
        -17.111923e-8, -17.111923e-8, -17.111923e-8, -17.111922e-8, -17.111924e-8, -17.111923e-8,
        -17.111922e-8, -17.111918e-8, -17.111916e-8, -17.111916e-8, -17.111902e-8, -17.111901e-8,
        -17.111893e-8, -17.111888e-8, -17.111880e-8, -17.111866e-8, -17.111825e-8, -17.111803e-8,
        -17.111782e-8, -17.111740e-8, -17.111698e-8, -17.111620e-8, -17.111697e-8, -17.111587e-8,
        -17.111484e-8, -17.111364e-8, -17.111551e-8, -17.111493e-8, -17.111438e-8, -17.111419e-8,
        -17.111378e-8, -17.111335e-8, -17.111245e-8,   0.0,         -17.111259e-8, -17.111495e-8,
        -17.111587e-8, -17.111698e-8, -17.111704e-8, -17.111901e-8, -17.111924e-8  };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearDown2 = {
          0.0,         -11.542722e-7, -14.072450e-7, -24.587243e-7, -34.090434e-7, -54.440652e-7,
        -61.476093e-7, -92.342191e-7, -15.710112e-6, -24.453233e-6, -37.277633e-6, -57.571858e-6,
        -97.519893e-6, -14.563539e-5, -23.024045e-5, -36.696565e-5, -57.776051e-5, -92.029637e-5,
        -14.553278e-4, -23.468061e-4, -36.612787e-4, -57.175425e-4, -93.953657e-4, -14.681902e-3,
        -22.966376e-3, -36.467601e-3, -58.822561e-3, -91.964280e-3, -14.613021e-2, -23.572457e-2,
        -36.887028e-2, -53.216962e-2, -95.063947e-2, -14.202253e-1, -14.954564e-1, -15.724382e-1,
        -16.519393e-1, -18.243049e-1, -20.604262e-1, -21.722133e-1, -23.603234e-1, -38.119463e-1,
        -63.360953e-1, -78.069950e-1, -78.461341e-1, -INFINITY,     -INFINITY       };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearUp1 = {
          0.0,         -37.128363e-7, -51.600182e-7, -64.492483e-7, -10.397929e-6, -15.090537e-6,
        -22.908902e-6, -35.883489e-6, -58.330688e-6, -94.345028e-6, -14.893645e-5, -22.633894e-5,
        -38.578929e-5, -58.004940e-5, -91.793307e-5, -14.649262e-4, -23.083072e-4, -36.779611e-4,
        -58.176287e-4, -93.835675e-4, -14.646031e-3, -22.867505e-3, -37.581932e-3, -58.745616e-3,
        -91.918607e-3, -14.605357e-2, -23.571991e-2, -36.895920e-2, -58.737760e-2, -95.053595e-2,
        -14.945946e-1, -21.692354e-1, -39.378873e-1, -59.979895e-1, -63.361726e-1, -66.849585e-1,
        -70.470128e-1, -78.437725e-1, -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,
        -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY       };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearUp2 = {
          0.0,         -87.311165e-8, -49.519936e-7, -89.110096e-7, -17.573716e-6, -27.9109574e-6,
        -50.481225e-6, -84.058937e-6, -13.865244e-5, -23.144475e-5, -36.899251e-5, -56.4572058e-5,
        -96.912705e-5, -14.603634e-4, -23.153749e-4, -37.024434e-4, -58.369291e-4, -93.0696283e-4,
        -14.726474e-3, -23.762847e-3, -37.101303e-3, -57.946089e-3, -95.286206e-3, -14.906893e-2,
        -23.354088e-2, -37.186675e-2, -60.216865e-2, -94.737056e-2, -15.211202e-1, -24.979661e-1,
        -40.198343e-1, -60.180912e-1, -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,
        -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,
        -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY       };
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
    AudioResult::SinadLinearUnity = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearDown1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearDown2 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearUp1 = {NAN};
std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::SinadLinearUp2 = {NAN};

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
        98.104753,     71.323906,     69.200300,     67.807032,     65.807375,     64.182137,
        61.952756,     59.917570,     57.851088,     55.690602,     53.705218,     51.881944,
        49.552020,     47.780457,     45.784817,     43.750229,     41.775831,     39.751259,
        37.759612,     35.681897,     33.750683,     31.813971,     29.654918,     27.714338,
        25.771321,     23.759419,     21.676958,     19.729111,     17.703849,     15.605547,
        13.618331,     11.993291,     93.682681e-1,  75.058529e-1,  72.618152e-1,  70.190888e-1,
        67.905283e-1,  63.046501e-1,  57.219424e-1,  96.646842e-2, -11.800093e-1, -19.053166e-1,
       -32.481103e-1, -39.035258e-1, -39.357713e-1, -INFINITY,     -INFINITY       };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointUp1 = {
        98.104753,     65.311939,     63.185424,     61.791873,     59.790805,     58.164827,
        55.935265,     53.899380,     51.832596,     49.672353,     47.686440,     45.863204,
        43.533167,     41.761634,     39.765780,     37.731120,     35.756445,     33.731668,
        31.739518,     29.659373,     27.727273,     25.790053,     23.627470,     21.682218,
        19.731317,     17.706412,     15.605282,     13.625923,     11.551374,     93.687396e-1,
        72.590092e-1,  54.719074e-1,  24.139626e-1,  14.632180e-3, -31.002134e-2, -64.047954e-2,
       -97.224124e-2, -16.646409e-1, -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,
       -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY       };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointUp2 = {
        98.104753,    61.280235,     59.151486,  57.758918,    55.756759,     54.130738,
        51.900960,    49.865009,     47.798133,  45.637967,    43.652049,     41.828666,
        39.498424,    37.726722,     35.730737,  33.695818,    31.720707,     29.695082,
        27.701826,    25.622181,     23.684311,  21.742982,    19.573739,     17.616782,
        15.645886,    13.590901,     11.439506,  93.839187e-1, 71.806586e-1,  47.728152e-1,
        23.024022e-1, 24.982464e-4, -INFINITY,  -INFINITY,    -INFINITY,     -INFINITY,
       -INFINITY,    -INFINITY,     -INFINITY,  -INFINITY,    -INFINITY,     -INFINITY,
       -INFINITY,    -INFINITY,     -INFINITY,  -INFINITY,    -INFINITY       };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearUnity = {
        98.104753,  98.092846,  98.104753,  98.104753,  98.092846,  98.104753,
        98.104753,  98.104753,  98.104753,  98.104753,  98.104753,  98.104753,
        98.104753,  98.104753,  98.104753,  98.104753,  98.104753,  98.104753,
        98.104753,  98.104753,  98.104753,  98.104753,  98.104753,  98.104753,
        98.104753,  98.104753,  98.104753,  98.104753,  98.104753,  98.104753,
        98.104753,  98.104753,  98.104753,  98.104753,  98.104753,  98.104753,
        98.104753,  98.104753,  98.104753,  98.104753, -INFINITY,  -INFINITY,
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
        98.104753,     95.289561,     94.862400,     94.525141,  93.940073,     93.316011,
        92.154949,     90.848747,     89.341663,     87.537958,  85.871250,     84.181411,
        81.962571,     80.242180,     78.265091,     76.229679,  74.240583,     72.169550,
        70.093247,     67.872158,     65.725775,     63.482846,  60.815363,     58.233046,
        55.437198,     52.131914,     48.843826,     45.397926,  41.652676,     37.620818,
        33.707307,     30.399366,     24.927414,     20.920955,  20.389503,     19.866953,
        19.355317,     18.300283,     16.983910,     14.388789, -12.106299e-2, -42.889467e-2,
       -17.464495e-1, -30.359045e-1, -30.749809e-1, -INFINITY,  -INFINITY       };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearUp1 = {
         98.104753,     95.266603,     94.883150,     94.529677,     93.927523,     93.298935,
         92.163956,     90.831482,     89.304226,     87.503074,     85.775126,     84.037384,
         81.704768,     79.878197,     77.693324,     75.359685,     72.935502,     70.254823,
         67.396415,     64.170431,     60.950253,     57.550937,     53.584608,     49.896485,
         46.111569,     42.093635,     37.908852,     33.884876,     29.596748,     24.987392,
         20.426909,     16.441892,     94.384135e-1,  38.400882e-1,  30.580566e-1,  22.804526e-1,
         15.022125e-1, -12.381482e-2, -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,
        -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY,     -INFINITY       };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearUp2 = {
         98.104753,    96.792502,     97.064182,  97.076402,  97.101973,  97.277858,
         97.222941,    96.662384,     94.903830,  91.456542,  87.420809,  83.733385,
         79.044123,    75.483861,     71.481244,  67.403795,  63.449142,  59.395142,
         55.406740,    51.246306,     47.369864,  43.486762,  39.147967,  35.233882,
         31.291983,    27.181934,     22.879094,  18.767894,  14.361357,  95.456587e-1,
         46.048270e-1, 50.176275e-4, -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,    -INFINITY,     -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,    -INFINITY,     -INFINITY,  -INFINITY,  -INFINITY    };
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
