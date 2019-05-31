// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>

#include "gtest/gtest.h"
#include "src/media/audio/audio_core/mixer/test/audio_analysis.h"

namespace media::audio::test {

constexpr double RT_2 = 1.4142135623730950488016887242;

// Test uint8 version of CompareBuffers, which we use to test output buffers
TEST(AnalysisHelpers, CompareBuffers_8) {
  uint8_t source[] = {0x42, 0x55};
  uint8_t expect[] = {0x42, 0xAA};

  // First values match ...
  EXPECT_TRUE(CompareBuffers(source, expect, 1));
  // ... but entire buffer does NOT
  EXPECT_FALSE(CompareBuffers(source, expect, fbl::count_of(source), false));
}

// Test int16 version of CompareBuffers, which we use to test output buffers
TEST(AnalysisHelpers, CompareBuffers_16) {
  int16_t source[] = {-1, 0x1157, 0x5555};
  int16_t expect[] = {-1, 0x1357, 0x5555};

  // Buffers do not match ...
  EXPECT_FALSE(CompareBuffers(source, expect, fbl::count_of(source), false));
  // ... but the first values DO
  EXPECT_TRUE(CompareBuffers(source, expect, 1));
}

// Test int32 version of CompareBuffers, which we use to test accum buffers
TEST(AnalysisHelpers, CompareBuffers_32) {
  int32_t source[] = {0x13579BDF, 0x26AE048C, -0x76543210, 0x1234567};
  int32_t expect[] = {0x13579BDF, 0x26AE048C, -0x76543210, 0x7654321};

  // Buffers do not match ...
  EXPECT_FALSE(CompareBuffers(source, expect, fbl::count_of(source), false));
  // ... but the first three values DO
  EXPECT_TRUE(CompareBuffers(source, expect, fbl::count_of(source) - 1));
}

// Test float version of CompareBuffers, which we use to test accum buffers
TEST(AnalysisHelpers, CompareBuffers_Float) {
  float source[] = {-0.5f, 1.0f / 3.0f, -2.0f / 9.0f, 3.1416f};
  float expect[] = {-0.5f, 1.0f / 3.0f, -2.0f / 9.0f, 3.14159f};

  // Buffers do not match ...
  EXPECT_FALSE(CompareBuffers(source, expect, fbl::count_of(source), false));
  // ... but the first three values DO
  EXPECT_TRUE(CompareBuffers(source, expect, fbl::count_of(source) - 1));
}

// Test double version of CompareBuffers, which we use to test accum buffers
TEST(AnalysisHelpers, CompareBuffers_Double) {
  double source[] = {-0.5, 1.0 / 3.0, -2.0 / 9.0, 3.14159001};
  double expect[] = {-0.5, 1.0 / 3.0, -2.0 / 9.0, 3.14159};

  // Buffers do not match ...
  EXPECT_FALSE(CompareBuffers(source, expect, fbl::count_of(source), false));
  // ... but the first three values DO
  EXPECT_TRUE(CompareBuffers(source, expect, fbl::count_of(source) - 1));
}

// Test uint8 version of this func, which we use to test output buffers
TEST(AnalysisHelpers, CompareBuffToVal_8) {
  uint8_t source[] = {0xBB, 0xBB};

  // No match ...
  EXPECT_FALSE(CompareBufferToVal(source, static_cast<uint8_t>(0xBC),
                                  fbl::count_of(source), false));
  // Match
  EXPECT_TRUE(CompareBufferToVal(source, static_cast<uint8_t>(0xBB),
                                 fbl::count_of(source)));
}

// Test int16 version of this func, which we use to test output buffers
TEST(AnalysisHelpers, CompareBuffToVal_16) {
  int16_t source[] = {0xBAD, 0xCAD};

  // No match ...
  EXPECT_FALSE(CompareBufferToVal(source, static_cast<int16_t>(0xBAD),
                                  fbl::count_of(source), false));
  // Match - if we only look at the second value
  EXPECT_TRUE(CompareBufferToVal(source + 1, static_cast<int16_t>(0xCAD), 1));
}

// Test int32 version of this func, which we use to test accum buffers
TEST(AnalysisHelpers, CompareBuffToVal_32) {
  int32_t source[] = {0xF00CAFE, 0xBADF00D};

  // No match ...
  EXPECT_FALSE(
      CompareBufferToVal(source, 0xF00CAFE, fbl::count_of(source), false));
  // Match - if we only look at the first value
  EXPECT_TRUE(CompareBufferToVal(source, 0xF00CAFE, 1));
}

// Test float version of this func, which we use to test output buffers
TEST(AnalysisHelpers, CompareBuffToVal_Float) {
  float source[] = {3.1415926f, 2.7182818f};

  // No match ...
  EXPECT_FALSE(
      CompareBufferToVal(source, 3.1415926f, fbl::count_of(source), false));
  // Match - if we only look at the first value
  EXPECT_TRUE(CompareBufferToVal(source, 3.1415926f, 1));
}

// GenerateCosine writes a cosine wave into given buffer & length, at given
// frequency, magnitude (default 1.0), and phase offset (default false).
// The 'accumulate' flag specifies whether to add into previous contents.
// OverwriteCosine/AccumulateCosine variants eliminate this flag.
//
// The uint8_t variant also provides the 0x80 offset to generated values.
TEST(AnalysisHelpers, GenerateCosine_8) {
  uint8_t source[] = {0, 0xFF};
  // false: overwrite previous values in source[]
  GenerateCosine(source, fbl::count_of(source), 0.0, false, 0.0, 0.0);

  // Frequency 0.0 produces constant value. Val 0 is shifted to 0x80.
  EXPECT_TRUE(CompareBufferToVal(source, static_cast<uint8_t>(0x80),
                                 fbl::count_of(source)));
}

TEST(AnalysisHelpers, GenerateCosine_16) {
  int16_t source[] = {12345, -6543};
  GenerateCosine(source, fbl::count_of(source), 0.0, false, -32766.4);

  // Frequency of 0.0 produces constant value, with -.4 rounded toward zero.
  EXPECT_TRUE(CompareBufferToVal(source, static_cast<int16_t>(-32766),
                                 fbl::count_of(source)));

  // Test default bool value (false): also overwrite
  OverwriteCosine(source, 1, 0.0, -41.5, 0.0);

  // Should only overwrite one value, and -.5 rounds away from zero.
  EXPECT_EQ(source[0], -42);
  EXPECT_EQ(source[1], -32766);
}

TEST(AnalysisHelpers, GenerateCosine_32) {
  int32_t source[] = {-4000, 0, 4000, 8000};

  // true: add generated signal into existing source[] values
  GenerateCosine(source, fbl::count_of(source), 1.0, true, 12345.6, M_PI);

  // PI phase leads to effective magnitude of -12345.6.
  // At frequency 1.0, the change to the buffer is [-12345.6, 0, +12345.6, 0],
  // with +.6 values being rounded away from zero.
  int32_t expect[] = {-16346, 0, 16346, 8000};
  EXPECT_TRUE(CompareBuffers(source, expect, fbl::count_of(source)));
}

// Test float-based version of AccumCosine, including default amplitude (1.0)
TEST(AnalysisHelpers, GenerateCosine_Float) {
  float source[] = {-1.0f, -2.0f, 3.0f, 4.0f};  // to be overwritten

  OverwriteCosine(source, fbl::count_of(source), 0.0);
  float expect[] = {1.0f, 1.0f, 1.0f, 1.0f};
  EXPECT_TRUE(CompareBuffers(source, expect, fbl::count_of(source)));

  // PI/2 shifts the freq:1 wave left by 1 here
  AccumulateCosine(source, fbl::count_of(source), 1.0, 0.5, M_PI / 2.0);
  float expect2[] = {1.0f, 0.5f, 1.0f, 1.5f};
  EXPECT_TRUE(CompareBuffers(source, expect2, fbl::count_of(source)));
}

// Test double-based version of AccumCosine (no int-based rounding)
TEST(AnalysisHelpers, GenerateCosine_Double) {
  double source[] = {-4000.0, -83000.0, 4000.0, 78000.0};
  AccumulateCosine(source, fbl::count_of(source), 1.0, 12345.5,
                   M_PI);  // add to existing

  // PI phase leads to effective magnitude of -12345.5.
  // At frequency 1.0, the change to the buffer is [-12345.5, 0, +12345.5, 0],
  // with no rounding because input is double.
  double expect[] = {-16345.5, -83000.0, 16345.5, 78000.0};
  EXPECT_TRUE(CompareBuffers(source, expect, fbl::count_of(source)));
}

TEST(AnalysisHelpers, GetPhase) {
  double reals[] = {0.5, 23, 0, -42, -0.1, -123, 0, 68, 0};
  double imags[] = {0, 23, 243, 42, 0, -123, -243, -68, 0};
  double expect[] = {0,    M_PI / 4,      M_PI / 2,  3 * M_PI / 4,
                     M_PI, -3 * M_PI / 4, -M_PI / 2, -M_PI / 4,
                     0};
  static_assert(fbl::count_of(imags) == fbl::count_of(reals), "buf mismatch");
  static_assert(fbl::count_of(expect) == fbl::count_of(reals), "buf mismatch");

  for (uint32_t idx = 0; idx < fbl::count_of(reals); ++idx) {
    EXPECT_EQ(expect[idx], GetPhase(reals[idx], imags[idx]));
  }
}

TEST(AnalysisHelpers, RectToPolar) {
  double real[] = {1.0, 1.0, 0.0, -1.0, -1.0, -1.0, 0.0, 1.0, 0.0, -0.0};
  double imag[] = {0.0, 1.0, 1.0, 1.0, -0.0, -1.0, -1.0, -1.0, 0.0, -0.0};
  double magn[10];
  double phase[10];
  const double epsilon = 0.00000001;

  RectangularToPolar(real, imag, fbl::count_of(real), magn, phase);
  double expect_magn[] = {1.0, RT_2, 1.0, RT_2, 1.0, RT_2, 1.0, RT_2, 0.0, 0.0};

  double expect_phase[] = {
      0.0,           M_PI / 4,  M_PI / 2,  3 * M_PI / 4, M_PI,
      -3 * M_PI / 4, -M_PI / 2, -M_PI / 4, 0.0,          0.0};

  // We used double here; below are acceptable and reliable tolerances
  for (uint32_t idx = 0; idx < fbl::count_of(expect_magn); ++idx) {
    EXPECT_LE(magn[idx], expect_magn[idx] + epsilon) << idx;
    EXPECT_GE(magn[idx], expect_magn[idx] - epsilon) << idx;

    EXPECT_LE(phase[idx], expect_phase[idx] + epsilon) << idx;
    EXPECT_GE(phase[idx], expect_phase[idx] - epsilon) << idx;
  }
}

TEST(AnalysisHelpers, RealDFT) {
  double reals[16];
  const uint32_t buf_size = fbl::count_of(reals);
  const double epsilon = 0.0000001024;

  const uint32_t buf_sz_2 = buf_size >> 1;
  double real_freq[9];
  double imag_freq[9];
  static_assert(fbl::count_of(real_freq) == buf_sz_2 + 1,
                "buf sizes must match");
  static_assert(fbl::count_of(imag_freq) == buf_sz_2 + 1,
                "buf sizes must match");

  // impulse
  OverwriteCosine(reals, buf_size, 0.0, 0.0);
  reals[0] = 1000000.0;
  RealDFT(reals, buf_size, real_freq, imag_freq);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    const double expect = 1000000.0;
    EXPECT_LE(real_freq[idx], expect + epsilon) << idx;
    EXPECT_GE(real_freq[idx], expect - epsilon) << idx;

    EXPECT_LE(imag_freq[idx], epsilon) << idx;
    EXPECT_GE(imag_freq[idx], -epsilon) << idx;
  }

  // DC
  OverwriteCosine(reals, buf_size, 0.0, 700000.0);
  RealDFT(reals, buf_size, real_freq, imag_freq);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    const double expect =
        (idx == 0 ? 700000.0 * static_cast<double>(buf_size) : 0.0);
    EXPECT_LE(real_freq[idx], expect + epsilon) << idx;
    EXPECT_GE(real_freq[idx], expect - epsilon) << idx;

    EXPECT_LE(imag_freq[idx], epsilon) << idx;
    EXPECT_GE(imag_freq[idx], -epsilon) << idx;
  }

  // folding freq
  OverwriteCosine(reals, buf_size, buf_size / 2.0, 1001001.0);
  RealDFT(reals, buf_size, real_freq, imag_freq);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    const double expect = (idx == buf_size / 2)
                              ? (1001001.0 * static_cast<double>(buf_size))
                              : 0.0;
    EXPECT_LE(real_freq[idx], expect + epsilon) << idx;
    EXPECT_GE(real_freq[idx], expect - epsilon) << idx;

    EXPECT_LE(imag_freq[idx], epsilon) << idx;
    EXPECT_GE(imag_freq[idx], -epsilon) << idx;
  }

  // 1
  OverwriteCosine(reals, buf_size, 1.0, 20202020.0);
  RealDFT(reals, buf_size, real_freq, imag_freq);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    const double expect =
        (idx == 1) ? (20202020.0 * static_cast<double>(buf_size) / 2.0) : 0.0;
    EXPECT_LE(real_freq[idx], expect + epsilon) << idx;
    EXPECT_GE(real_freq[idx], expect - epsilon) << idx;

    EXPECT_LE(imag_freq[idx], epsilon) << idx;
    EXPECT_GE(imag_freq[idx], -epsilon) << idx;
  }

  // 1, with -PI/2 phase
  OverwriteCosine(reals, buf_size, 1.0, 20202020.0, -M_PI / 2.0);
  RealDFT(reals, buf_size, real_freq, imag_freq);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    EXPECT_LE(real_freq[idx], epsilon) << idx;
    EXPECT_GE(real_freq[idx], -epsilon) << idx;

    const double expect =
        (idx == 1) ? (20202020.0 * static_cast<double>(buf_size) / 2.0) : 0.0;
    EXPECT_LE(imag_freq[idx], -expect + epsilon) << idx;
    EXPECT_GE(imag_freq[idx], -expect - epsilon) << idx;
  }
}

TEST(AnalysisHelpers, IDFT) {
  double reals[16];
  double expects[16];
  const uint32_t buf_size = fbl::count_of(reals);
  const double epsilon = 0.00000002;
  static_assert(buf_size == fbl::count_of(expects), "buf size mismatch");

  double real_freq[9];
  double imag_freq[9];
  const uint32_t buf_sz_2 = buf_size >> 1;
  static_assert(fbl::count_of(real_freq) == buf_sz_2 + 1, "buf size mismatch");
  static_assert(fbl::count_of(imag_freq) == buf_sz_2 + 1, "buf size mismatch");

  // impulse
  OverwriteCosine(real_freq, buf_sz_2 + 1, 0.0, 123.0);
  OverwriteCosine(imag_freq, buf_sz_2 + 1, 0.0, 0.0);

  InverseDFT(real_freq, imag_freq, buf_size, reals);
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    const double expect = (idx == 0 ? 123.0 : 0.0);
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;
  }

  // DC
  OverwriteCosine(real_freq, buf_sz_2 + 1, 0.0, 0.0);
  real_freq[0] = 4321.0 * buf_size;
  OverwriteCosine(imag_freq, buf_sz_2 + 1, 0.0, 0.0);

  InverseDFT(real_freq, imag_freq, buf_size, reals);
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    const double expect = 4321.0;
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;
  }

  // folding freq
  OverwriteCosine(real_freq, buf_sz_2 + 1, 0.0, 0.0);
  real_freq[buf_sz_2] = 10203.0 * buf_size;
  OverwriteCosine(imag_freq, buf_sz_2 + 1, 0.0, 0.0);

  InverseDFT(real_freq, imag_freq, buf_size, reals);

  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    const double expect = (idx % 2 == 0 ? 10203.0 : -10203.0);
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;
  }

  // freq 1
  OverwriteCosine(real_freq, buf_sz_2 + 1, 0.0, 0.0);
  real_freq[1] = 20202020.0 * buf_sz_2;
  OverwriteCosine(imag_freq, buf_sz_2 + 1, 0.0, 0.0);

  OverwriteCosine(expects, buf_size, 1.0, 20202020.0);
  InverseDFT(real_freq, imag_freq, buf_size, reals);

  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    EXPECT_LE(reals[idx], expects[idx] + epsilon) << idx;
    EXPECT_GE(reals[idx], expects[idx] - epsilon) << idx;
  }

  // freq 1, with 3PI/4 phase
  OverwriteCosine(real_freq, buf_sz_2 + 1, 0.0, 0.0);
  real_freq[1] = -20202020.0 / RT_2 * buf_sz_2;
  OverwriteCosine(imag_freq, buf_sz_2 + 1, 0.0, 0.0);
  imag_freq[1] = 20202020.0 / RT_2 * buf_sz_2;

  OverwriteCosine(expects, buf_size, 1.0, 20202020.0, 3.0 * M_PI / 4.0);
  InverseDFT(real_freq, imag_freq, buf_size, reals);

  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    EXPECT_LE(reals[idx], expects[idx] + epsilon) << idx;
    EXPECT_GE(reals[idx], expects[idx] - epsilon) << idx;
  }
}

TEST(AnalysisHelpers, FFT) {
  double reals[16];
  double imags[16];
  const double epsilon = 0.00000015;

  const uint32_t buf_size = fbl::count_of(reals);
  static_assert(fbl::count_of(imags) == buf_size, "buf sizes must match");
  const uint32_t buf_sz_2 = buf_size >> 1;

  // Impulse input produces constant val in all frequency bins
  OverwriteCosine(reals, buf_size, 0.0, 0.0);
  reals[0] = 1000000.0;
  OverwriteCosine(imags, buf_size, 0.0, 0.0);
  FFT(reals, imags, buf_size);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    const double expect = 1000000.0;
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;

    EXPECT_LE(imags[idx], epsilon) << idx;
    EXPECT_GE(imags[idx], -epsilon) << idx;
  }

  // DC input produces val only in frequency bin 0
  OverwriteCosine(reals, buf_size, 0.0, 700000.0);
  OverwriteCosine(imags, buf_size, 0.0, 0.0);
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
  OverwriteCosine(reals, buf_size, buf_sz_2, test_val);
  OverwriteCosine(imags, buf_size, 0.0, 0.0);
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
  OverwriteCosine(reals, buf_size, 1.0, test_val);
  OverwriteCosine(imags, buf_size, 0.0, 0.0);
  FFT(reals, imags, buf_size);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    const double expect = (idx == 1) ? (test_val * buf_size / 2.0) : 0.0;
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;

    EXPECT_LE(imags[idx], epsilon) << idx;
    EXPECT_GE(imags[idx], -epsilon) << idx;
  }

  // That same cosine, shifted by PI/2, should have identical results, but
  // flipped between real and imaginary domains.
  OverwriteCosine(reals, buf_size, 1.0, test_val, -M_PI / 2.0);
  OverwriteCosine(imags, buf_size, 0.0, 0.0, 0.0);
  FFT(reals, imags, buf_size);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    EXPECT_LE(reals[idx], epsilon) << idx;
    EXPECT_GE(reals[idx], -epsilon) << idx;

    const double expect = (idx == 1) ? (test_val * buf_size / 2.0) : 0.0;
    EXPECT_LE(imags[idx], -expect + epsilon) << idx;
    EXPECT_GE(imags[idx], -expect - epsilon) << idx;
  }
}

TEST(AnalysisHelpers, IFFT) {
  double reals[16];
  double imags[16];
  double expects[16];
  const uint32_t buf_size = fbl::count_of(reals);
  const uint32_t buf_sz_2 = buf_size >> 1;

  const double epsilon = 0.00000002;
  static_assert(buf_size == fbl::count_of(imags), "buf size mismatch");
  static_assert(buf_size == fbl::count_of(expects), "buf size mismatch");

  // impulse
  OverwriteCosine(reals, buf_size, 0.0, 123.0);
  OverwriteCosine(imags, buf_size, 0.0, 0.0);

  InverseFFT(reals, imags, buf_size);
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    const double expect = (idx == 0 ? 123.0 : 0.0);
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;

    EXPECT_LE(imags[idx], epsilon) << idx;
    EXPECT_GE(imags[idx], -epsilon) << idx;
  }

  // DC
  OverwriteCosine(reals, buf_size, 0.0, 0.0);
  reals[0] = 4321.0 * buf_size;
  OverwriteCosine(imags, buf_size, 0.0, 0.0);

  InverseFFT(reals, imags, buf_size);
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    const double expect = 4321.0;
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;
  }

  // folding freq
  OverwriteCosine(reals, buf_size, 0.0, 0.0);
  reals[buf_sz_2] = 10203.0 * buf_size;
  OverwriteCosine(imags, buf_size, 0.0, 0.0);

  InverseFFT(reals, imags, buf_size);
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    const double expect = (idx % 2 == 0 ? 10203.0 : -10203.0);
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;
  }

  // freq 1
  OverwriteCosine(reals, buf_size, 0.0, 0.0);
  reals[1] = 20202020.0 * buf_size;
  OverwriteCosine(imags, buf_size, 0.0, 0.0);

  OverwriteCosine(expects, buf_size, 1.0, 20202020.0);
  InverseFFT(reals, imags, buf_size);
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    EXPECT_LE(reals[idx], expects[idx] + epsilon) << idx;
    EXPECT_GE(reals[idx], expects[idx] - epsilon) << idx;
  }

  // freq 1, with 3PI/4 phase
  OverwriteCosine(reals, buf_size, 0.0, 0.0);
  reals[1] = -20202020.0 / RT_2 * buf_size;
  OverwriteCosine(imags, buf_size, 0.0, 0.0);
  imags[1] = 20202020.0 / RT_2 * buf_size;

  OverwriteCosine(expects, buf_size, 1.0, 20202020.0, 3.0 * M_PI / 4.0);
  InverseFFT(reals, imags, buf_size);
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    EXPECT_LE(reals[idx], expects[idx] + epsilon) << idx;
    EXPECT_GE(reals[idx], expects[idx] - epsilon) << idx;
  }
}

// MeasureAudioFreq function accepts buffer of audio data, length and the
// frequency at which to analyze audio. It returns magnitude of signal at that
// frequency, and combined (root-sum-square) magnitude of all OTHER frequencies.
// For inputs of magnitude 3 and 4, their combination equals 5.
TEST(AnalysisHelpers, MeasureAudioFreq_32) {
  int32_t reals[] = {5, -3, 13, -3};  // cos freq 0,1,2; mag 3,4,6; phase 0,pi,0
  double magn_signal = -54.32;        // will be overwritten
  double magn_other = 42.0;           // will be overwritten

  MeasureAudioFreq(reals, fbl::count_of(reals), 0, &magn_signal);
  EXPECT_EQ(3.0, magn_signal);

  MeasureAudioFreq(reals, fbl::count_of(reals), 1, &magn_signal, &magn_other);
  EXPECT_EQ(4.0, magn_signal);

  MeasureAudioFreq(reals, fbl::count_of(reals), 2, &magn_signal, &magn_other);
  EXPECT_EQ(6.0, magn_signal);
  EXPECT_EQ(5.0, magn_other);
}

// Test float-based MeasureAudioFreq (only needed to validate OutputProducer).
// reals[] consists of cosines with freq 0,1,2; magnitude 3,4,6; phase 0,pi,pi.
TEST(AnalysisHelpers, MeasureAudioFreq_Float) {
  float reals[] = {-7.0f, 9.0f, 1.0f, 9.0f};
  double magn_signal = -54.32;
  double magn_other = 42.0;

  MeasureAudioFreq(reals, fbl::count_of(reals), 0, &magn_signal);
  EXPECT_EQ(3.0, magn_signal);

  MeasureAudioFreq(reals, fbl::count_of(reals), 1, &magn_signal, &magn_other);
  EXPECT_EQ(4.0, magn_signal);

  MeasureAudioFreq(reals, fbl::count_of(reals), 2, &magn_signal, &magn_other);
  EXPECT_EQ(6.0, magn_signal);  // Magnitude is absolute value (ignore phase)
  EXPECT_EQ(5.0, magn_other);
}

}  // namespace media::audio::test
