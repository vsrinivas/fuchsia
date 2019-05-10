// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>

#include "src/media/audio/audio_core/mixer/test/audio_result.h"
#include "src/media/audio/audio_core/mixer/test/mixer_tests_shared.h"

namespace media::audio::test {

// Convenience abbreviation within this source file to shorten names
using Resampler = media::audio::Mixer::Resampler;

//
// Gain tests - how does the Gain object respond when given values close to its
// maximum or minimum; does it correctly cache; do values combine to form Unity
// gain. Is data scaling accurately performed, and is it adequately linear? Do
// our gains and accumulators behave as expected when they overflow?
//
// Gain tests using AScale and the Gain object only
//
class GainTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ::testing::Test::SetUp();

    rate_1khz_output_ = TimelineRate(1000, ZX_SEC(1));
  }

  void TestUnityGain(float source_gain_db, float dest_gain_db) {
    gain_.SetSourceGain(source_gain_db);
    EXPECT_EQ(Gain::kUnityScale, gain_.GetGainScale(dest_gain_db));

    gain_.SetDestGain(dest_gain_db);
    EXPECT_FALSE(gain_.IsSilent());
    EXPECT_TRUE(gain_.IsUnity());
  }

  void TestMinMuteGain(float source_gain_db, float dest_gain_db) {
    gain_.SetSourceGain(source_gain_db);
    EXPECT_EQ(Gain::kMuteScale, gain_.GetGainScale(dest_gain_db));

    gain_.SetDestGain(dest_gain_db);
    EXPECT_EQ(Gain::kMuteScale, gain_.GetGainScale());
    EXPECT_FALSE(gain_.IsUnity());
    EXPECT_TRUE(gain_.IsSilent());
  }

  // Used for debugging purposes.
  static void DisplayScaleVals(const Gain::AScale* scale_arr,
                               uint32_t buf_size) {
    printf("\n    ********************************************************");
    printf("\n **************************************************************");
    printf("\n ***    Displaying raw scale array data for length %5d    ***",
           buf_size);
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

class MuteTest : public GainTest {};
class RampTest : public GainTest {};
class ScaleArrayTest : public GainTest {};

TEST_F(GainTest, Defaults) {
  EXPECT_EQ(gain_.GetGainScale(), Gain::kUnityScale);
  EXPECT_TRUE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsRamping());
}

// Test the internally-used inline func that converts AScale gain to dB.
TEST_F(GainTest, GainScaleToDb) {
  // Unity scale is 0.0dB (no change).
  EXPECT_EQ(Gain::ScaleToDb(Gain::kUnityScale), Gain::kUnityGainDb);

  // 10x scale-up in amplitude (by definition) is exactly +20.0dB.
  EXPECT_EQ(Gain::ScaleToDb(Gain::kUnityScale * 10.0f), 20.0f);

  // 1/100x scale-down in amplitude (by definition) is exactly -40.0dB.
  EXPECT_EQ(Gain::ScaleToDb(Gain::kUnityScale * 0.01f), -40.0f);

  // 1/2x scale-down by calculation: -6.020600... dB.
  const float half_scale = -6.0206001f;
  // Because of float imprecision, use our Compare...() with float tolerance.
  EXPECT_TRUE(CompareBufferToVal(
      &half_scale, Gain::ScaleToDb(Gain::kUnityScale * 0.5f), 1, true, true));
}

// Test the inline function that converts a numerical value to dB.
TEST_F(GainTest, DoubleToDb) {
  EXPECT_EQ(Gain::DoubleToDb(Gain::kUnityScale), 0.0);  // Unity is 0 dB
  EXPECT_EQ(Gain::DoubleToDb(Gain::kUnityScale * 100.0),
            40.0);                                              // 100x is 40 dB
  EXPECT_EQ(Gain::DoubleToDb(Gain::kUnityScale * 0.1), -20.0);  // 10% is -20 dB

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
  expect_gain.SetSourceGain(6.0f);
  expect_amplitude_scale = expect_gain.GetGainScale(-12.0f);

  // If Render gain defaults to 0.0, this represents -6.0 dB too.
  amplitude_scale = gain_.GetGainScale(-6.0f);
  EXPECT_EQ(expect_amplitude_scale, amplitude_scale);

  // Now set a different renderer gain that will be cached (+3.0).
  gain_.SetSourceGain(3.0f);
  amplitude_scale = gain_.GetGainScale(-3.0f);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);

  // If Render gain is cached val of +3, then combo should be Unity.
  amplitude_scale = gain_.GetGainScale(-3.0f);
  EXPECT_EQ(Gain::kUnityScale, amplitude_scale);

  // Try another Output gain; with cached +3 this should equate to -6dB.
  gain_.SetDestGain(-9.0f);
  EXPECT_EQ(expect_amplitude_scale, gain_.GetGainScale());

  // Render gain cached +3 and Output gain non-cached -3 should lead to Unity.
  EXPECT_EQ(Gain::kUnityScale, gain_.GetGainScale(-3.0f));

  // Cached Output gain should still be -9, leading to -6dB.
  EXPECT_EQ(expect_amplitude_scale, gain_.GetGainScale());
}

// We independently limit stream and device gains to kMaxGainDb/0, respectively.
// MTWN-70 concerns Gain's statefulness. Does it need this complexity?
TEST_F(GainTest, MaxClamp) {
  // Renderer Gain of 2 * kMaxGainDb is clamped to kMaxGainDb (+24 dB).
  gain_.SetSourceGain(Gain::kMaxGainDb * 2);
  EXPECT_EQ(Gain::kMaxScale, gain_.GetGainScale(Gain::kUnityGainDb));

  // This combination (24.05 dB) is clamped to 24.0dB.
  gain_.SetSourceGain(Gain::kMaxGainDb);
  EXPECT_EQ(Gain::kMaxScale, gain_.GetGainScale(0.05f));

  // System limits renderer gain to kMaxGainDb, even when sum is less than 0.
  // Renderer Gain +36dB (clamped to +24dB) plus system Gain -48dB ==> -24dB.
  constexpr float kScale24DbDown = 0.0630957344f;
  gain_.SetSourceGain(Gain::kMaxGainDb * 1.5f);
  gain_.SetDestGain(-2 * Gain::kMaxGainDb);
  EXPECT_EQ(kScale24DbDown, gain_.GetGainScale());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsSilent());

  // AudioCore limits master to 0dB, but Gain object handles up to kMaxGainDb.
  // Dest also clamps to +24dB: source(-48dB) + dest(+36dB=>24dB) becomes -24dB.
  gain_.SetSourceGain(-2 * Gain::kMaxGainDb);
  gain_.SetDestGain(Gain::kMaxGainDb * 1.5f);
  EXPECT_EQ(kScale24DbDown, gain_.GetGainScale());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsSilent());
}

// System independently limits stream and master/device Gains to kMinGainDb
// (-160dB). Assert scale is zero, if either (or combo) are kMinGainDb or less.
TEST_F(GainTest, MinMute) {
  // if OutputGain <= kMinGainDb, scale must be 0, regardless of renderer gain.
  TestMinMuteGain(-2 * Gain::kMinGainDb, Gain::kMinGainDb);

  // if renderer gain <= kMinGainDb, scale must be 0, regardless of Output gain.
  TestMinMuteGain(Gain::kMinGainDb, Gain::kMaxGainDb * 1.2);

  // if sum of renderer gain and Output gain <= kMinGainDb, scale should be 0.
  // Output gain is just slightly above MinGain; renderer takes us below it.
  TestMinMuteGain(-2.0f, Gain::kMinGainDb + 1.0f);
}

// Mute-related tests
//
// These tests use SetMute itself (as opposed to Gain tests that use gain values
// that exceed our lower limit and hence produce silence).
//
TEST_F(MuteTest, SourceGainThenMute) {
  gain_.SetSourceGain(Gain::kMaxGainDb);
  EXPECT_GT(gain_.GetGainScale(), Gain::kUnityScale);
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsSilent());

  gain_.SetSourceMute(true);
  EXPECT_EQ(gain_.GetGainScale(), Gain::kMuteScale);
  EXPECT_TRUE(gain_.IsSilent());

  gain_.SetSourceMute(false);
  EXPECT_GT(gain_.GetGainScale(), Gain::kUnityScale);
  EXPECT_FALSE(gain_.IsSilent());

  gain_.SetDestMute(true);
  EXPECT_EQ(gain_.GetGainScale(), Gain::kMuteScale);
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsSilent());

  gain_.SetDestMute(false);
  EXPECT_GT(gain_.GetGainScale(), Gain::kUnityScale);
  EXPECT_FALSE(gain_.IsSilent());
}

TEST_F(MuteTest, DestGainThenMute) {
  gain_.SetDestGain(Gain::kMaxGainDb);
  EXPECT_GT(gain_.GetGainScale(), Gain::kUnityScale);
  EXPECT_FALSE(gain_.IsSilent());

  gain_.SetSourceMute(true);
  EXPECT_EQ(gain_.GetGainScale(), Gain::kMuteScale);
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsSilent());

  gain_.SetDestMute(true);
  EXPECT_EQ(gain_.GetGainScale(), Gain::kMuteScale);
  EXPECT_TRUE(gain_.IsSilent());

  gain_.SetSourceMute(false);
  EXPECT_EQ(gain_.GetGainScale(), Gain::kMuteScale);
  EXPECT_TRUE(gain_.IsSilent());

  gain_.SetDestMute(false);
  EXPECT_GT(gain_.GetGainScale(), Gain::kUnityScale);
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsSilent());
}

TEST_F(MuteTest, SourceMuteThenGain) {
  gain_.SetSourceMute(true);
  EXPECT_EQ(gain_.GetGainScale(), Gain::kMuteScale);
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsSilent());

  gain_.SetDestGain(Gain::kMaxGainDb);
  EXPECT_TRUE(gain_.IsSilent());

  gain_.SetSourceGain(Gain::kMinGainDb);
  EXPECT_TRUE(gain_.IsSilent());

  gain_.SetSourceGain(Gain::kUnityGainDb);
  EXPECT_EQ(gain_.GetGainScale(), Gain::kMuteScale);
  EXPECT_TRUE(gain_.IsSilent());
}

TEST_F(MuteTest, DestMuteThenGain) {
  gain_.SetDestMute(true);
  EXPECT_EQ(gain_.GetGainScale(), Gain::kMuteScale);
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsSilent());

  gain_.SetDestGain(Gain::kMaxGainDb);
  EXPECT_TRUE(gain_.IsSilent());

  gain_.SetSourceGain(Gain::kMinGainDb);
  EXPECT_TRUE(gain_.IsSilent());

  gain_.SetSourceGain(Gain::kUnityGainDb);
  EXPECT_EQ(gain_.GetGainScale(), Gain::kMuteScale);
  EXPECT_TRUE(gain_.IsSilent());
}

// Ramp-related tests
//
// Setting a ramp with zero duration is the same as an immediate gain change.
TEST_F(RampTest, SetRampWithNoDurationChangesGain) {
  gain_.SetSourceGain(-11.0f);
  gain_.SetDestGain(-1.0f);

  gain_.SetSourceGainWithRamp(+1.0f, 0);
  EXPECT_TRUE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());
}

// Setting a ramp with non-zero duration does not take effect until Advance.
TEST_F(RampTest, SetRampWithDurationDoesntChangeGain) {
  gain_.SetSourceGain(24.0f);
  gain_.SetDestGain(-24.0f);

  gain_.SetSourceGainWithRamp(Gain::kMinGainDb, 1);

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

  gain_.SetSourceGainWithRamp(+22.0f, ZX_SEC(1));

  // Expect pre-ramp conditions
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
}

// If we are ramping-down and already silent, IsSilent should remain true.
TEST_F(RampTest, SilentAndRampingDownIsSilent) {
  gain_.SetDestGain(-160.0f);
  gain_.SetSourceGainWithRamp(-1.0f, ZX_SEC(1));

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

  gain_.SetSourceGainWithRamp(-1.0f, ZX_SEC(1));

  // Expect pre-ramp conditions
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
}

// If the beginning and end of a ramp are the same, it isn't ramping.
TEST_F(RampTest, FlatIsntRamping) {
  gain_.SetSourceGain(Gain::kUnityGainDb);
  gain_.SetDestGain(-20.0f);

  gain_.SetSourceGainWithRamp(0.0f, ZX_SEC(1));

  // Expect pre-ramp conditions
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsRamping());
}

// Upon Advance, we should see a change in the instantaneous GetGainScale().
TEST_F(RampTest, AdvanceChangesGain) {
  gain_.SetSourceGain(-150.0f);
  gain_.SetDestGain(-13.0f);

  gain_.SetSourceGainWithRamp(+13.0f, 1);

  // Advance far beyond end of ramp -- 10 msec (10 frames@1kHz) vs. 1 nsec.
  gain_.Advance(10, rate_1khz_output_);

  // Expect post-ramp conditions
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_TRUE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsRamping());
}

TEST_F(RampTest, SourceMuteRampIsRampingButSilent) {
  gain_.SetSourceMute(true);
  EXPECT_FALSE(gain_.IsRamping());

  gain_.SetSourceGainWithRamp(-20.0f, ZX_MSEC(9));
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsSilent());
}

TEST_F(RampTest, DestMuteRampIsRampingButSilent) {
  gain_.SetDestMute(true);
  gain_.SetSourceGainWithRamp(10.0f, ZX_MSEC(5));
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsSilent());
}

TEST_F(RampTest, RampSourceMuteIsRampingButSilent) {
  gain_.SetSourceGainWithRamp(-20.0f, ZX_MSEC(9));
  gain_.SetSourceMute(true);

  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsSilent());
}

TEST_F(RampTest, RampDestMuteIsRampingButSilent) {
  gain_.SetSourceGainWithRamp(-20.0f, ZX_MSEC(9));
  gain_.SetDestMute(true);

  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsSilent());
}

// ScaleArray-related tests
//
// If no ramp, all vals returned by GetScaleArray should equal GetGainScale().
TEST_F(ScaleArrayTest, GetScaleArrayNoRampEqualsGetScale) {
  Gain::AScale scale_arr[3];
  gain_.SetDestGain(-42.0f);
  gain_.SetSourceGain(-68.0f);

  gain_.GetScaleArray(scale_arr, fbl::count_of(scale_arr), rate_1khz_output_);
  Gain::AScale expect_scale = gain_.GetGainScale();
  EXPECT_TRUE(
      CompareBufferToVal(scale_arr, expect_scale, fbl::count_of(scale_arr)));

  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());
}

// Validate when ramp and GetScaleArray are identical length.
TEST_F(ScaleArrayTest, GetScaleArrayRamp) {
  Gain::AScale scale_arr[6];
  Gain::AScale expect_arr[6] = {1.0, 0.82, 0.64, 0.46, 0.28, 0.1};

  gain_.SetSourceGainWithRamp(-20, ZX_MSEC(5));
  gain_.GetScaleArray(scale_arr, fbl::count_of(scale_arr), rate_1khz_output_);

  // When comparing buffers, do it within the tolerance of 32-bit float
  EXPECT_TRUE(CompareBuffers(scale_arr, expect_arr, fbl::count_of(scale_arr),
                             true, true));

  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());
}

// Validate when ramp duration is greater than GetScaleArray.
TEST_F(ScaleArrayTest, GetScaleArrayLongRamp) {
  Gain::AScale scale_arr[4];  // At 1kHz this is less than the ramp duration.
  Gain::AScale expect_arr[4] = {1.0, 0.901, 0.802, 0.703};

  gain_.SetSourceGainWithRamp(-40, ZX_MSEC(10));
  gain_.GetScaleArray(scale_arr, fbl::count_of(scale_arr), rate_1khz_output_);

  // When comparing buffers, do it within the tolerance of 32-bit float
  EXPECT_TRUE(CompareBuffers(scale_arr, expect_arr, fbl::count_of(scale_arr),
                             true, true));

  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());
}

// Validate when ramp duration is shorter than GetScaleArray.
TEST_F(ScaleArrayTest, GetScaleArrayShortRamp) {
  Gain::AScale scale_arr[9];  // At 1kHz this is longer than the ramp duration.
  Gain::AScale expect_arr[9] = {1.0, 0.82, 0.64, 0.46, 0.28,
                                0.1, 0.1,  0.1,  0.1};

  gain_.SetSourceGainWithRamp(-20, ZX_MSEC(5));
  gain_.GetScaleArray(scale_arr, fbl::count_of(scale_arr), rate_1khz_output_);

  // When comparing buffers, do it within the tolerance of 32-bit float
  EXPECT_TRUE(CompareBuffers(scale_arr, expect_arr, fbl::count_of(scale_arr),
                             true, true));

  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());
}

// Successive GetScaleArray calls without Advance should return same results.
TEST_F(ScaleArrayTest, GetScaleArrayWithoutAdvance) {
  gain_.SetSourceGainWithRamp(-123.45678, ZX_MSEC(9));

  Gain::AScale scale_arr[10];
  gain_.GetScaleArray(scale_arr, fbl::count_of(scale_arr), rate_1khz_output_);

  Gain::AScale scale_arr2[10];
  gain_.GetScaleArray(scale_arr2, fbl::count_of(scale_arr2), rate_1khz_output_);

  EXPECT_TRUE(CompareBuffers(scale_arr, scale_arr2, fbl::count_of(scale_arr)));
}

// Advances that exceed ramp durations should lead to end-to-ramp conditions.
TEST_F(ScaleArrayTest, GetScaleArrayBigAdvance) {
  Gain::AScale scale_arr[6];
  Gain::AScale expect = Gain::kUnityScale * 2;

  gain_.SetSourceGainWithRamp(6.0205999, ZX_MSEC(5));
  gain_.GetScaleArray(scale_arr, fbl::count_of(scale_arr), rate_1khz_output_);

  EXPECT_FALSE(CompareBufferToVal(scale_arr, expect, fbl::count_of(scale_arr),
                                  false, true));
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());

  gain_.Advance(rate_1khz_output_.Scale(ZX_SEC(10)), rate_1khz_output_);
  gain_.GetScaleArray(scale_arr, fbl::count_of(scale_arr), rate_1khz_output_);

  EXPECT_TRUE(CompareBufferToVal(scale_arr, expect, fbl::count_of(scale_arr)));
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());
}

// Clearing a ramp should reset any in-process ramps.
TEST_F(ScaleArrayTest, ClearSourceRamp) {
  Gain::AScale scale_arr[6];
  Gain::AScale scale_arr2[6];

  gain_.SetSourceGainWithRamp(-30.1029995, ZX_MSEC(5));
  gain_.GetScaleArray(scale_arr, fbl::count_of(scale_arr), rate_1khz_output_);

  EXPECT_FALSE(CompareBufferToVal(scale_arr, Gain::kUnityScale,
                                  fbl::count_of(scale_arr), false, true));
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());

  // After clearing the ramp, scale_arr should be constant.
  gain_.ClearSourceRamp();
  gain_.GetScaleArray(scale_arr, fbl::count_of(scale_arr), rate_1khz_output_);

  EXPECT_TRUE(CompareBufferToVal(scale_arr, Gain::kUnityScale,
                                 fbl::count_of(scale_arr)));
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_TRUE(gain_.IsUnity());

  // Without a ramp, scale_arr should be constant even after Advance.
  gain_.Advance(10, rate_1khz_output_);
  gain_.GetScaleArray(scale_arr2, fbl::count_of(scale_arr2), rate_1khz_output_);

  EXPECT_TRUE(CompareBufferToVal(scale_arr2, Gain::kUnityScale,
                                 fbl::count_of(scale_arr2)));
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_TRUE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsSilent());
}

// After partial Advance through a ramp, instantaneous gain should be accurate.
TEST_F(ScaleArrayTest, AdvanceHalfwayThroughRamp) {
  Gain::AScale scale_arr[4];  // At 1kHz this is less than the ramp duration.
  Gain::AScale expect_arr[4];

  gain_.SetSourceGainWithRamp(-20.0f, ZX_MSEC(9));
  gain_.GetScaleArray(scale_arr, fbl::count_of(scale_arr), rate_1khz_output_);

  Gain::AScale expect_scale = Gain::kUnityScale;
  EXPECT_EQ(gain_.GetGainScale(), expect_scale);

  // When comparing buffers, do it within the tolerance of 32-bit float
  for (auto& val : expect_arr) {
    val = expect_scale;
    expect_scale -= 0.1;
  }
  EXPECT_TRUE(CompareBuffers(scale_arr, expect_arr, fbl::count_of(scale_arr),
                             true, true));
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());

  // Advance only partially through the duration of the ramp.
  const uint32_t kFramesToAdvance = 2;
  gain_.Advance(kFramesToAdvance, rate_1khz_output_);
  gain_.GetScaleArray(scale_arr, fbl::count_of(scale_arr), rate_1khz_output_);
  // DisplayScaleVals(scale_arr, fbl::count_of(scale_arr));

  expect_scale = expect_arr[kFramesToAdvance];
  EXPECT_TRUE(
      CompareBufferToVal(&expect_scale, gain_.GetGainScale(), 1, true, true));

  for (auto& val : expect_arr) {
    val = expect_scale;
    expect_scale -= 0.1;
  }
  EXPECT_TRUE(CompareBuffers(scale_arr, expect_arr, fbl::count_of(scale_arr),
                             true, true));
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsSilent());
}

TEST_F(ScaleArrayTest, MuteDuringRamp) {
  Gain::AScale scale_arr[4];  // At 1kHz this is less than the ramp duration.
  Gain::AScale expect_arr[4];

  gain_.SetSourceGainWithRamp(-20.0f, ZX_MSEC(9));
  gain_.GetScaleArray(scale_arr, fbl::count_of(scale_arr), rate_1khz_output_);

  Gain::AScale expect_scale = Gain::kUnityScale;
  EXPECT_EQ(gain_.GetGainScale(), expect_scale);
  gain_.SetSourceMute(true);
  EXPECT_EQ(gain_.GetGainScale(), Gain::kMuteScale);

  for (auto& val : expect_arr) {
    val = expect_scale;
    expect_scale -= 0.1;
  }
  // When comparing buffers, do it within the tolerance of 32-bit float
  EXPECT_TRUE(CompareBuffers(scale_arr, expect_arr, fbl::count_of(scale_arr),
                             true, true));

  // Advance only partially through the duration of the ramp.
  const uint32_t kFramesToAdvance = 2;
  gain_.Advance(kFramesToAdvance, rate_1khz_output_);
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_TRUE(gain_.IsSilent());
  gain_.SetSourceMute(false);
  gain_.GetScaleArray(scale_arr, fbl::count_of(scale_arr), rate_1khz_output_);
  // DisplayScaleVals(scale_arr, fbl::count_of(scale_arr));

  expect_scale = expect_arr[kFramesToAdvance];
  EXPECT_TRUE(
      CompareBufferToVal(&expect_scale, gain_.GetGainScale(), 1, true, true));

  for (auto& val : expect_arr) {
    val = expect_scale;
    expect_scale -= 0.1;
  }
  EXPECT_TRUE(CompareBuffers(scale_arr, expect_arr, fbl::count_of(scale_arr),
                             true, true));
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsSilent());
}

//
// Data scaling tests
//
// Tests using Gain via a Mixer object, in a mixing environment.
//
// These validate the actual scaling of audio data, including overflow and any
// truncation or rounding (above just checks the generation of scale values).
//
// When doing direct bit-for-bit comparisons in these tests, we must factor in
// the left-shift biasing that is done while converting input data into the
// internal format of our accumulator. For this reason, all "expect" values are
// specified at a higher-than-needed precision of 24-bit, and then normalized
// down to the actual pipeline width.
//
// The 'MixGain' tests involve gain-scaling in the context of mixing (as opposed
// to earlier tests that directly probe the Gain object in isolation).

// Verify whether per-stream gain interacts linearly with accumulation buffer.
TEST(MixGain, Scaling_Linearity) {
  int16_t source[] = {0x0CE4, 0x0CCC, 0x23, 4, -0x0E, -0x19, -0x0CCC, -0x0CDB};
  float accum[8];

  // Validate that +20.00 dB leads to exactly 10x in value (within limits)
  float stream_gain_db = 20.0f;

  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                           44100, 1, 44100, Resampler::SampleAndHold);
  DoMix(mixer.get(), source, accum, false, fbl::count_of(accum),
        stream_gain_db);

  float expect[] = {0x080E8000,  0x07FF8000,  0x015E000,   0x00028000,
                    -0x0008C000, -0x000FA000, -0x07FF8000, -0x0808E000};
  NormalizeInt28ToPipelineBitwidth(expect, fbl::count_of(expect));
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  //
  // How precisely linear are our gain stages, mathematically?
  // Validate that -12.0411998 dB leads to exactly 0.25x in value
  stream_gain_db = -12.0411998f;

  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 44100, 1,
                      44100, Resampler::SampleAndHold);
  DoMix(mixer.get(), source, accum, false, fbl::count_of(accum),
        stream_gain_db);

  float expect2[] = {0x00339000,  0x00333000,  0x00008C00,  0x00001000,
                     -0x00003800, -0x00006400, -0x00333000, -0x00336C00};
  NormalizeInt28ToPipelineBitwidth(expect2, fbl::count_of(expect2));
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

// How does our gain scaling respond to scale values close to the limits?
// Using 16-bit inputs, verify the behavior of our Gain object when given the
// closest-to-Unity and closest-to-Mute scale values.
TEST(MixGain, Scaling_Precision) {
  int16_t max_source[] = {0x7FFF, -0x8000};  // max/min 16-bit signed values.
  float accum[2];

  // kMinGainDbUnity is the lowest (furthest-from-Unity) with no observable
  // attenuation on full-scale (i.e. the smallest indistinguishable from Unity).
  // At this gain_scale, audio should be unchanged.
  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                           48000, 1, 48000, Resampler::SampleAndHold);
  DoMix(mixer.get(), max_source, accum, false, fbl::count_of(accum),
        AudioResult::kMinGainDbUnity);

  //  At this gain_scale, resulting audio should be unchanged.
  float max_expect1[] = {0x07FFF000, -0x08000000};  // left-shift source by 12.
  NormalizeInt28ToPipelineBitwidth(max_expect1, fbl::count_of(max_expect1));
  EXPECT_TRUE(CompareBuffers(accum, max_expect1, fbl::count_of(accum)));

  // This is the highest (closest-to-Unity) AScale with an observable effect on
  // full-scale (i.e. the largest sub-Unity AScale distinguishable from Unity).
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, Resampler::SampleAndHold);
  DoMix(mixer.get(), max_source, accum, false, fbl::count_of(accum),
        AudioResult::kMaxGainDbNonUnity);

  // Float32 has 25-bit precision (not 28), hence our min delta is 8 (not 1).
  float max_expect2[] = {0x07FFEFF8, -0x07FFFFF8};
  NormalizeInt28ToPipelineBitwidth(max_expect2, fbl::count_of(max_expect2));
  EXPECT_TRUE(CompareBuffers(accum, max_expect2, fbl::count_of(accum)));

  // kMinGainDbNonMute is the lowest (closest-to-zero) at which audio is not
  // silenced (i.e. the smallest that is distinguishable from Mute).  Although
  // the results may be smaller than we can represent in our 28-bit test data
  // representation, they are still non-zero and thus validate our scalar limit.
  int16_t min_source[] = {1, -1};
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, Resampler::SampleAndHold);
  DoMix(mixer.get(), min_source, accum, false, fbl::count_of(accum),
        AudioResult::kMinGainDbNonMute);

  // The method used elsewhere in this file for expected result arrays (28-bit
  // fixed-point, normalized into float) cannot precisely express these values.
  // Nonetheless, they are present and non-zero!
  float min_expect[] = {3.051763215e-13, -3.051763215e-13};
  EXPECT_TRUE(CompareBuffers(accum, min_expect, fbl::count_of(accum)));

  //
  // kMaxGainDbMute is the highest (furthest-from-Mute) scalar that silences
  // full scale data (i.e. the largest AScale that is indistinguishable from
  // Mute). Consider an AScale value corresponding to ever-so-slightly above
  // -160dB: if this increment is small enough, the float32 cannot discern it
  // and treats it as -160dB, our limit for "automatically mute".  Per a mixer
  // optimization, if gain is Mute-equivalent, we skip mixing altogether. This
  // is equivalent to setting 'accumulate' and adding zeroes, so set that flag
  // here and expect no change in the accumulator, even with max inputs.
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, Resampler::SampleAndHold);
  DoMix(mixer.get(), max_source, accum, true, fbl::count_of(accum),
        AudioResult::kMaxGainDbMute);

  EXPECT_TRUE(CompareBuffers(accum, min_expect, fbl::count_of(accum)));
}

//
// Tests on our multi-stream accumulator -- can values temporarily exceed the
// max or min values for an individual stream; at what value doese the
// accumulator hit its limit, and at that limit does it clamp or rollover?
//
// Can accumulator result exceed the max range of individual streams?
TEST(MixGain, Accumulator) {
  int16_t source[] = {0x7FFF, -0x8000};
  float accum[] = {0x07FFF000, -0x08000000};
  float expect[] = {0x0FFFE000, -0x10000000};
  float expect2[] = {0x17FFD000, -0x18000000};

  // When mixed, these far exceed any int16 range
  NormalizeInt28ToPipelineBitwidth(accum, fbl::count_of(accum));
  NormalizeInt28ToPipelineBitwidth(expect, fbl::count_of(expect));
  NormalizeInt28ToPipelineBitwidth(expect2, fbl::count_of(expect2));

  // These values exceed the per-stream range of int16
  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                           48000, 1, 48000, Resampler::SampleAndHold);
  DoMix(mixer.get(), source, accum, true, fbl::count_of(accum));
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  // these values even exceed uint16
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 2, 48000, 2,
                      48000, Resampler::SampleAndHold);
  DoMix(mixer.get(), source, accum, true, 1);
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

// Our mixer contains an optimization in which it skips mixing operations if it
// detects that gain is below a certain threshold (regardless of "accumulate").
void TestAccumulatorClear(Resampler sampler_type) {
  int16_t source[] = {-32768, 32767, -16384, 16383};
  float accum[] = {-32768, 32767, -16384, 16383};
  float expect[] = {-32768, 32767, -16384, 16383};

  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                           48000, 1, 48000, sampler_type);
  // Use a gain guaranteed to silence any signal -- Gain::kMinGainDb.
  DoMix(mixer.get(), source, accum, true, fbl::count_of(accum),
        Gain::kMinGainDb);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  // When accumulate = false but gain is sufficiently low, overwriting previous
  // contents is skipped. This should lead to the same results as above.
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 1,
                      48000, sampler_type);
  DoMix(mixer.get(), source, accum, false, fbl::count_of(accum),
        Gain::kMinGainDb);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
}

// Validate the SampleAndHold interpolator for this behavior.
TEST(MixGain, Accumulator_Clear_Point) {
  TestAccumulatorClear(Resampler::SampleAndHold);
}

// Validate the same assertions, with LinearInterpolation interpolator.
TEST(MixGain, Accumulator_Clear_Linear) {
  TestAccumulatorClear(Resampler::LinearInterpolation);
}

}  // namespace media::audio::test
