// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/test/audio_result.h"

namespace media {
namespace audio {
namespace test {

// See audio_result.h for in-depth descriptions of these class members/consts.

//
// Input
//
constexpr double AudioResult::kLevelToleranceSource8;
constexpr double AudioResult::kLevelToleranceSource16;
constexpr double AudioResult::kLevelToleranceSourceFloat;

double AudioResult::FloorSource8 = -INFINITY;
double AudioResult::FloorSource16 = -INFINITY;
double AudioResult::FloorSourceFloat = -INFINITY;

constexpr double AudioResult::kPrevFloorSource8;
constexpr double AudioResult::kPrevFloorSource16;
constexpr double AudioResult::kPrevFloorSourceFloat;

//
// Rechannel
//
constexpr double AudioResult::kPrevStereoMonoTolerance;
constexpr double AudioResult::kPrevLevelStereoMono;

double AudioResult::FloorStereoMono = -INFINITY;
constexpr double AudioResult::kPrevFloorStereoMono;

//
// Interpolate
//
constexpr double AudioResult::kLevelToleranceInterpolation;

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
         0.0,              -0.00000051349009, -0.00000017111923, -0.00000017111923, -0.00000051349009, -0.00000017111923,
        -0.00000017111923, -0.00000017111923, -0.00000017111923, -0.00000017111922, -0.00000017111924, -0.00000017111923,
        -0.00000017111922, -0.00000017111918, -0.00000017111916, -0.00000017111916, -0.00000017111902, -0.00000017111901,
        -0.00000017111893, -0.00000017111888, -0.00000017111880, -0.00000017111866, -0.00000017111825, -0.00000017111803,
        -0.00000017111782, -0.00000017111740, -0.00000017111698, -0.00000017111620, -0.00000017111697, -0.00000017111587,
        -0.00000017111484, -0.00000017111364, -0.00000017111551, -0.00000017111493, -0.00000017111438, -0.00000017111419,
        -0.00000017111378, -0.00000017111335, -0.00000017111245,  0.0,              -INFINITY,         -INFINITY,
        -INFINITY,         -INFINITY,         -INFINITY,         -INFINITY,         -INFINITY           };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointDown1 = {
         0.0,              -0.00000051349009, -0.00000017111923, -0.00000017111923, -0.00000051349009, -0.00000017111923,
        -0.00000017111923, -0.00000017111923, -0.00000017111923, -0.00000017111922, -0.00000017111924, -0.00000017111923,
        -0.00000017111922, -0.00000017111918, -0.00000017111916, -0.00000017111916, -0.00000017111902, -0.00000017111901,
        -0.00000017111893, -0.00000017111888, -0.00000017111880, -0.00000017111866, -0.00000017111825, -0.00000017111803,
        -0.00000017111782, -0.00000017111740, -0.00000017111698, -0.00000017111620, -0.00000017111697, -0.00000017111587,
        -0.00000017111484, -0.00000017111364, -0.00000017111551, -0.00000017111493, -0.00000017111438, -0.00000017111419,
        -0.00000017111378, -0.00000017111335, -0.00000017111245,  0.0,              -0.00000017111259, -0.00000017111495,
        -0.00000017111587, -0.00000017111698, -0.00000017111704, -0.00000017111901, -0.00000017111924   };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointDown2 = {
         0.0,             -0.0000015661948, -0.0000021304830, -0.0000041508399, -0.0000023143243, -0.0000031327856,
        -0.0000037360669, -0.0000054763505, -0.0000089959366, -0.000014731346,  -0.000020321179,  -0.000031731982,
        -0.000052764227,  -0.000075010333,  -0.00011669687,   -0.00018422096,   -0.00032025470,   -0.00046360090,
        -0.00073919056,   -0.0026341508,    -0.0018324744,    -0.0028623225,    -0.0053773101,    -0.0079726975,
        -0.011529201,     -0.018264152,     -0.029403342,     -0.045993385,     -0.073353496,     -0.11787925,
        -0.18829853,      -0.26586876,      -0.47550260,      -0.70996558,      -0.74762418,      -0.78798215,
        -0.82556167,      -0.91611536,      -1.0304170,       -4.9421652,       -1.1800062,       -1.9051853,
        -3.2568267,       -3.9035274,       -3.9357721,       -INFINITY,        -INFINITY          };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointUp1 = {
         0.0,             -0.0000023075525, -0.0000032001429, -0.0000064414709,  -0.0000054977887, -0.0000082058602,
        -0.000012004771,  -0.000018551996,  -0.000029915955,  -0.000049498439,   -0.000075621557,  -0.00011651375,
        -0.00019729576,   -0.00029205186,   -0.00045999485,   -0.00073347275,    -0.0011847081,    -0.0018418967,
        -0.0029203791,    -0.0061792698,    -0.0073249332,    -0.011436950,      -0.019475496,     -0.029997351,
        -0.046002219,     -0.073055332,     -0.11785222,      -0.18448649,       -0.29379399,      -0.47528470,
        -0.74972919,      -1.0844994,       -1.9689696,       -3.0035901,        -3.1680521,       -3.3423374,
        -3.5235648,       -3.9218944,       -INFINITY,        -INFINITY,         -INFINITY,        -INFINITY,
        -INFINITY,        -INFINITY,        -INFINITY,        -INFINITY,         -INFINITY          };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointUp2 = {
         0.0,            -0.0000041304954, -0.0000057928341, -0.0000077888059, -0.000012433739, -0.00001728964,
        -0.000028546931, -0.000045313133,  -0.00007261814,   -0.00011908468,   -0.00018782672,  -0.00028555123,
        -0.00048794867,  -0.00073346458,   -0.0011610326,    -0.0018545131,    -0.0029217547,   -0.0046568377,
        -0.0073665403,   -0.011884764,     -0.018553984,     -0.028976383,     -0.047646455,    -0.074537857,
        -0.1167739,      -0.18593683,      -0.30108777,      -0.47368893,      -0.76056394,     -1.2489873,
        -2.0099221,      -3.0090516,       -INFINITY,        -INFINITY,        -INFINITY,       -INFINITY,
        -INFINITY,       -INFINITY,        -INFINITY,        -INFINITY,        -INFINITY,       -INFINITY,
        -INFINITY,       -INFINITY,        -INFINITY,        -INFINITY,        -INFINITY         };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearUnity = {
         0.0,              -0.00000051349009, -0.00000017111923, -0.00000017111923, -0.00000051349009, -0.00000017111923,
        -0.00000017111923, -0.00000017111923, -0.00000017111923, -0.00000017111922, -0.00000017111924, -0.00000017111923,
        -0.00000017111922, -0.00000017111918, -0.00000017111916, -0.00000017111916, -0.00000017111902, -0.00000017111901,
        -0.00000017111893, -0.00000017111888, -0.00000017111880, -0.00000017111866, -0.00000017111825, -0.00000017111803,
        -0.00000017111782, -0.00000017111740, -0.00000017111698, -0.00000017111620, -0.00000017111697, -0.00000017111587,
        -0.00000017111484, -0.00000017111364, -0.00000017111551, -0.00000017111493, -0.00000017111438, -0.00000017111419,
        -0.00000017111378, -0.00000017111335, -0.00000017111245,  0.0,              -INFINITY,         -INFINITY,
        -INFINITY,         -INFINITY,         -INFINITY,         -INFINITY,         -INFINITY           };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearDown1 = {
         0.0,              -0.00000051349009, -0.00000017111923, -0.00000017111923, -0.00000051349009, -0.00000017111923,
        -0.00000017111923, -0.00000017111923, -0.00000017111923, -0.00000017111922, -0.00000017111924, -0.00000017111923,
        -0.00000017111922, -0.00000017111918, -0.00000017111916, -0.00000017111916, -0.00000017111902, -0.00000017111901,
        -0.00000017111893, -0.00000017111888, -0.00000017111880, -0.00000017111866, -0.00000017111825, -0.00000017111803,
        -0.00000017111782, -0.00000017111740, -0.00000017111698, -0.00000017111620, -0.00000017111697, -0.00000017111587,
        -0.00000017111484, -0.00000017111364, -0.00000017111551, -0.00000017111493, -0.00000017111438, -0.00000017111419,
        -0.00000017111378, -0.00000017111335, -0.00000017111245,  0.0,              -0.00000017111259, -0.00000017111495,
        -0.00000017111587, -0.00000017111698, -0.00000017111704, -0.00000017111901, -0.00000017111924   };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearDown2 = {
         0.0,             -0.0000011542722,  -0.0000014072450, -0.0000024587243,  -0.0000034090434, -0.0000054440652,
        -0.0000061476093, -0.0000092342191,  -0.000015710112,  -0.000024453233,   -0.000037277633,  -0.000057571858,
        -0.000097519893,  -0.00014563539,    -0.00023024045,   -0.00036696565,    -0.00057776051,   -0.00092029637,
        -0.0014553278,    -0.0023468061,     -0.0036612787,    -0.0057175425,     -0.0093953657,    -0.014681188,
        -0.022966376,     -0.036467601,      -0.058822561,     -0.091963973,      -0.14613021,      -0.23572457,
        -0.36887028,      -0.53216962,       -0.95063947,      -1.4202253,        -1.4954564,       -1.5724378,
        -1.6519393,       -1.8243049,        -2.0604254,       -2.1722133,        -2.3603218,       -3.8119463,
        -6.3360953,       -7.8069950,        -7.8461322,       -INFINITY,         -INFINITY          };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearUp1 = {
         0.0,            -0.0000037128363, -0.0000051600182, -0.0000064492483, -0.000010397929,  -0.000015090537,
        -0.000022908902, -0.000035702193,  -0.000058330688,  -0.000094345028,  -0.00014893645,   -0.00022633894,
        -0.00038578929,  -0.00058004940,   -0.00091793307,   -0.0014649262,    -0.0023083072,    -0.0036779611,
        -0.0058176287,   -0.0093835675,    -0.014646031,     -0.022867505,     -0.037581932,     -0.058745616,
        -0.091918607,    -0.14605357,      -0.23571991,      -0.36895916,      -0.58737760,      -0.95053595,
        -1.4945945,      -2.1692351,       -3.9378873,       -5.9979895,       -6.3361722,       -6.6849578,
        -7.0470128,      -7.8437725,       -INFINITY,        -INFINITY,        -INFINITY,        -INFINITY,
        -INFINITY,       -INFINITY,        -INFINITY,        -INFINITY,        -INFINITY          };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearUp2 = {
         0.0,             0.0,             0.0,            0.0,            0.0,            0.0,
        -0.000014502297, -0.000047254454, -0.00010311277, -0.00019493732, -0.00033284117, -0.00052846866,
        -0.00093294826,  -0.0014245213,   -0.0022791782,  -0.0036668623,  -0.0058008273,  -0.0092710816,
        -0.014689911,    -0.023726651,    -0.037064259,   -0.057908872,   -0.095249010,   -0.14903174,
        -0.23350410,     -0.37182886,     -0.60212938,    -0.94733064,    -1.5210772,     -2.4979184,
        -4.0197772,      -6.0180152,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,       -INFINITY,       -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,       -INFINITY,       -INFINITY,      -INFINITY,      -INFINITY        };
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

// These thresholds changed as a result of adjusting our resampler tests to call
// Mixer objects in the same way that their primary callers in audio_server do.
// clang-format off
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointUnity = {
        98.104753, 98.092846, 98.104753, 98.104753, 98.092846, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, -INFINITY, -INFINITY,
        -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY   };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointDown1 = {
        98.104753, 98.092846, 98.104753, 98.104753, 98.092846, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, -0.00000000067190481, -0.00000000067187492,
        -0.00000000067185563, -0.00000000067184599, -0.00000000067185852, -0.00000000067184695, -0.00000000067184599 };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointDown2 = {
        98.104753,  71.323906,  69.200300,  67.807032,   65.807375,  64.182137,
        61.952756,  59.917570,  57.851088,  55.690602,   53.705218,  51.881944,
        49.552020,  47.780457,  45.784880,  43.750229,   41.775831,  39.751259,
        37.759612,  35.681900,  33.750683,  31.813971,   29.654934,  27.714346,
        25.771321,  23.759419,  21.676958,  19.729111,   17.703849,  15.605547,
        13.618333,  11.993291,   9.3682738,  7.5058529,   7.2618160,  7.0190895,
         6.7905283,  6.3046508,  5.7219424,  0.96646878, -1.1800092, -1.9053161,
        -3.2481088, -3.9035256, -3.9357708, -INFINITY,   -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointUp1 = {
        98.104753,   65.311939,  63.185424,  61.791873,    59.790805,   58.164827,
        55.935265,   53.899380,  51.832596,  49.672353,    47.686440,   45.863204,
        43.533175,   41.761634,  39.765790,  37.731120,    35.756445,   33.731668,
        31.739518,   29.659384,  27.727273,  25.790053,    23.627479,   21.682220,
        19.731317,   17.706412,  15.605282,  13.625923,    11.551374,    9.3687417,
         7.2590096,   5.4719074,  2.4139626,  0.014632316, -0.31002084, -0.64047954,
        -0.97224124, -1.6646409, -INFINITY,  -INFINITY,    -INFINITY,   -INFINITY,
        -INFINITY,   -INFINITY,  -INFINITY,  -INFINITY,    -INFINITY     };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointUp2 = {
        98.104753,  61.280235,     59.151486, 57.758918,  55.756759,  54.130738,
        51.900960,  49.865009,     47.798133, 45.637967,  43.652049,  41.828666,
        39.498424,  37.726722,     35.730737, 33.695818,  31.720707,  29.695082,
        27.701826,  25.622181,     23.684311, 21.742982,  19.573739,  17.616782,
        15.645886,  13.590901,     11.439506,  9.3839187,  7.1806586,  4.7728152,
         2.3024022,  0.0024982464, -INFINITY, -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,     -INFINITY, -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,     -INFINITY, -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearUnity = {
        98.104753, 98.092846, 98.104753, 98.104753, 98.092846, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, -INFINITY, -INFINITY,
        -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY   };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearDown1 = {
        98.104753, 98.092846, 98.104753, 98.104753, 98.092846, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, 98.104753, 98.104753,
        98.104753, 98.104753, 98.104753, 98.104753, -0.00000000067190481, -0.00000000067187492,
        -0.00000000067185563, -0.00000000067184599, -0.00000000067185852, -0.00000000067184695, -0.00000000067184599 };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearDown2 = {
        98.104753,  95.289561,  94.862400,  94.525141, 93.940073,   93.316011,
        92.154949,  90.848747,  89.341663,  87.537958, 85.871250,   84.181411,
        81.962571,  80.242180,  78.265091,  76.229679, 74.240583,   72.169550,
        70.093247,  67.872158,  65.725775,  63.482846, 60.815363,   58.233046,
        55.437671,  52.131914,  48.843995,  45.398143, 41.652676,   37.620818,
        33.707357,  30.399366,  24.927414,  20.920955, 20.389515,   19.866953,
        19.355317,  18.3002836, 16.983910,  14.388789, -0.12106292, -0.42889467,
        -1.7464495, -3.0359045, -3.0749782, -INFINITY, -INFINITY     };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearUp1 = {
        98.104753,  95.266603,   94.883150,  94.529677,  93.927523,  93.298935,
        92.163956,  90.831482,   89.304226,  87.503074,  85.775126,  84.037384,
        81.704768,  79.878197,   77.693324,  75.359685,  72.935502,  70.254823,
        67.396415,  64.170431,   60.950253,  57.550937,  53.584723,  49.896485,
        46.111739,  42.093635,   37.908867,  33.884876,  29.596748,  24.987392,
        20.426910,  16.441892,    9.4384135,  3.8400882,  3.0580579,  2.2804542,
         1.5022130, -0.12381373, -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,   -INFINITY,  -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearUp2 = {
         98.104753, 96.792502,     97.064182, 97.076402, 97.101973, 97.277858,
         97.222941, 96.662384,     94.903830, 91.603820, 87.824016, 84.114373,
         79.314469, 75.675840,     71.610033, 67.485311, 63.502550, 59.428508,
         55.428268, 51.259762,     47.378702, 43.492472, 39.151449, 35.236064,
         31.293400, 27.182858,     22.879676, 18.768289, 14.361630,  9.5458529,
          4.6049836, 0.0051707793, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
         -INFINITY, -INFINITY,     -INFINITY, -INFINITY, -INFINITY, -INFINITY,
         -INFINITY, -INFINITY,     -INFINITY, -INFINITY, -INFINITY   };
// clang-format on

//
// Scale
//
constexpr double AudioResult::kGainToleranceMultiplier;

constexpr uint32_t AudioResult::kScaleEpsilon;
constexpr uint32_t AudioResult::kMinScaleNonZero;

constexpr double AudioResult::kPrevDynRangeTolerance;

double AudioResult::LevelEpsilonDown = -INFINITY;
constexpr double AudioResult::kPrevLevelEpsilonDown;

double AudioResult::SinadEpsilonDown = -INFINITY;
constexpr double AudioResult::kPrevSinadEpsilonDown;

double AudioResult::Level60Down = -INFINITY;

double AudioResult::Sinad60Down = -INFINITY;
constexpr double AudioResult::kPrevSinad60Down;

//
// Sum
//
constexpr double AudioResult::kLevelToleranceMix8;
constexpr double AudioResult::kLevelToleranceMix16;
constexpr double AudioResult::kLevelToleranceMixFloat;

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
// Output
//
constexpr double AudioResult::kLevelToleranceOutput8;
constexpr double AudioResult::kLevelToleranceOutput16;
constexpr double AudioResult::kLevelToleranceOutputFloat;

double AudioResult::FloorOutput8 = -INFINITY;
double AudioResult::FloorOutput16 = -INFINITY;
double AudioResult::FloorOutputFloat = -INFINITY;

constexpr double AudioResult::kPrevFloorOutput8;
constexpr double AudioResult::kPrevFloorOutput16;
constexpr double AudioResult::kPrevFloorOutputFloat;

}  // namespace test
}  // namespace audio
}  // namespace media
