// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/test/audio_result.h"

namespace media {
namespace audio {
namespace test {

// These audio measurements are measured by various test cases throughout the
// set. They are eventually displayed in a recap after all other tests complete.

//
// How close is a measured level to the reference dB level?  Val-being-checked
// must be within this distance (above OR below) from the reference dB level.
constexpr double AudioResult::kLevelToleranceSource8;
constexpr double AudioResult::kLevelToleranceOutput8;
constexpr double AudioResult::kLevelToleranceMix8;

constexpr double AudioResult::kLevelToleranceSource16;
constexpr double AudioResult::kLevelToleranceOutput16;
constexpr double AudioResult::kLevelToleranceInterp16;
constexpr double AudioResult::kLevelToleranceMix16;

// Note: our internal representation is still effectively int16_t (w/ headroom);
// this overshadows any precision gains from ingesting / emitting data in float.
// Until our accumulator is higher-precision, float/int16 metrics will be equal.
constexpr double AudioResult::kLevelToleranceSourceFloat;
constexpr double AudioResult::kLevelToleranceOutputFloat;
constexpr double AudioResult::kLevelToleranceMixFloat;

//
// Purely when calculating gain (in dB) from gain_scale (fixed-point int),
// derived values must be within this multiplier (above or below) of target.
constexpr double AudioResult::kGainToleranceMultiplier;

//
// What is our best-case noise floor in absence of rechannel/gain/SRC/mix. Val
// is root-sum-square of all other freqs besides the 1kHz reference, in dBr
// units (compared to magnitude of received reference). Using dBr (not dBFS)
// includes level attenuation, making this metric a good proxy of
// frequency-independent fidelity in our audio processing pipeline.
double AudioResult::FloorSource8 = -INFINITY;
double AudioResult::FloorMix8 = -INFINITY;
double AudioResult::FloorOutput8 = -INFINITY;

double AudioResult::FloorSource16 = -INFINITY;
double AudioResult::FloorMix16 = -INFINITY;
double AudioResult::FloorOutput16 = -INFINITY;

double AudioResult::FloorSourceFloat = -INFINITY;
double AudioResult::FloorMixFloat = -INFINITY;
double AudioResult::FloorOutputFloat = -INFINITY;

double AudioResult::FloorStereoMono = -INFINITY;

// Val-being-checked (in dBr to reference signal) must be >= this value.
constexpr double AudioResult::kPrevFloorSource8;
constexpr double AudioResult::kPrevFloorMix8;
constexpr double AudioResult::kPrevFloorOutput8;

constexpr double AudioResult::kPrevFloorSource16;
constexpr double AudioResult::kPrevFloorMix16;
constexpr double AudioResult::kPrevFloorOutput16;

constexpr double AudioResult::kPrevFloorSourceFloat;
constexpr double AudioResult::kPrevFloorMixFloat;
constexpr double AudioResult::kPrevFloorOutputFloat;

double AudioResult::LevelMix8 = -INFINITY;
double AudioResult::LevelMix16 = -INFINITY;
double AudioResult::LevelMixFloat = -INFINITY;

//
// Frequency Response
//
// What is our received level (in dBFS), when sending sinusoids through our
// mixers at certain resampling ratios. The PointSampler and LinearSampler
// objects are specifically targeted with resampling ratios that are highly
// representative of how the current system uses them. A more exhaustive set
// of ratios will be included in subsequent CL, for more in-depth testing
// outside of CQ.
//
// We test PointSampler at Unity (no SRC) and 2:1 (such as 96-to-48), and
// LinearSampler at 294:160 and 147:160 (e.g. 88.2-to-48 and 44.1-to-48).
//
// We perform frequency response tests at various frequencies (kSummaryFreqs[]
// from frequency_set.h), storing the result at each frequency. As with
// resampling ratios, subsequent CL contains a more exhaustive frequency set,
// for in-depth testing and diagnostics to be done outside CQ.

// These arrays hold Frequency Response results as measured during the test run.
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

// Val-being-checked (in dBFS) must be greater than or equal to this value.
//
// Note: with rates other than N:1 or 1:N, interpolating resamplers dampen high
// frequencies, as shown in previously-saved LinearSampler results.

// clang-format off
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointUnity = {
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,      -INFINITY, -INFINITY,
        -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY   };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointDown1 = {
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0        };

// These thresholds changed as a result of adjusting our resampler tests to call
// the Mixer objects in the same way that their primary callers in audio_server
// do. For reference, the previous values are (for the time being only) included
// in nearby comments, showing changes that vary widely by resampler, SRC ratio
// and frequency. These commented values will be removed soon, in upcoming CL.
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointDown2 = {
         0.0,             -0.0000015661948, -0.0000021304830, -0.0000041508399, -0.0000023143243, -0.0000031327856,
    //   0.0,              0.0,             -0.0000010765944, -0.0000023500457, -0.0000023204115, -0.0000024819551,
        -0.0000037360669, -0.0000054763505, -0.0000089959366, -0.000014731346,  -0.000020321179,  -0.000031731982,
    //  -0.0000094623437, -0.0000098927249, -0.000015352092,  -0.000024634046,  -0.000037785192,  -0.000055401688,
        -0.000052764227,  -0.000075010333,  -0.00011669687,   -0.00018422096,   -0.00032025470,   -0.00046360090,
    //  -0.000095210661,  -0.00014667886,   -0.00023037397,   -0.00036630747,   -0.00057694728,   -0.00091971656,
        -0.00073919056,   -0.0026341508,    -0.0018324744,    -0.0028623225,    -0.0053773101,    -0.0079726975,
    //  -0.0014540418,    -0.0023462583,    -0.0036597335,    -0.0057131220,    -0.0093914625,    -0.014676537,
        -0.011529201,     -0.018264152,     -0.029403342,     -0.045993385,     -0.073353496,     -0.11787925,
    //  -0.022958352,     -0.036462171,     -0.058796479,     -0.091924166,     -0.14606405,      -0.23562194,
        -0.18829853,      -0.26586876,      -0.47550260,      -0.70996558,      -0.74762418,      -0.78798215,
    //  -0.36871115,      -0.53193309,      -0.95022584,      -1.4196076,       -1.4948005,       -1.5718115,
        -0.82556167,      -0.91611536,      -1.0304170,       -4.9421652,       -1.1800062,       -1.9051851,
    //  -1.6511036,       -1.8235781,       -2.0597837,       -4.7395855,       -2.3593078,       -3.8104597,
        -3.2568267,       -3.9035274,       -3.9357721,       -INFINITY,        -INFINITY          };
    //  -6.3343783,       -7.8036133,       -7.8427753,       -INFINITY,        -INFINITY          };

// These thresholds changed as a result of adjusting our resampler tests to call
// the Mixer objects in the same way that their primary callers in audio_server
// do. For reference, the previous values are (for the time being only) included
// in nearby comments, showing changes that vary widely by resampler, SRC ratio
// and frequency. These commented values will be removed soon, in upcoming CL.
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointUp1 = {
         0.0,             -0.0000023075525, -0.0000032001429, -0.0000064414709,  -0.0000054977887, -0.0000082058602,
    //   0.0,             -0.0000023504212, -0.0000043544860, -0.0000058073382,  -0.000010595969,  -0.000013453493,
        -0.000012004771,  -0.000018551996,  -0.000029915955,  -0.000049498439,   -0.000075621557,  -0.00011651375,
    //  -0.000028152172,  -0.000035439283,  -0.000056947810,  -0.000094066218,   -0.00014871579,   -0.00022498862,
        -0.00019729576,   -0.00029205186,   -0.00045999485,   -0.00073347275,    -0.0011847081,    -0.0018418967,
    //  -0.00038578026,   -0.00057974394,   -0.00091727461,   -0.0014643869,     -0.0023078001,    -0.0036776787,
        -0.0029203791,    -0.0061792698,    -0.0073249332,    -0.011436950,      -0.019475496,     -0.029997351,
    //  -0.0058170869,    -0.0093836438,    -0.014647070,     -0.022867596,      -0.037581764,     -0.058746770,
        -0.046002219,     -0.073055332,     -0.11785222,      -0.18448649,       -0.29379399,      -0.47528470,
    //  -0.091922190,     -0.14606728,      -0.23572614,      -0.36897083,       -0.58739390,      -0.95056709,
        -0.74972919,      -1.0844994,       -1.9689696,       -3.0035901,        -3.1680521,       -3.3423374,
    //  -1.4946470,       -2.1693107,       -3.9380418,       -5.9982547,        -6.3364566,       -6.6853228,
        -3.5235644,       -3.9218944,       -INFINITY,        -INFINITY,         -INFINITY,        -INFINITY,
    //  -7.0472288,       -7.8442190,       -INFINITY,        -INFINITY,         -INFINITY,        -INFINITY,
        -INFINITY,        -INFINITY,        -INFINITY,        -INFINITY,         -INFINITY          };
    //  -INFINITY,        -INFINITY,        -INFINITY,        -INFINITY,         -INFINITY          };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespPointUp2 = {
         0.0,            -0.00000094085172, -0.0000046468703, -0.0000066428421, -0.0000092440951, -0.000016143672,
        -0.000027400967, -0.000044167169,   -0.000071472173,  -0.00011793872,   -0.00018668076,   -0.00028440526,
        -0.00048680271,  -0.00073231862,    -0.0011598867,    -0.0018533672,    -0.0029206088,    -0.0046556918,
        -0.0073653944,   -0.011883618,      -0.018552838,     -0.028975237,     -0.047645309,     -0.074536711,
        -0.11677276,     -0.18593568,       -0.30108662,      -0.47368778,      -0.76056279,      -1.2489862,
        -2.0099209,      -3.0090504,        -INFINITY,        -INFINITY,        -INFINITY,        -INFINITY,
        -INFINITY,       -INFINITY,         -INFINITY,        -INFINITY,        -INFINITY,        -INFINITY,
        -INFINITY,       -INFINITY,         -INFINITY,        -INFINITY,        -INFINITY          };

// Thresholds and cached results for the Linear resampler
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearUnity = {
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,      -INFINITY, -INFINITY,
        -INFINITY, -INFINITY, -INFINITY, -INFINITY, -INFINITY   };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearDown1 = {
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0,       0.0,
         0.0,       0.0,       0.0,       0.0,       0.0        };

// These thresholds changed as a result of adjusting our resampler tests to call
// the Mixer objects in the same way that their primary callers in audio_server
// do. For reference, the previous values are (for the time being only) included
// in nearby comments, showing changes that vary widely by resampler, SRC ratio
// and frequency. These commented values will be removed soon, in upcoming CL.
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearDown2 = {
         0.0,             -0.00000048789315, -0.0000014072450, -0.0000024587243,  -0.0000034090434, -0.0000054440652,
    //   0.0,              0.0,              -0.0000010765944, -0.0000023500457,  -0.0000023204115, -0.0000024819551,
        -0.0000061476093, -0.0000088116046,  -0.000015710112,  -0.000024453233,   -0.000037277633,  -0.000057571858,
    //  -0.0000094623437, -0.0000098927249,  -0.000015352092,  -0.000024634046,   -0.000037785192,  -0.000055401688,
        -0.000097519893,  -0.00014563539,    -0.00023024045,   -0.00036696565,    -0.00057760750,   -0.00092029637,
    //  -0.000095210661,  -0.00014667886,    -0.00023037397,   -0.00036630747,    -0.00057694728,   -0.00091971656,
        -0.0014552986,    -0.0023468061,     -0.0036612787,    -0.0057175425,     -0.0093951864,    -0.014681188,
    //  -0.0014540418,    -0.0023462583,     -0.0036597335,    -0.0057131220,     -0.0093914625,    -0.014676537,
        -0.022966376,     -0.036467601,      -0.058822561,     -0.091963973,      -0.14613021,      -0.23572457,
    //  -0.022958352,     -0.036462171,      -0.058796479,     -0.091924166,      -0.14606405,      -0.23562194,
        -0.36887028,      -0.53216962,       -0.95063909,      -1.4202253,        -1.4954564,       -1.5724378,
    //  -0.36871115,      -0.53193309,       -0.95022584,      -1.4196076,        -1.4948005,       -1.5718115,
        -1.6519393,       -1.8243049,        -2.0604254,       -2.1722131,        -2.3603218,       -3.8119463,
    //  -1.6511036,       -1.8235781,        -2.0597837,       -2.1695351,        -2.3593078,       -3.8104597,
        -6.3360909,       -7.8069950,        -7.8461322,       -INFINITY,         -INFINITY          };
    //  -6.3343783,       -7.8036133,        -7.8427753,       -INFINITY,         -INFINITY          };

// These thresholds changed as a result of adjusting our resampler tests to call
// the Mixer objects in the same way that their primary callers in audio_server
// do. For reference, the previous values are (for the time being only) included
// in nearby comments, showing changes that vary widely by resampler, SRC ratio
// and frequency. These commented values will be removed soon, in upcoming CL.
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearUp1 = {
         0.0,            -0.0000037128363, -0.0000051600182, -0.0000064492483, -0.000010397929,  -0.000015090537,
    //   0.0,            -0.0000023504212, -0.0000043544860, -0.0000058073382, -0.000010595969,  -0.000013453493,
        -0.000022908902, -0.000035702193,  -0.000058330688,  -0.000094345028,  -0.00014893645,   -0.00022633894,
    //  -0.000028152172, -0.000035439283,  -0.000056947810,  -0.000094066218,  -0.00014871579,   -0.00022498862,
        -0.00038578929,  -0.00058004940,   -0.00091793307,   -0.0014649262,    -0.0023083072,    -0.0036779611,
    //  -0.00038578026,  -0.00057974394,   -0.00091727461,   -0.0014643869,    -0.0023078001,    -0.0036776787,
        -0.0058176287,   -0.0093835675,    -0.014646031,     -0.022867505,     -0.037581932,     -0.058745616,
    //  -0.0058170869,   -0.0093836438,    -0.014647070,     -0.022867596,     -0.037581764,     -0.058746770,
        -0.091918607,    -0.14605357,      -0.23571991,      -0.36895916,      -0.58737760,      -0.95053595,
    //  -0.091922190,    -0.14606728,      -0.23572614,      -0.36897083,      -0.58739390,      -0.95056709,
        -1.4945945,      -2.1692351,       -3.9378865,       -5.9979892,       -6.3361722,       -6.6849578,
    //  -1.4946470,      -2.1693107,       -3.9380418,       -5.9982547,       -6.3364566,       -6.6853228,
        -7.0470128,      -7.8437725,       -INFINITY,        -INFINITY,        -INFINITY,        -INFINITY,
    //  -7.0472288,      -7.8442190,       -INFINITY,        -INFINITY,        -INFINITY,        -INFINITY,
        -INFINITY,       -INFINITY,        -INFINITY,        -INFINITY,        -INFINITY          };
    //  -INFINITY,       -INFINITY,        -INFINITY,        -INFINITY,        -INFINITY          };
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevFreqRespLinearUp2 = {
         0.0,             0.0,             0.0,            0.0,            0.0,            0.0,
        -0.000013644048, -0.000046810601, -0.00010100580, -0.00019465068, -0.00033192920, -0.00052690209,
        -0.00093112269,  -0.0014226934,   -0.0022778943,  -0.0036651282,  -0.0057995663,  -0.0092702625,
        -0.014689288,    -0.023725530,    -0.037063175,   -0.057907640,   -0.095247821,   -0.14903146,
        -0.23350238,     -0.37182734,     -0.60212938,    -0.94732901,    -1.5210760,     -2.4979167,
        -4.0197755,      -6.0180143,      -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,       -INFINITY,       -INFINITY,      -INFINITY,      -INFINITY,      -INFINITY,
        -INFINITY,       -INFINITY,       -INFINITY,      -INFINITY,      -INFINITY        };
// clang-format on

//
// Signal-to-Noise-And-Distortion (SINAD)
//
// Sinad (signal-to-noise-and-distortion) is the ratio (in dBr) of the reference
// signal as received (nominally from a 1kHz input), compared to the power of
// all OTHER frequencies (combined via root-sum-square).
//
// Distortion is often measured at a lone reference frequency (kReferenceFreq).
// We measure noise floor at only 1 kHz, and for summary SINAD tests use only
// 40 Hz, 1 kHz and 12 kHz, but for full-spectrum tests we test 47 frequencies.
//
// These arrays hold the various SINAD results as measured during the test run.
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

//
// For SINAD, measured value must exceed or equal the below cached value.
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
        98.104753, 98.104753, 98.104753, 98.104753, -0.00000000067190481,
        -0.00000000067187492, -0.00000000067185563, -0.00000000067184599,
        -0.00000000067185852, -0.00000000067184695, -0.00000000067184599 };

// These thresholds changed as a result of adjusting our resampler tests to call
// the Mixer objects in the same way that their primary callers in audio_server
// do. For reference, the previous values are (for the time being only) included
// in nearby comments, showing changes that vary widely by resampler, SRC ratio
// and frequency. These commented values will be removed soon, in upcoming CL.
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointDown2 = {
        98.104753,  71.323906,  69.201459,  67.807032,   65.808049,  64.182137,
    //  98.104753,  71.325527,  69.203010,  67.810855,   65.810494,  64.185771,
        61.953130,  59.917570,  57.851104,  55.690602,   53.705351,  51.882208,
    //  61.956263,  59.921275,  57.854351,  55.694260,   53.708424,  51.885269,
        49.552104,  47.780722,  45.784941,  43.750229,   41.775952,  39.751377,
    //  49.554996,  47.783543,  45.787773,  43.753380,   41.778917,  39.754352,
        37.759675,  35.681921,  33.750683,  31.813971,   29.654934,  27.714346,
    //  37.762733,  35.685843,  33.752056,  31.817059,   29.659154,  27.718592,
        25.771321,  23.759419,  21.676958,  19.729111,   17.703853,  15.605551,
    //  25.773439,  23.760738,  21.679963,  19.730849,   17.706180,  15.606975,
        13.618335,  11.993293,   9.3682681,  7.5058537,   7.2618160,  7.0190895,
    //  13.628791,  11.995917,   9.3701623,  7.5067307,   7.2632536,  7.0252622,
         6.7905293,  6.3046508,  5.7219446,  0.96646899, -1.1800092, -1.9053154,
    //   6.7910862,  6.3150376,  5.7248151,  1.3060701,  -1.1796529, -1.9052303,
        -3.2481088, -3.9035256, -3.9357708, -INFINITY,   -INFINITY    };
    //  -3.1671896, -3.9018061, -3.9213881, -INFINITY,   -INFINITY    };

// These thresholds changed as a result of adjusting our resampler tests to call
// the Mixer objects in the same way that their primary callers in audio_server
// do. For reference, the previous values are (for the time being only) included
// in nearby comments, showing changes that vary widely by resampler, SRC ratio
// and frequency. These commented values will be removed soon, in upcoming CL.
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointUp1 = {
        98.104753,   65.312860,  63.186199,  61.791873,    59.790805,   58.165108,
    //  98.104753,   65.313198,  63.185034,  61.792752,    59.789427,   58.165761,
        55.935299,   53.899380,  51.832815,  49.672353,    47.686771,   45.863345,
    //  55.935501,   53.899349,  51.832904,  49.672659,    47.686826,   45.863617,
        43.533175,   41.761767,  39.765838,  37.731120,    35.756472,   33.731696,
    //  43.533377,   41.761669,  39.765810,  37.731375,    35.756649,   33.731716,
        31.739542,   29.659389,  27.727273,  25.790054,    23.627479,   21.682220,
    //  31.739704,   29.661948,  27.726999,  25.790149,    23.628946,   21.683583,
        19.731317,   17.706412,  15.605282,  13.625923,    11.551374,    9.3687440,
    //  19.730975,   17.706038,  15.604995,  13.625649,    11.551174,    9.3685059,
         7.2590103,   5.4719074,  2.4139626,  0.014632906, -0.31002047, -0.64047954,
    //   7.2637387,   5.4713226,  2.4136955,  0.022372357, -0.31031315, -0.64108807,
        -0.97223940, -1.6646409, -INFINITY,  -INFINITY,    -INFINITY,   -INFINITY,
    //  -0.97231558, -1.6650025, -INFINITY,  -INFINITY,    -INFINITY,   -INFINITY,
        -INFINITY,   -INFINITY,  -INFINITY,  -INFINITY,    -INFINITY     };
    //  -INFINITY,   -INFINITY,  -INFINITY,  -INFINITY,    -INFINITY     };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadPointUp2 = {
        98.104753, 61.280235,     59.151486, 57.758918, 55.756759, 54.130738,
        51.900960, 49.865009,     47.798133, 45.637967, 43.652049, 41.828666,
        39.498424, 37.726722,     35.730737, 33.695818, 31.720707, 29.695082,
        27.701826, 25.622181,     23.684311, 21.742982, 19.573739, 17.616782,
        15.645886, 13.590901,     11.439506,  9.3839187, 7.1806586, 4.7728152,
         2.3024022, 0.0024982464, -INFINITY,  -INFINITY, -INFINITY, -INFINITY,
        -INFINITY, -INFINITY,     -INFINITY,  -INFINITY, -INFINITY, -INFINITY,
        -INFINITY, -INFINITY,     -INFINITY,  -INFINITY, -INFINITY   };


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
        98.104753, 98.104753, 98.104753, 98.104753, -0.00000000067190481,
        -0.00000000067187492, -0.00000000067185563, -0.00000000067184599,
        -0.00000000067185852, -0.00000000067184695, -0.00000000067184599 };

// These thresholds changed as a result of adjusting our resampler tests to call
// the Mixer objects in the same way that their primary callers in audio_server
// do. For reference, the previous values are (for the time being only) included
// in nearby comments, showing changes that vary widely by resampler, SRC ratio
// and frequency. These commented values will be removed soon, in upcoming CL.
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearDown2 = {
        98.104753,  95.289561,  94.891547,  94.525141, 93.940073,   93.323163,
    //  98.104753,  93.124218,  93.076842,  93.089388, 93.087849,   93.104014,
        92.166484,  90.848747,  89.381217,  87.537958, 85.881447,   84.184903,
    //  93.124252,  93.090645,  93.094310,  93.042959, 93.054275,   92.991397,
        81.971267,  80.249597,  78.265091,  76.229679, 74.247552,   72.172114,
    //  92.875920,  92.646527,  92.008977,  90.782039, 88.678501,   85.663296,
        70.093247,  67.872158,  65.726354,  63.482846, 60.816577,   58.233046,
    //  82.149077,  78.222449,  74.421795,  70.595109, 66.291866,   62.410875,
        55.437671,  52.131914,  48.843995,  45.398143, 41.652924,   37.620863,
    //  58.517504,  54.480815,  50.300839,  46.375885, 42.282581,   38.012863,
        33.707357,  30.399366,  24.927414,  20.920976, 20.389515,   19.866953,
    //  33.952617,  30.563039,  25.010451,  20.970874, 20.436067,   19.911664,
        19.355321,  18.300297,  16.983910,  14.388789, -0.12106284, -0.42889467,
    //  19.394056,  18.336941,  17.017172,  14.391131, -0.12037547, -0.42761185,
        -1.7464492, -3.0359045, -3.0749778, -INFINITY, -INFINITY     };
    //  -1.7436040, -3.0325247, -3.0715630, -INFINITY, -INFINITY     };

// These thresholds changed as a result of adjusting our resampler tests to call
// the Mixer objects in the same way that their primary callers in audio_server
// do. For reference, the previous values are (for the time being only) included
// in nearby comments, showing changes that vary widely by resampler, SRC ratio
// and frequency. These commented values will be removed soon, in upcoming CL.
const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearUp1 = {
        98.104753,  95.266603,   94.883150,  94.529677,  93.927523,  93.307331,
    //  98.104753,  93.128846,   93.096975,  93.096829,  93.107059,  93.060076,
        92.163956,  90.837338,   89.337242,  87.503074,  85.778623,  84.037384,
    //  93.052275,  93.072997,   92.992189,  92.857695,  92.586523,  92.039931,
        81.710516,  79.883300,   77.693324,  75.359685,  72.935502,  70.257695,
    //  90.468576,  88.599321,   85.677088,  82.109717,  78.343967,  74.403325,
        67.397070,  64.170431,   60.950924,  57.550937,  53.584723,  49.896485,
    //  70.441937,  66.297342,   62.428449,  58.548117,  54.215625,  50.308188,
        46.111739,  42.093635,   37.908881,  33.884891,  29.596778,  24.987412,
    //  46.376339,  42.282366,   38.008890,  33.946106,  29.632310,  25.006935,
        20.426919,  16.441892,    9.4384135,  3.8400890,  3.0580579,  2.2804544,
    //  20.437118,  16.447273,    9.4396395,  3.8398891,  3.0577226,  2.2800661,
         1.5022137, -0.12381333, -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
    //   1.5015886, -0.12452049, -INFINITY,  -INFINITY,  -INFINITY,  -INFINITY,
        -INFINITY,  -INFINITY,   -INFINITY,  -INFINITY,  -INFINITY    };
    //  -INFINITY,  -INFINITY,   -INFINITY,  -INFINITY,  -INFINITY    };

const std::array<double, FrequencySet::kNumReferenceFreqs>
    AudioResult::kPrevSinadLinearUp2 = {
        98.104753, 96.792502,    97.064182, 97.076402, 97.101973, 97.277858,
        97.222941, 96.662384,    94.928026, 91.603820, 87.824016, 84.116671,
        79.320913, 75.679693,    71.610512, 67.486632, 63.502715, 59.428508,
        55.428268, 51.259762,    47.378702, 43.492485, 39.151453, 35.236064,
        31.293422, 27.182867,    22.879676, 18.768294, 14.361632,  9.5458552,
        4.6049849, 0.0051707793, -INFINITY, -INFINITY, -INFINITY, -INFINITY,
        -INFINITY, -INFINITY,    -INFINITY, -INFINITY, -INFINITY, -INFINITY,
        -INFINITY, -INFINITY,    -INFINITY, -INFINITY, -INFINITY   };
// clang-format on

//
// Dynamic Range
//
// Dynamic Range (gain integrity and system response at low volume levels) is
// measured at a single reference frequency (kReferenceFreq), on a lone mono
// source without SRC. By determining the smallest possible change in gain that
// causes a detectable change in output (our 'gain epsilon'), we determine a
// system's sensitivity to gain changes. We measure not only the output level of
// the signal, but also the noise level across all other frequencies. Performing
// these same measurements (output level and noise level) with a gain of -60 dB
// as well is the standard definition of Dynamic Range testing: by adding 60 dB
// to the measured signal-to-noise, one determines a system's usable range of
// data values (translatable into the more accessible Effective Number Of Bits
// metric). The level measurement at -60 dB is useful not only as a component of
// the "noise in the presence of signal" calculation, but also as a second
// avenue toward measuring a system's linearity/accuracy/precision with regard
// to data scaling and gain.

// The nearest-unity scale at which we observe effects on signals.
constexpr uint32_t AudioResult::kScaleEpsilon;
// The lowest scale at which full-scale signals are not reduced to zero.
constexpr uint32_t AudioResult::kMinScaleNonZero;

// Level and unwanted artifacts, applying the smallest-detectable gain change.
double AudioResult::LevelEpsilonDown = -INFINITY;
double AudioResult::SinadEpsilonDown = -INFINITY;

// Level and unwanted artifacts, applying -60dB gain (measures dynamic range).
double AudioResult::Level60Down = -INFINITY;
double AudioResult::Sinad60Down = -INFINITY;

// For dynamic range, level-being-checked (in dBFS) should be within
// kPrevDynRangeTolerance of the dB gain setting (e.g. -60.0 dB).
constexpr double AudioResult::kPrevDynRangeTolerance;

// Previously-cached level and sinad when applying the smallest-detectable gain.
constexpr double AudioResult::kPrevLevelEpsilonDown;
constexpr double AudioResult::kPrevSinadEpsilonDown;

// Previously-cached sinad when applying exactly -60.0 dB gain.
constexpr double AudioResult::kPrevSinad60Down;

//
// Rechannelization
//
// Previously-cached thresholds related to stereo-to-mono mixing.
constexpr double AudioResult::kPrevLevelStereoMono;
constexpr double AudioResult::kPrevStereoMonoTolerance;
constexpr double AudioResult::kPrevFloorStereoMono;

}  // namespace test
}  // namespace audio
}  // namespace media
