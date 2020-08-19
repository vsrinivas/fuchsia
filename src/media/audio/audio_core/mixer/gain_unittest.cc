// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/gain.h"

#include <iterator>

#include <fbl/algorithm.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using testing::Each;
using testing::FloatEq;
using testing::Not;
using testing::Pointwise;

namespace media::audio::test {

TEST(StaticGainTest, CombineGains) {
  static_assert(-90.0 < Gain::kMinGainDb / 2);
  static_assert(15.0 > Gain::kMaxGainDb / 2);

  EXPECT_EQ(Gain::CombineGains(-90, -90), Gain::kMinGainDb);
  EXPECT_EQ(Gain::CombineGains(15, 15), Gain::kMaxGainDb);
  EXPECT_EQ(Gain::CombineGains(-20, 5), -15);
}

// Gain tests - how does the Gain object respond when given values close to its
// maximum or minimum; does it correctly cache; do values combine to form Unity
// gain. Is data scaling accurately performed, and is it adequately linear? Do
// our gains and accumulators behave as expected when they overflow?
//
// Gain tests using AScale and the Gain object only
//
class GainTest : public testing::Test {
 protected:
  void SetUp() override {
    testing::Test::SetUp();
    rate_1khz_output_ = TimelineRate(1000, ZX_SEC(1));
  }

  void TestUnityGain(float source_gain_db, float dest_gain_db) {
    gain_.SetSourceGain(source_gain_db);
    gain_.SetDestGain(dest_gain_db);
    EXPECT_FLOAT_EQ(Gain::kUnityScale, gain_.GetGainScale());

    EXPECT_FALSE(gain_.IsSilent());
    EXPECT_TRUE(gain_.IsUnity());
  }

  void TestMinMuteGain(float source_gain_db, float dest_gain_db) {
    gain_.SetSourceGain(source_gain_db);
    gain_.SetDestGain(dest_gain_db);

    EXPECT_FLOAT_EQ(Gain::kMuteScale, gain_.GetGainScale());

    EXPECT_FALSE(gain_.IsUnity());
    EXPECT_TRUE(gain_.IsSilent());
  }

  // Used for debugging purposes.
  static void DisplayScaleVals(const Gain::AScale* scale_arr, uint32_t buf_size) {
    printf("\n    ********************************************************");
    printf("\n **************************************************************");
    printf("\n ***    Displaying raw scale array data for length %5d    ***", buf_size);
    printf("\n **************************************************************");
    for (uint32_t idx = 0; idx < buf_size; ++idx) {
      if (idx % 10 == 0) {
        printf("\n [%d]  ", idx);
      }
      printf("%.7f   ", scale_arr[idx]);
    }
    printf("\n **************************************************************");
    printf("\n    ********************************************************");
    printf("\n");
  }

  Gain gain_;
  TimelineRate rate_1khz_output_;
};

class RampTest : public GainTest {};
class ScaleArrayTest : public GainTest {};

TEST_F(GainTest, Defaults) {
  EXPECT_FLOAT_EQ(gain_.GetGainScale(), Gain::kUnityScale);
  EXPECT_TRUE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsRamping());
}

// Test the internally-used inline func that converts AScale gain to dB.
TEST_F(GainTest, GainScaleToDb) {
  // Unity scale is 0.0dB (no change).
  EXPECT_FLOAT_EQ(Gain::ScaleToDb(Gain::kUnityScale), Gain::kUnityGainDb);

  // 10x scale-up in amplitude (by definition) is exactly +20.0dB.
  EXPECT_FLOAT_EQ(Gain::ScaleToDb(Gain::kUnityScale * 10.0f), 20.0f);

  // 1/100x scale-down in amplitude (by definition) is exactly -40.0dB.
  EXPECT_FLOAT_EQ(Gain::ScaleToDb(Gain::kUnityScale * 0.01f), -40.0f);

  // 1/2x scale-down by calculation: -6.020600... dB.
  const float half_scale = -6.0206001f;
  EXPECT_FLOAT_EQ(half_scale, Gain::ScaleToDb(Gain::kUnityScale * 0.5f));
}

// Test the inline function that converts a numerical value to dB.
TEST_F(GainTest, DoubleToDb) {
  EXPECT_DOUBLE_EQ(Gain::DoubleToDb(Gain::kUnityScale), 0.0);  // Unity is 0 dB
  EXPECT_DOUBLE_EQ(Gain::DoubleToDb(Gain::kUnityScale * 100.0),
                   40.0);                                              // 100x is 40 dB
  EXPECT_DOUBLE_EQ(Gain::DoubleToDb(Gain::kUnityScale * 0.1), -20.0);  // 10% is -20 dB

  EXPECT_GE(Gain::DoubleToDb(Gain::kUnityScale * 0.5),
            -6.0206 * 1.000001);  // 50% is roughly -6.0206 dB
  EXPECT_LE(Gain::DoubleToDb(Gain::kUnityScale * 0.5),
            -6.0206 * 0.999999);  // FP representation => 2 comps
}

// Do source and destination gains correctly combine to produce unity scaling?
TEST_F(GainTest, Unity) {
  TestUnityGain(Gain::kUnityGainDb, Gain::kUnityGainDb);

  // These positive/negative values should sum to 0.0: UNITY
  TestUnityGain(Gain::kMaxGainDb / 2, -Gain::kMaxGainDb / 2);
  TestUnityGain(-Gain::kMaxGainDb, Gain::kMaxGainDb);
}

// Gain caches any previously set source gain, using it if needed.
// This verifies the default and caching behavior of the Gain object
TEST_F(GainTest, SourceGainCaching) {
  Gain expect_gain;
  Gain::AScale amplitude_scale, expect_amplitude_scale;

  // Set expect_amplitude_scale to a value that represents -6.0 dB.
  expect_gain.SetSourceGain(-6.0f);
  expect_amplitude_scale = expect_gain.GetGainScale();

  // If Render gain defaults to 0.0, this represents -6.0 dB too.
  gain_.SetSourceGain(0.0f);
  gain_.SetDestGain(-6.0f);
  amplitude_scale = gain_.GetGainScale();
  EXPECT_FLOAT_EQ(expect_amplitude_scale, amplitude_scale);

  // Now set a different renderer gain that will be cached (+3.0).
  gain_.SetSourceGain(3.0f);
  gain_.SetDestGain(-3.0f);
  amplitude_scale = gain_.GetGainScale();
  EXPECT_FLOAT_EQ(Gain::kUnityScale, amplitude_scale);

  // If Render gain is cached val of +3, then combo should be Unity.
  gain_.SetDestGain(-3.0f);
  amplitude_scale = gain_.GetGainScale();
  EXPECT_FLOAT_EQ(Gain::kUnityScale, amplitude_scale);

  // Try another Output gain; with cached +3 this should equate to -6dB.
  gain_.SetDestGain(-9.0f);
  EXPECT_FLOAT_EQ(expect_amplitude_scale, gain_.GetGainScale());

  // Render gain cached +3 and Output gain non-cached -3 should lead to Unity.
  gain_.SetDestGain(-3.0f);
  EXPECT_FLOAT_EQ(Gain::kUnityScale, gain_.GetGainScale());
}

// System independently limits stream and master/device Gains to kMinGainDb
// (-160dB). Assert scale is zero, if either (or combo) are kMinGainDb or less.
TEST_F(GainTest, MinMute) {
  // First, test for source/dest interactions.
  // if OutputGain <= kMinGainDb, scale must be 0, regardless of renderer gain.
  TestMinMuteGain(-2 * Gain::kMinGainDb, Gain::kMinGainDb);

  // if renderer gain <= kMinGainDb, scale must be 0, regardless of Output gain.
  TestMinMuteGain(Gain::kMinGainDb, Gain::kMaxGainDb * 1.2);

  // if sum of renderer gain and Output gain <= kMinGainDb, scale should be 0.
  // Output gain is just slightly above MinGain; renderer takes us below it.
  TestMinMuteGain(-2.0f, Gain::kMinGainDb + 1.0f);

  // Next, test for source/dest interactions.
  // Check if source alone mutes.
  TestMinMuteGain(Gain::kMinGainDb, Gain::kUnityGainDb);
  TestMinMuteGain(Gain::kMinGainDb, Gain::kUnityGainDb + 1);
  // Check if dest alone mutes.
  TestMinMuteGain(Gain::kUnityGainDb + 1, Gain::kMinGainDb);
  TestMinMuteGain(Gain::kUnityGainDb, Gain::kMinGainDb);

  // Check if the combination mutes.
  TestMinMuteGain(Gain::kMinGainDb / 2, Gain::kMinGainDb / 2);
}

// Ramp-related tests
//
// Setting a ramp with zero duration is the same as an immediate gain change.
TEST_F(RampTest, SetRampWithNoDurationChangesGain) {
  gain_.SetSourceGain(-11.0f);
  gain_.SetDestGain(-1.0f);

  gain_.SetSourceGainWithRamp(+1.0f, zx::nsec(0));
  EXPECT_TRUE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());
}

// Setting a ramp with non-zero duration does not take effect until Advance.
TEST_F(RampTest, SetRampWithDurationDoesntChangeGain) {
  gain_.SetSourceGain(24.0f);
  gain_.SetDestGain(-24.0f);

  gain_.SetSourceGainWithRamp(Gain::kMinGainDb, zx::nsec(1));

  // Expect pre-ramp conditions
  EXPECT_TRUE(gain_.GetGainScale() == Gain::kUnityScale);
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
}

// If a ramp-up is active/pending, then IsSilent should not be true.
TEST_F(RampTest, RampingUpIsNeverSilent) {
  gain_.SetSourceGain(-150.0f);
  gain_.SetDestGain(-22.0f);

  gain_.SetSourceGainWithRamp(+22.0f, zx::sec(1));

  // Expect pre-ramp conditions
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
}

// If we are ramping-down and already silent, IsSilent should remain true.
TEST_F(RampTest, SilentAndRampingDownIsSilent) {
  gain_.SetDestGain(-160.0f);
  gain_.SetSourceGainWithRamp(-1.0f, zx::sec(1));

  // Expect pre-ramp conditions
  EXPECT_TRUE(gain_.IsSilent());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());
}

// If a ramp is active/pending, then IsUnity should never be true.
TEST_F(RampTest, RampingIsNeverUnity) {
  gain_.SetSourceGain(Gain::kUnityGainDb);
  gain_.SetDestGain(Gain::kUnityGainDb);
  EXPECT_TRUE(gain_.IsUnity());

  gain_.SetSourceGainWithRamp(-1.0f, zx::sec(1));

  // Expect pre-ramp conditions
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
}

// If the beginning and end of a ramp are the same, it isn't ramping.
TEST_F(RampTest, FlatIsntRamping) {
  gain_.SetSourceGain(Gain::kUnityGainDb);
  gain_.SetDestGain(-20.0f);

  gain_.SetSourceGainWithRamp(0.0f, zx::sec(1));

  // Expect pre-ramp conditions
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsRamping());
}

// Upon Advance, we should see a change in the instantaneous GetGainScale().
TEST_F(RampTest, AdvanceChangesGain) {
  gain_.SetSourceGain(-150.0f);
  gain_.SetDestGain(-13.0f);

  gain_.SetSourceGainWithRamp(+13.0f, zx::nsec(1));

  // Advance far beyond end of ramp -- 10 msec (10 frames@1kHz) vs. 1 nsec.
  gain_.Advance(10, rate_1khz_output_);

  // Expect post-ramp conditions
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_TRUE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsRamping());
}

// ScaleArray-related tests
//
// If no ramp, all vals returned by GetScaleArray should equal GetGainScale().
TEST_F(ScaleArrayTest, GetScaleArrayNoRampEqualsGetScale) {
  Gain::AScale scale_arr[3];
  gain_.SetDestGain(-42.0f);
  gain_.SetSourceGain(-68.0f);

  gain_.GetScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);
  Gain::AScale expect_scale = gain_.GetGainScale();

  EXPECT_THAT(scale_arr, Each(FloatEq(expect_scale)));

  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());
}

// Validate when ramp and GetScaleArray are identical length.
TEST_F(ScaleArrayTest, GetScaleArrayRamp) {
  Gain::AScale scale_arr[6];
  Gain::AScale expect_arr[6] = {1.0, 0.82, 0.64, 0.46, 0.28, 0.1};

  gain_.SetSourceGainWithRamp(-20, zx::msec(5));
  gain_.GetScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  EXPECT_THAT(scale_arr, Pointwise(FloatEq(), expect_arr));

  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());
}

// Validate when ramp duration is greater than GetScaleArray.
TEST_F(ScaleArrayTest, GetScaleArrayLongRamp) {
  Gain::AScale scale_arr[4];  // At 1kHz this is less than the ramp duration.
  Gain::AScale expect_arr[4] = {1.0, 0.901, 0.802, 0.703};

  gain_.SetSourceGainWithRamp(-40, zx::msec(10));
  gain_.GetScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  EXPECT_THAT(scale_arr, Pointwise(FloatEq(), expect_arr));

  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());
}

// Validate when ramp duration is shorter than GetScaleArray.
TEST_F(ScaleArrayTest, GetScaleArrayShortRamp) {
  Gain::AScale scale_arr[9];  // At 1kHz this is longer than the ramp duration.
  Gain::AScale expect_arr[9] = {1.0, 0.82, 0.64, 0.46, 0.28, 0.1, 0.1, 0.1, 0.1};

  gain_.SetSourceGainWithRamp(-20, zx::msec(5));
  gain_.GetScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  EXPECT_THAT(scale_arr, Pointwise(FloatEq(), expect_arr));

  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());
}

// Successive GetScaleArray calls without Advance should return same results.
TEST_F(ScaleArrayTest, GetScaleArrayWithoutAdvance) {
  gain_.SetSourceGainWithRamp(-123.45678, zx::msec(9));

  Gain::AScale scale_arr[10];
  gain_.GetScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  Gain::AScale scale_arr2[10];
  gain_.GetScaleArray(scale_arr2, std::size(scale_arr2), rate_1khz_output_);

  EXPECT_THAT(scale_arr, Pointwise(FloatEq(), scale_arr2));
}

// Advances that exceed ramp durations should lead to end-to-ramp conditions.
TEST_F(ScaleArrayTest, GetScaleArrayBigAdvance) {
  Gain::AScale scale_arr[6];
  Gain::AScale expect = Gain::kUnityScale * 2;

  gain_.SetSourceGainWithRamp(6.0205999, zx::msec(5));
  gain_.GetScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  EXPECT_THAT(scale_arr, Not(Each(FloatEq(expect))));
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());

  gain_.Advance(rate_1khz_output_.Scale(ZX_SEC(10)), rate_1khz_output_);
  gain_.GetScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  EXPECT_THAT(scale_arr, Each(FloatEq(expect)));
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());
}

// Completing a ramp should fast-forward any in-process ramps.
TEST_F(ScaleArrayTest, CompleteSourceRamp) {
  Gain::AScale scale_arr[6];
  Gain::AScale scale_arr2[6];

  constexpr float target_gain_db = -30.1029995;
  const float target_gain_scale = Gain::DbToScale(target_gain_db);

  // With a 5ms duration and 1 frame per ms, scale_arr will perfectly fit
  // each frame such that scale_arr[5] == target_gain_scale.
  gain_.SetSourceGainWithRamp(target_gain_db, zx::msec(5));
  gain_.GetScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  for (size_t k = 0; k < std::size(scale_arr); k++) {
    const float diff = Gain::kUnityScale - target_gain_scale;
    const float want = Gain::kUnityScale - diff * static_cast<float>(k) / 5.0;
    EXPECT_FLOAT_EQ(want, scale_arr[k]) << "index " << k;
  }
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_EQ(Gain::kUnityGainDb, gain_.GetGainDb());
  EXPECT_EQ(Gain::kUnityScale, gain_.GetGainScale());

  // After clearing the ramp, scale_arr should be constant.
  gain_.CompleteSourceRamp();
  gain_.GetScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  EXPECT_THAT(scale_arr, Each(FloatEq(target_gain_scale)));
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_EQ(target_gain_db, gain_.GetGainDb());
  EXPECT_EQ(target_gain_scale, gain_.GetGainScale());

  // Without a ramp, scale_arr should be constant even after Advance.
  gain_.Advance(10, rate_1khz_output_);
  gain_.GetScaleArray(scale_arr2, std::size(scale_arr2), rate_1khz_output_);

  EXPECT_THAT(scale_arr, Each(FloatEq(target_gain_scale)));
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_EQ(target_gain_db, gain_.GetGainDb());
  EXPECT_EQ(target_gain_scale, gain_.GetGainScale());
}

// After partial Advance through a ramp, instantaneous gain should be accurate.
TEST_F(ScaleArrayTest, AdvanceHalfwayThroughRamp) {
  Gain::AScale scale_arr[4];  // At 1kHz this is less than the ramp duration.
  Gain::AScale expect_arr[4];

  gain_.SetSourceGainWithRamp(-20.0f, zx::msec(9));
  gain_.GetScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  Gain::AScale expect_scale = Gain::kUnityScale;
  EXPECT_FLOAT_EQ(gain_.GetGainScale(), expect_scale);

  // When comparing buffers, do it within the tolerance of 32-bit float
  for (auto& val : expect_arr) {
    val = expect_scale;
    expect_scale -= 0.1;
  }
  EXPECT_THAT(scale_arr, Pointwise(FloatEq(), expect_arr));
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());

  // Advance only partially through the duration of the ramp.
  const uint32_t kFramesToAdvance = 2;
  gain_.Advance(kFramesToAdvance, rate_1khz_output_);
  gain_.GetScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);
  // DisplayScaleVals(scale_arr, std::size(scale_arr));

  expect_scale = expect_arr[kFramesToAdvance];
  EXPECT_FLOAT_EQ(expect_scale, gain_.GetGainScale());

  for (auto& val : expect_arr) {
    val = expect_scale;
    expect_scale -= 0.1;
  }
  EXPECT_THAT(scale_arr, Pointwise(FloatEq(), expect_arr));
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsSilent());
}

// After partial Advance through a ramp, followed by a second ramp, the second ramp
// ramp should start where the first ramp left off.
TEST_F(ScaleArrayTest, TwoRamps) {
  gain_.SetSourceGainWithRamp(-20.0f, zx::msec(10));

  auto scale_start = Gain::kUnityScale;
  EXPECT_FLOAT_EQ(scale_start, gain_.GetGainScale());
  EXPECT_TRUE(gain_.IsRamping());

  // Advance only partially through the duration of the ramp.
  gain_.Advance(2, rate_1khz_output_); // 1 frame == 1ms

  auto expect_scale = scale_start + (Gain::DbToScale(-20.f) - scale_start) * 2.0/10.0;
  EXPECT_FLOAT_EQ(expect_scale, gain_.GetGainScale());
  EXPECT_TRUE(gain_.IsRamping());

  // A new ramp should start at the same spot.
  gain_.SetSourceGainWithRamp(-80.0f, zx::msec(10));

  scale_start = expect_scale;
  EXPECT_FLOAT_EQ(expect_scale, gain_.GetGainScale());
  EXPECT_TRUE(gain_.IsRamping());

  // Advance again.
  gain_.Advance(2, rate_1khz_output_);

  expect_scale = scale_start + (Gain::DbToScale(-80.f) - scale_start) * 2.0/10.0;
  EXPECT_FLOAT_EQ(expect_scale, gain_.GetGainScale());
  EXPECT_TRUE(gain_.IsRamping());
}

}  // namespace media::audio::test
