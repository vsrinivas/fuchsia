// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>

#include "audio_analysis.h"
#include "gtest/gtest.h"

namespace media {
namespace test {

// Test the inline function that converts a numerical value to dB.
TEST(AnalysisHelpers, ValToDb) {
  EXPECT_EQ(ValToDb(1.0), 0.0f);    // Unity is 0 dB
  EXPECT_EQ(ValToDb(100.0), 40.0);  // 100x is 40 dB
  EXPECT_EQ(ValToDb(0.1), -20.0);   // 10% is -20 dB

  EXPECT_GE(ValToDb(0.5), -6.0206004);  // 50% is roughly -6.0206 dB
  EXPECT_LE(ValToDb(0.5), -6.0205998);  // inexact representation: 2 compares
}

// Test the inline function that converts from fixed-point gain to dB.
TEST(AnalysisHelpers, GainScaleToDb) {
  EXPECT_EQ(GainScaleToDb(audio::Gain::kUnityScale), 0.0);
  EXPECT_EQ(GainScaleToDb(audio::Gain::kUnityScale * 10), 20.0);

  EXPECT_GE(GainScaleToDb(audio::Gain::kUnityScale / 100), -40.000002);
  EXPECT_LE(GainScaleToDb(audio::Gain::kUnityScale / 100), -39.999998);

  EXPECT_GE(GainScaleToDb(audio::Gain::kUnityScale >> 1), -6.0206004);
  EXPECT_LE(GainScaleToDb(audio::Gain::kUnityScale >> 1), -6.0205998);
}

// Test uint8 version of CompareBuffers, which we use to test output buffers
TEST(AnalysisHelpers, CompareBuffers8) {
  uint8_t source[] = {0x42, 0x55};
  uint8_t expect[] = {0x42, 0xAA};

  // First values match ...
  EXPECT_TRUE(CompareBuffers(source, expect, 1));
  // ... but entire buffer does NOT
  EXPECT_FALSE(CompareBuffers(source, expect, fbl::count_of(source), false));
}

// Test int16 version of CompareBuffers, which we use to test output buffers
TEST(AnalysisHelpers, CompareBuffers16) {
  int16_t source[] = {-1, 0x1157, 0x5555};
  int16_t expect[] = {-1, 0x1357, 0x5555};

  // Buffers do not match ...
  EXPECT_FALSE(CompareBuffers(source, expect, fbl::count_of(source), false));
  // ... but the first values DO
  EXPECT_TRUE(CompareBuffers(source, expect, 1));
}

// Test int32 version of CompareBuffers, which we use to test accum buffers
TEST(AnalysisHelpers, CompareBuffers32) {
  int32_t source[] = {0x13579BDF, 0x26AE048C, -0x76543210, 0x1234567};
  int32_t expect[] = {0x13579BDF, 0x26AE048C, -0x76543210, 0x7654321};

  // Buffers do not match ...
  EXPECT_FALSE(CompareBuffers(source, expect, fbl::count_of(source), false));
  // ... but the first three values DO
  EXPECT_TRUE(CompareBuffers(source, expect, fbl::count_of(source) - 1));
}

// Test uint8 version of this func, which we use to test output buffers
TEST(AnalysisHelpers, CompareBuffToVal8) {
  uint8_t source[] = {0xBB, 0xBB};

  // No match ...
  EXPECT_FALSE(CompareBufferToVal(source, static_cast<uint8_t>(0xBC),
                                  fbl::count_of(source), false));
  // Match
  EXPECT_TRUE(CompareBufferToVal(source, static_cast<uint8_t>(0xBB),
                                 fbl::count_of(source)));
}

// Test int16 version of this func, which we use to test output buffers
TEST(AnalysisHelpers, CompareBuffToVal16) {
  int16_t source[] = {0xBAD, 0xCAD};

  // No match ...
  EXPECT_FALSE(CompareBufferToVal(source, static_cast<int16_t>(0xBAD),
                                  fbl::count_of(source), false));
  // Match - if we only look at the second value
  EXPECT_TRUE(CompareBufferToVal(source + 1, static_cast<int16_t>(0xCAD), 1));
}

// Test int32 version of this func, which we use to test accum buffers
TEST(AnalysisHelpers, CompareBuffToVal32) {
  int32_t source[] = {0xF00CAFE, 0xBADF00D};

  // No match ...
  EXPECT_FALSE(
      CompareBufferToVal(source, 0xF00CAFE, fbl::count_of(source), false));
  // Match - if we only look at the first value
  EXPECT_TRUE(CompareBufferToVal(source, 0xF00CAFE, 1));
}

// AccumCosine writes a cosine wave into a given buffer & length, at the given
// frequency and magnitude (default 1.0). The final two parameters are phase
// (default 0.0) and whether to accumulate with existing values (default true).
TEST(AnalysisHelpers, AccumCosine16) {
  int16_t source[] = {12345, -6543};
  AccumCosine(source, 2, 0.0, -32766.4, 0.0, false);  // overwrite source[]

  // Frequency of 0.0 produces constant value, with -.4 rounded toward zero.
  EXPECT_TRUE(CompareBufferToVal(source, static_cast<int16_t>(-32766),
                                 fbl::count_of(source)));
}

TEST(AnalysisHelpers, AccumCosine32) {
  int32_t source[] = {-4000, 0, 4000, 8000};
  AccumCosine(source, 4, 1.0, 12345.6, M_PI, true);  // add to existing vals

  // PI phase leads to effective magnitude of -12345.6.
  // At frequency 1.0, the change to the buffer is [-12345.6, 0, +12345.6, 0],
  // as .6 values are rounded away from zero.
  int32_t expect[] = {-16346, 0, 16346, 8000};
  EXPECT_TRUE(CompareBuffers(source, expect, fbl::count_of(source)));
}

TEST(AnalysisHelpers, AccumCosineDouble) {
  double source[] = {-4000.0, -83000.0, 4000.0, 78000.0};
  AccumCosine(source, 4, 1.0, 12345.5, M_PI, true);  // add to existing vals

  // PI phase leads to effective magnitude of -12345.5.
  // At frequency 1.0, the change to the buffer is [-12345.5, 0, +12345.5, 0],
  // with no rounding because input is double.
  double expect[] = {-16345.5, -83000.0, 16345.5, 78000.0};
  EXPECT_TRUE(CompareBuffers(source, expect, fbl::count_of(source)));
}

TEST(AnalysisHelpers, FFT) {
  double reals[16];
  double imags[16];
  const double epsilon = 0.00000015;

  const uint32_t buf_size = fbl::count_of(reals);
  static_assert(fbl::count_of(imags) == buf_size, "buf sizes must match");
  const uint32_t buf_sz_2 = buf_size >> 1;

  // Impulse input produces constant val in all frequency bins
  AccumCosine(reals, buf_size, 0.0, 0.0, 0.0, false);
  reals[0] = 1000000.0;
  AccumCosine(imags, buf_size, 0.0, 0.0, 0.0, false);
  FFT(reals, imags, buf_size);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    const double expect = 1000000.0;
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;

    EXPECT_LE(imags[idx], epsilon) << idx;
    EXPECT_GE(imags[idx], -epsilon) << idx;
  }

  // DC input produces val only in frequency bin 0
  AccumCosine(reals, buf_size, 0.0, 700000.0, 0.0, false);
  AccumCosine(imags, buf_size, 0.0, 0.0, 0.0, false);
  FFT(reals, imags, buf_size);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    const double expect =
        (idx == 0 ? 700000.0 * static_cast<double>(buf_size) : 0.0);
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;

    EXPECT_LE(imags[idx], epsilon) << idx;
    EXPECT_GE(imags[idx], -epsilon) << idx;
  }

  // Folding frequency (buf_size/2) produces all zeroes except N/2.
  double test_val = 1001001.0;
  AccumCosine(reals, buf_size, buf_sz_2, test_val, 0.0, false);
  AccumCosine(imags, buf_size, 0.0, 0.0, 0.0, false);
  FFT(reals, imags, buf_size);

  for (uint32_t idx = 0; idx < buf_sz_2; ++idx) {
    EXPECT_LE(reals[idx], epsilon) << idx;
    EXPECT_GE(reals[idx], -epsilon) << idx;

    EXPECT_LE(imags[idx], epsilon) << idx;
    EXPECT_GE(imags[idx], -epsilon) << idx;
  }
  EXPECT_LE(reals[buf_sz_2], (test_val * buf_size) + epsilon);
  EXPECT_GE(reals[buf_sz_2], (test_val * buf_size) - epsilon);
  EXPECT_LE(imags[buf_sz_2], epsilon);
  EXPECT_GE(imags[buf_sz_2], -epsilon);

  // A cosine that fits exactly into the buffer len should produce zero values
  // in all frequency bins except for bin 1.
  test_val = 20202020.0;
  AccumCosine(reals, buf_size, 1.0, test_val, 0.0, false);
  AccumCosine(imags, buf_size, 0.0, 0.0, 0.0, false);
  FFT(reals, imags, buf_size);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    const double expect = (idx == 1) ? (test_val * buf_size / 2.0) : 0.0;
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;

    EXPECT_LE(imags[idx], epsilon) << idx;
    EXPECT_GE(imags[idx], -epsilon) << idx;
  }

  // That same cosine, shifted by PI/2, should have identical reesults, but
  // flipped between real and imaginary domains.
  AccumCosine(reals, buf_size, 1.0, test_val, -M_PI / 2.0, false);
  AccumCosine(imags, buf_size, 0.0, 0.0, 0.0, false);
  FFT(reals, imags, buf_size);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    EXPECT_LE(reals[idx], epsilon) << idx;
    EXPECT_GE(reals[idx], -epsilon) << idx;

    const double expect = (idx == 1) ? (test_val * buf_size / 2.0) : 0.0;
    EXPECT_LE(imags[idx], -expect + epsilon) << idx;
    EXPECT_GE(imags[idx], -expect - epsilon) << idx;
  }
}

// MeasureAudioFreq function accepts buffer of audio data, length and the
// frequency at which to analyze audio. It returns magnitude of signal at that
// frequency, and combined (root-sum-square) magnitude of all OTHER frequencies.
// For inputs of magnitude 3 and 4, their combination equals 5.
TEST(AnalysisHelpers, MeasureAudioFreq) {
  int32_t reals[] = {5, -3, 13, -3};  // sinusoids at freq 0,1,2; magns 3,4,6
  float magn_signal = -54.32;         // will be overwritten
  float magn_other = 42.0f;           // will be overwritten

  MeasureAudioFreq(reals, fbl::count_of(reals), 0, &magn_signal);
  EXPECT_EQ(3.0f, magn_signal);

  MeasureAudioFreq(reals, fbl::count_of(reals), 1, &magn_signal, &magn_other);
  EXPECT_EQ(4.0f, magn_signal);

  MeasureAudioFreq(reals, fbl::count_of(reals), 2, &magn_signal, &magn_other);
  EXPECT_EQ(6.0f, magn_signal);
  EXPECT_EQ(5.0f, magn_other);
}

}  // namespace test
}  // namespace media
