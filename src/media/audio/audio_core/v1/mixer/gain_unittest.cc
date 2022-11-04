// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/mixer/gain.h"

#include <lib/syslog/cpp/macros.h>

#include <iterator>

#include <fbl/algorithm.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/lib/test/constants.h"

namespace media::audio {
namespace {

using ::testing::Each;
using ::testing::FloatEq;
using ::testing::Not;
using ::testing::Pointwise;

TEST(StaticGainTest, CombineGains) {
  static_assert(-90.0 < 0.5f * media_audio::kMinGainDb);
  EXPECT_FLOAT_EQ(Gain::CombineGains(-90.0f, -90.0f), media_audio::kMinGainDb);

  EXPECT_FLOAT_EQ(Gain::CombineGains(-20.0f, 5.0f), -15.0f);
  EXPECT_FLOAT_EQ(Gain::CombineGains(15.0f, 15.0f), 30.0f);
}

// Gain tests - how does the Gain object respond when given values close to its
// maximum or minimum; does it correctly cache; do values combine to form Unity
// gain. Is data scaling accurately performed, and is it adequately linear? Do
// our gains and accumulators behave as expected when they overflow?
//
// Gain tests using AScale and the Gain object only
//
class GainBase : public testing::Test {
 protected:
  void SetUp() override {
    testing::Test::SetUp();
    rate_1khz_output_ = TimelineRate(1000, ZX_SEC(1));
  }

  // Used for debugging purposes.
  static void DisplayScaleVals(const Gain::AScale* scale_arr, int64_t buf_size) {
    printf("\n    ********************************************************");
    printf("\n **************************************************************");
    printf("\n ***    Displaying raw scale array data for length %5ld    ***", buf_size);
    printf("\n **************************************************************");
    for (auto idx = 0; idx < buf_size; ++idx) {
      if (idx % 10 == 0) {
        printf("\n [%d]  ", idx);
      }
      printf("%.7f   ", scale_arr[idx]);
    }
    printf("\n **************************************************************");
    printf("\n    ********************************************************");
    printf("\n");
  }

  // Overridden by SourceDestGainControl and DestSourceGainControl
  virtual void SetGain(float gain_db) = 0;
  virtual void SetOtherGain(float gain_db) = 0;
  virtual void SetGainWithRamp(float gain_db, zx::duration duration,
                               fuchsia::media::audio::RampType ramp_type =
                                   fuchsia::media::audio::RampType::SCALE_LINEAR) = 0;
  virtual void SetOtherGainWithRamp(float gain_db, zx::duration duration,
                                    fuchsia::media::audio::RampType ramp_type =
                                        fuchsia::media::audio::RampType::SCALE_LINEAR) = 0;
  virtual float GetPartialGainDb() = 0;
  virtual float GetOtherPartialGainDb() = 0;

  virtual void CompleteRamp() = 0;

  // Used by SourceDestGainTest and DestSourceGainTest
  void TestUnityGain(float first_gain_db, float second_gain_db);
  void UnityChecks();
  void GainCachingChecks();
  void VerifyMinGain(float first_gain_db, float second_gain_db);
  void MinGainChecks();
  void VerifyMaxGain(float first_gain_db, float second_gain_db);
  void MaxGainChecks();
  void SourceMuteChecks();

  void TestRampWithNoDuration();
  void TestRampWithDuration();
  void TestRampIntoSilence();
  void TestRampOutOfSilence();
  void TestRampFromSilenceToSilence();
  void TestRampsCombineForSilence();
  void TestRampUnity();
  void TestFlatRamp();
  void TestRampingBelowMinGain();
  void TestRampWithMute();
  void TestAdvance();
  void TestSetGainCancelsRamp();
  void TestRampsForSilence();
  void TestRampsForNonSilence();

  // These precise scale tests produce a scale array from two controls.
  // The returned max value ignores the "internal" control.
  enum class AdjustmentControlPosition {
    First,
    Second,
    None,
  };
  virtual AdjustmentControlPosition adjustment_control_position() const = 0;

  void TestCalculateScaleArrayNoRamp();
  void TestCalculateScaleArray();
  void TestScaleArrayLongRamp();
  void TestScaleArrayShortRamp();
  void TestScaleArrayWithoutAdvance();
  void TestScaleArrayBigAdvance();
  void TestRampCompletion();
  void TestAdvanceHalfwayThroughRamp();
  void TestSuccessiveRamps();
  void TestCombinedRamps();
  void TestCrossFades();
  void TestScaleArrayForMinScale();

  Gain gain_;

  // All tests use a 1 kHz frame rate, for easy 1-frame-per-msec observation.
  TimelineRate rate_1khz_output_;
};

// Used so that identical testing is done on each pair of gain controls.

class SourceDestGainControl : public GainBase {
 protected:
  void SetGain(float gain_db) override { gain_.SetSourceGain(gain_db); }
  void SetOtherGain(float gain_db) override { gain_.SetDestGain(gain_db); }
  void SetGainWithRamp(float gain_db, zx::duration duration,
                       fuchsia::media::audio::RampType ramp_type =
                           fuchsia::media::audio::RampType::SCALE_LINEAR) override {
    gain_.SetSourceGainWithRamp(gain_db, duration, ramp_type);
  }
  void SetOtherGainWithRamp(float gain_db, zx::duration duration,
                            fuchsia::media::audio::RampType ramp_type =
                                fuchsia::media::audio::RampType::SCALE_LINEAR) override {
    gain_.SetDestGainWithRamp(gain_db, duration, ramp_type);
  }
  float GetPartialGainDb() override { return gain_.GetSourceGainDb(); }
  float GetOtherPartialGainDb() override { return gain_.GetDestGainDb(); }

  void CompleteRamp() override { gain_.CompleteSourceRamp(); }

  AdjustmentControlPosition adjustment_control_position() const override {
    return AdjustmentControlPosition::None;
  }
};

class DestSourceGainControl : public GainBase {
 protected:
  void SetGain(float gain_db) override { gain_.SetDestGain(gain_db); }
  void SetOtherGain(float gain_db) override { gain_.SetSourceGain(gain_db); }
  void SetGainWithRamp(float gain_db, zx::duration duration,
                       fuchsia::media::audio::RampType ramp_type =
                           fuchsia::media::audio::RampType::SCALE_LINEAR) override {
    gain_.SetDestGainWithRamp(gain_db, duration, ramp_type);
  }
  void SetOtherGainWithRamp(float gain_db, zx::duration duration,
                            fuchsia::media::audio::RampType ramp_type =
                                fuchsia::media::audio::RampType::SCALE_LINEAR) override {
    gain_.SetSourceGainWithRamp(gain_db, duration, ramp_type);
  }
  float GetPartialGainDb() override { return gain_.GetDestGainDb(); }
  float GetOtherPartialGainDb() override { return gain_.GetSourceGainDb(); }

  void CompleteRamp() override { gain_.CompleteDestRamp(); }

  AdjustmentControlPosition adjustment_control_position() const override {
    return AdjustmentControlPosition::None;
  }
};

class SourceAdjustmentGainControl : public GainBase {
 protected:
  void SetGain(float gain_db) override { gain_.SetSourceGain(gain_db); }
  void SetOtherGain(float gain_db) override { gain_.SetGainAdjustment(gain_db); }
  void SetGainWithRamp(float gain_db, zx::duration duration,
                       fuchsia::media::audio::RampType ramp_type =
                           fuchsia::media::audio::RampType::SCALE_LINEAR) override {
    gain_.SetSourceGainWithRamp(gain_db, duration, ramp_type);
  }
  void SetOtherGainWithRamp(float gain_db, zx::duration duration,
                            fuchsia::media::audio::RampType ramp_type =
                                fuchsia::media::audio::RampType::SCALE_LINEAR) override {
    gain_.SetGainAdjustmentWithRamp(gain_db, duration, ramp_type);
  }
  float GetPartialGainDb() override { return gain_.GetSourceGainDb(); }
  float GetOtherPartialGainDb() override { return gain_.GetGainAdjustmentDb(); }

  void CompleteRamp() override { gain_.CompleteSourceRamp(); }

  AdjustmentControlPosition adjustment_control_position() const override {
    return AdjustmentControlPosition::Second;
  }
};

class AdjustmentSourceGainControl : public GainBase {
 protected:
  void SetGain(float gain_db) override { gain_.SetGainAdjustment(gain_db); }
  void SetOtherGain(float gain_db) override { gain_.SetSourceGain(gain_db); }
  void SetGainWithRamp(float gain_db, zx::duration duration,
                       fuchsia::media::audio::RampType ramp_type =
                           fuchsia::media::audio::RampType::SCALE_LINEAR) override {
    gain_.SetGainAdjustmentWithRamp(gain_db, duration, ramp_type);
  }
  void SetOtherGainWithRamp(float gain_db, zx::duration duration,
                            fuchsia::media::audio::RampType ramp_type =
                                fuchsia::media::audio::RampType::SCALE_LINEAR) override {
    gain_.SetSourceGainWithRamp(gain_db, duration, ramp_type);
  }
  float GetPartialGainDb() override { return gain_.GetGainAdjustmentDb(); }
  float GetOtherPartialGainDb() override { return gain_.GetSourceGainDb(); }

  void CompleteRamp() override { gain_.CompleteAdjustmentRamp(); }

  AdjustmentControlPosition adjustment_control_position() const override {
    return AdjustmentControlPosition::First;
  }
};

// General (non-specific to source or dest) gain checks
class GainTest : public SourceDestGainControl {};

// Gain checks that can be source/dest inverted.
class SourceDestGainTest : public SourceDestGainControl {};
class DestSourceGainTest : public DestSourceGainControl {};
class SourceAdjustmentGainTest : public SourceAdjustmentGainControl {};
class AdjustmentSourceGainTest : public AdjustmentSourceGainControl {};

// Test the defaults upon construction
TEST_F(GainTest, Defaults) {
  EXPECT_FLOAT_EQ(gain_.GetGainScale(), media_audio::kUnityGainScale);
  EXPECT_TRUE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsRamping());
}

void GainBase::TestUnityGain(float first_gain_db, float second_gain_db) {
  SetGain(first_gain_db);
  SetOtherGain(second_gain_db);

  EXPECT_FLOAT_EQ(media_audio::kUnityGainScale, gain_.GetGainScale());
  EXPECT_FLOAT_EQ(media_audio::kUnityGainDb, GetPartialGainDb() + GetOtherPartialGainDb());
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_TRUE(gain_.IsUnity());
}
void GainBase::UnityChecks() {
  TestUnityGain(media_audio::kUnityGainDb, media_audio::kUnityGainDb);

  // These positive/negative values should sum to 0.0: UNITY
  TestUnityGain(24.0f, -24.0f);
  TestUnityGain(-5.0f, 5.0f);
}
// Do source and destination gains correctly combine to produce unity scaling?
TEST_F(SourceDestGainTest, Unity) { UnityChecks(); }
TEST_F(DestSourceGainTest, Unity) { UnityChecks(); }
TEST_F(SourceAdjustmentGainTest, Unity) { UnityChecks(); }
TEST_F(AdjustmentSourceGainTest, Unity) { UnityChecks(); }

void GainBase::GainCachingChecks() {
  Gain expect_gain;
  Gain::AScale amplitude_scale, expect_amplitude_scale;

  // Set expect_amplitude_scale to a value that represents -6.0 dB.
  expect_gain.SetSourceGain(-6.0f);
  expect_amplitude_scale = expect_gain.GetGainScale();

  // Source gain defaults to 0.0, so this represents -6.0 dB too.
  SetGain(0.0f);
  SetOtherGain(-6.0f);
  amplitude_scale = gain_.GetGainScale();

  EXPECT_FLOAT_EQ(expect_amplitude_scale, amplitude_scale);

  // Now set a different source gain that will be cached (+3.0).
  SetGain(3.0f);
  SetOtherGain(-3.0f);
  amplitude_scale = gain_.GetGainScale();

  EXPECT_FLOAT_EQ(media_audio::kUnityGainScale, amplitude_scale);
  EXPECT_GT(GetPartialGainDb(), media_audio::kUnityGainDb);
  EXPECT_LT(GetOtherPartialGainDb(), media_audio::kUnityGainDb);

  SetOtherGain(-1.0f);

  EXPECT_EQ(GetOtherPartialGainDb(), -1.0f);

  // If source gain is cached val of +3, then combo should be greater than Unity.
  amplitude_scale = gain_.GetGainScale();

  EXPECT_GT(amplitude_scale, media_audio::kUnityGainScale);
  // And now the previous SetOtherGain call has been incorporated into the cache.
  EXPECT_EQ(GetOtherPartialGainDb(), -1.0f);

  // Try another dest gain; with cached +3 this should equate to -6dB.
  SetOtherGain(-9.0f);

  EXPECT_FLOAT_EQ(expect_amplitude_scale, gain_.GetGainScale());

  // source gain cached +3 and dest gain non-cached -3 should lead to Unity.
  SetOtherGain(-3.0f);

  EXPECT_FLOAT_EQ(media_audio::kUnityGainScale, gain_.GetGainScale());
}
// Gain caches any previously set source gain, using it if needed.
// This verifies the default and caching behavior of the Gain object
TEST_F(SourceDestGainTest, GainCaching) { GainCachingChecks(); }
TEST_F(DestSourceGainTest, GainCaching) { GainCachingChecks(); }
TEST_F(SourceAdjustmentGainTest, GainCaching) { GainCachingChecks(); }
TEST_F(AdjustmentSourceGainTest, GainCaching) { GainCachingChecks(); }

void GainBase::VerifyMinGain(float first_gain_db, float second_gain_db) {
  SCOPED_TRACE("VerifyMinGain(" + std::to_string(first_gain_db) + ", " +
               std::to_string(second_gain_db) + ")");

  SetGain(first_gain_db);
  SetOtherGain(second_gain_db);

  EXPECT_FLOAT_EQ(Gain::kMuteScale, gain_.GetGainScale());

  EXPECT_FLOAT_EQ(GetPartialGainDb(), std::max(first_gain_db, media_audio::kMinGainDb));
  EXPECT_FLOAT_EQ(GetOtherPartialGainDb(), std::max(second_gain_db, media_audio::kMinGainDb));

  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsSilent());
}
void GainBase::MinGainChecks() {
  // First, test for source/dest interactions.
  // if dest gain <= media_audio::kMinGainDb, scale must be 0, regardless of source gain.
  VerifyMinGain(-2 * media_audio::kMinGainDb, media_audio::kMinGainDb);

  // if source gain <= media_audio::kMinGainDb, scale must be 0, regardless of dest gain.
  VerifyMinGain(media_audio::kMinGainDb, -2.0f * media_audio::kMinGainDb);

  // if sum of source gain and dest gain <= media_audio::kMinGainDb, scale should be 0.
  // dest gain is just slightly above MinGain; source gain takes us below it.
  VerifyMinGain(-2.0f, media_audio::kMinGainDb + 1.0f);

  // Next, test for source/dest interactions.
  // Check if source alone mutes.
  VerifyMinGain(media_audio::kMinGainDb, media_audio::kUnityGainDb);
  VerifyMinGain(media_audio::kMinGainDb, media_audio::kUnityGainDb + 1);
  // Check if dest alone mutes.
  VerifyMinGain(media_audio::kUnityGainDb + 1, media_audio::kMinGainDb);
  VerifyMinGain(media_audio::kUnityGainDb, media_audio::kMinGainDb);

  // Check if the combination mutes.
  VerifyMinGain(media_audio::kMinGainDb / 2, media_audio::kMinGainDb / 2);
}
// System independently limits source gain and dest gain to media_audio::kMinGainDb (-160dB).
// Assert scale is zero, if either (or combo) are media_audio::kMinGainDb or less.
TEST_F(SourceDestGainTest, GainIsLimitedToMin) { MinGainChecks(); }
TEST_F(DestSourceGainTest, GainIsLimitedToMin) { MinGainChecks(); }
TEST_F(SourceAdjustmentGainTest, GainIsLimitedToMin) { MinGainChecks(); }
TEST_F(AdjustmentSourceGainTest, GainIsLimitedToMin) { MinGainChecks(); }

void GainBase::SourceMuteChecks() {
  SetGain(0.0f);

  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_TRUE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_EQ(gain_.GetGainScale(), media_audio::kUnityGainScale);
  EXPECT_EQ(gain_.GetGainDb(), media_audio::kUnityGainDb);

  gain_.SetSourceMute(false);

  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_TRUE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_EQ(gain_.GetGainScale(), media_audio::kUnityGainScale);
  EXPECT_EQ(gain_.GetGainDb(), media_audio::kUnityGainDb);

  gain_.SetSourceMute(true);

  EXPECT_TRUE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_EQ(gain_.GetGainScale(), Gain::kMuteScale);
  EXPECT_LE(gain_.GetGainDb(), media_audio::kMinGainDb);

  gain_.SetSourceMute(false);
  SetGainWithRamp(-10.0, zx::msec(25));

  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_EQ(gain_.GetGainScale(), media_audio::kUnityGainScale);
  EXPECT_EQ(gain_.GetGainDb(), media_audio::kUnityGainDb);

  gain_.SetSourceMute(true);

  EXPECT_TRUE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_EQ(gain_.GetGainScale(), Gain::kMuteScale);
  EXPECT_LE(gain_.GetGainDb(), media_audio::kMinGainDb);
}
// source_mute control should affect IsSilent, IsUnity, IsRamping and GetGainScale appropriately.
TEST_F(SourceDestGainTest, SourceMuteOverridesGainAndRamp) { SourceMuteChecks(); }
TEST_F(DestSourceGainTest, SourceMuteOverridesGainAndRamp) { SourceMuteChecks(); }
TEST_F(SourceAdjustmentGainTest, SourceMuteOverridesGainAndRamp) { SourceMuteChecks(); }
TEST_F(AdjustmentSourceGainTest, SourceMuteOverridesGainAndRamp) { SourceMuteChecks(); }

// Ramp-related tests
//
void GainBase::TestRampWithNoDuration() {
  SetGain(-11.0f);
  SetOtherGain(-1.0f);

  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsRamping());

  SetGainWithRamp(+1.0f, zx::nsec(0));

  EXPECT_TRUE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());
}
// Setting a ramp with zero duration is the same as an immediate gain change.
TEST_F(SourceDestGainTest, SetRampWithNoDurationChangesCurrentGain) { TestRampWithNoDuration(); }
TEST_F(DestSourceGainTest, SetRampWithNoDurationChangesCurrentGain) { TestRampWithNoDuration(); }
TEST_F(SourceAdjustmentGainTest, SetRampWithNoDurationChangesCurrentGain) {
  TestRampWithNoDuration();
}
TEST_F(AdjustmentSourceGainTest, SetRampWithNoDurationChangesCurrentGain) {
  TestRampWithNoDuration();
}

// Setting a ramp with non-zero duration does not take effect until Advance.
void GainBase::TestRampWithDuration() {
  SetGain(24.0f);
  SetOtherGain(-24.0f);

  EXPECT_TRUE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsRamping());

  SetGainWithRamp(media_audio::kMinGainDb, zx::nsec(1));

  EXPECT_TRUE(gain_.GetGainScale() == media_audio::kUnityGainScale);
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
}
// Setting a ramp with non-zero duration does not take effect until Advance.
TEST_F(SourceDestGainTest, SetRampWithDurationDoesntChangeCurrentGain) { TestRampWithDuration(); }
TEST_F(DestSourceGainTest, SetRampWithDurationDoesntChangeCurrentGain) { TestRampWithDuration(); }
TEST_F(SourceAdjustmentGainTest, SetRampWithDurationDoesntChangeCurrentGain) {
  TestRampWithDuration();
}
TEST_F(AdjustmentSourceGainTest, SetRampWithDurationDoesntChangeCurrentGain) {
  TestRampWithDuration();
}

void GainBase::TestRampIntoSilence() {
  SetGain(0.0f);
  SetOtherGain(media_audio::kMinGainDb + 1.0f);
  SetGainWithRamp(media_audio::kMinGainDb + 1.0f, zx::sec(1));

  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());

  SetOtherGain(0.0f);
  SetGainWithRamp(media_audio::kMinGainDb * 2, zx::sec(1));

  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());
}
// If we are ramping-down and already silent, IsSilent should remain true.
TEST_F(SourceDestGainTest, RampFromNonSilenceToSilenceIsNotSilent) { TestRampIntoSilence(); }
TEST_F(DestSourceGainTest, RampFromNonSilenceToSilenceIsNotSilent) { TestRampIntoSilence(); }
TEST_F(SourceAdjustmentGainTest, RampFromNonSilenceToSilenceIsNotSilent) { TestRampIntoSilence(); }
TEST_F(AdjustmentSourceGainTest, RampFromNonSilenceToSilenceIsNotSilent) { TestRampIntoSilence(); }

void GainBase::TestRampOutOfSilence() {
  // Combined, we start in silence...
  SetGain(media_audio::kMinGainDb + 10.f);
  SetOtherGain(-22.0f);

  EXPECT_TRUE(gain_.IsSilent());

  // ... and ramp out of it
  SetGainWithRamp(+22.0f, zx::sec(1));

  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());

  // The first stage, on its own, makes us silent...
  SetGain(media_audio::kMinGainDb - 5.0f);
  SetOtherGain(0.0f);

  EXPECT_TRUE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsRamping());

  // ... but it ramps out of it.
  SetGainWithRamp(media_audio::kMinGainDb + 1.0f, zx::sec(1));

  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());
}
// If we are ramping-down and already silent, IsSilent should remain true.
TEST_F(SourceDestGainTest, RampFromSilenceToNonSilenceIsNotSilent) { TestRampOutOfSilence(); }
TEST_F(DestSourceGainTest, RampFromSilenceToNonSilenceIsNotSilent) { TestRampOutOfSilence(); }
TEST_F(SourceAdjustmentGainTest, RampFromSilenceToNonSilenceIsNotSilent) { TestRampOutOfSilence(); }
TEST_F(AdjustmentSourceGainTest, RampFromSilenceToNonSilenceIsNotSilent) { TestRampOutOfSilence(); }

void GainBase::TestRampFromSilenceToSilence() {
  // Both start and end are at/below media_audio::kMinGainDb -- ramping up
  SetGain(media_audio::kMinGainDb - 1.0f);
  SetGainWithRamp(media_audio::kMinGainDb, zx::sec(1));

  EXPECT_TRUE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsRamping());

  // Both start and end are at/below media_audio::kMinGainDb -- ramping down
  SetGainWithRamp(media_audio::kMinGainDb - 2.0f, zx::sec(1));

  EXPECT_TRUE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsRamping());
}
// If the beginning and end of a ramp are both at/below min gain, it isn't ramping.
TEST_F(SourceDestGainTest, RampFromSilenceToSilenceIsNotRamping) { TestRampFromSilenceToSilence(); }
TEST_F(DestSourceGainTest, RampFromSilenceToSilenceIsNotRamping) { TestRampFromSilenceToSilence(); }
TEST_F(SourceAdjustmentGainTest, RampFromSilenceToSilenceIsNotRamping) {
  TestRampFromSilenceToSilence();
}
TEST_F(AdjustmentSourceGainTest, RampFromSilenceToSilenceIsNotRamping) {
  TestRampFromSilenceToSilence();
}

void GainBase::TestRampsCombineForSilence() {
  // Both start and end are at/below media_audio::kMinGainDb -- ramping up
  SetGain(media_audio::kMinGainDb);
  SetOtherGain(media_audio::kUnityGainDb);

  EXPECT_TRUE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsRamping());

  // Because our scalelinear ramps are not equal-power, we "bulge" at the midpoint of fades, thus
  // combined ramps may not be silent just because their endpoints are.
  SetGainWithRamp(media_audio::kUnityGainDb, zx::sec(1));
  SetOtherGainWithRamp(media_audio::kMinGainDb, zx::sec(1));

  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_TRUE(gain_.IsRamping());
}
// If the beginning and end of a ramp are both at/below min gain, it isn't ramping.
TEST_F(SourceDestGainTest, RampsCombineForSilenceIsNotSilent) { TestRampsCombineForSilence(); }
TEST_F(DestSourceGainTest, RampsCombineForSilenceIsNotSilent) { TestRampsCombineForSilence(); }
TEST_F(SourceAdjustmentGainTest, RampsCombineForSilenceIsNotSilent) {
  TestRampsCombineForSilence();
}
TEST_F(AdjustmentSourceGainTest, RampsCombineForSilenceIsNotSilent) {
  TestRampsCombineForSilence();
}

void GainBase::TestRampUnity() {
  SetGain(media_audio::kUnityGainDb);
  SetOtherGain(media_audio::kUnityGainDb);

  EXPECT_TRUE(gain_.IsUnity());

  SetGainWithRamp(-1.0f, zx::sec(1));

  // Expect pre-ramp conditions
  EXPECT_EQ(gain_.GetGainDb(), media_audio::kUnityGainDb);
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsUnity());  // unity at this instant, but not _staying_ there
  EXPECT_TRUE(gain_.IsRamping());
}
// If a ramp is active/pending, then IsUnity should never be true.
TEST_F(SourceDestGainTest, RampIsNeverUnity) { TestRampUnity(); }
TEST_F(DestSourceGainTest, RampIsNeverUnity) { TestRampUnity(); }
TEST_F(SourceAdjustmentGainTest, RampIsNeverUnity) { TestRampUnity(); }
TEST_F(AdjustmentSourceGainTest, RampIsNeverUnity) { TestRampUnity(); }

void GainBase::TestFlatRamp() {
  SetGain(media_audio::kUnityGainDb);
  SetOtherGain(-20.0f);

  SetGainWithRamp(0.0f, zx::sec(1));

  // Expect pre-ramp conditions
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsRamping());

  // ... and a flat ramp should combine with the other side to equal Unity.
  SetOtherGain(0.0f);
  EXPECT_TRUE(gain_.IsUnity());
}
// If the beginning and end of a ramp are the same, it isn't ramping.
TEST_F(SourceDestGainTest, FlatIsntRamping) { TestFlatRamp(); }
TEST_F(DestSourceGainTest, FlatIsntRamping) { TestFlatRamp(); }
TEST_F(SourceAdjustmentGainTest, FlatIsntRamping) { TestFlatRamp(); }
TEST_F(AdjustmentSourceGainTest, FlatIsntRamping) { TestFlatRamp(); }

void GainBase::TestRampWithMute() {
  SetGain(0.0f);
  SetGainWithRamp(-10.0, zx::msec(25));

  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_TRUE(gain_.IsRamping());

  gain_.SetSourceMute(true);

  EXPECT_TRUE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsRamping());

  // after clearing the mute, we should be seen as ramping.
  gain_.SetSourceMute(false);

  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_TRUE(gain_.IsRamping());
}
// If the beginning and end of a ramp are the same, it isn't ramping.
TEST_F(SourceDestGainTest, MuteOverridesRamp) { TestRampWithMute(); }
TEST_F(DestSourceGainTest, MuteOverridesRamp) { TestRampWithMute(); }
TEST_F(SourceAdjustmentGainTest, MuteOverridesRamp) { TestRampWithMute(); }
TEST_F(AdjustmentSourceGainTest, MuteOverridesRamp) { TestRampWithMute(); }

void GainBase::TestAdvance() {
  SetGain(-150.0f);
  SetOtherGain(-13.0f);
  SetGainWithRamp(+13.0f, zx::nsec(1));

  // Advance far beyond end of ramp -- 10 msec (10 frames@1kHz) vs. 1 nsec.
  gain_.Advance(10, rate_1khz_output_);

  // Expect post-ramp conditions
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_TRUE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsRamping());
}
// Upon Advance, we should see a change in the instantaneous GetGainScale().
TEST_F(SourceDestGainTest, AdvanceChangesGain) { TestAdvance(); }
TEST_F(DestSourceGainTest, AdvanceChangesGain) { TestAdvance(); }
TEST_F(SourceAdjustmentGainTest, AdvanceChangesGain) { TestAdvance(); }
TEST_F(AdjustmentSourceGainTest, AdvanceChangesGain) { TestAdvance(); }

void GainBase::TestSetGainCancelsRamp() {
  SetGain(-60.0f);
  SetOtherGain(-20.0f);
  SetGainWithRamp(-20.0f, zx::sec(1));

  EXPECT_FLOAT_EQ(gain_.GetGainDb(), -80.0f);
  EXPECT_TRUE(gain_.IsRamping());

  // Advance halfway through the ramp (500 frames, which at 1kHz is 500 ms).
  gain_.Advance(500, rate_1khz_output_);

  EXPECT_TRUE(gain_.IsRamping());

  SetGain(0.0f);

  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_FLOAT_EQ(gain_.GetGainDb(), -20.0f);
}
// Setting a static gain during ramping should cancel the ramp
TEST_F(SourceDestGainTest, SetSourceGainCancelsRamp) { TestSetGainCancelsRamp(); }
TEST_F(DestSourceGainTest, SetSourceGainCancelsRamp) { TestSetGainCancelsRamp(); }
TEST_F(SourceAdjustmentGainTest, SetSourceGainCancelsRamp) { TestSetGainCancelsRamp(); }
TEST_F(AdjustmentSourceGainTest, SetSourceGainCancelsRamp) { TestSetGainCancelsRamp(); }

void GainBase::TestRampsForSilence() {
  // Flat ramp reverts to static gain combination
  SetGain(-80.0f);
  SetOtherGain(-80.0f);
  SetGainWithRamp(-80.0f, zx::sec(1));

  EXPECT_TRUE(gain_.IsSilent());

  // Already below the silence threshold and ramping downward
  SetGainWithRamp(-90.0f, zx::sec(1));

  EXPECT_TRUE(gain_.IsSilent());

  // Ramping upward, but other stage is below mute threshold
  SetGain(10.0f);
  SetOtherGain(media_audio::kMinGainDb);
  SetGainWithRamp(12.0f, zx::sec(1));

  EXPECT_TRUE(gain_.IsSilent());

  // Ramping upward, but to a target below mute threshold
  SetGain(media_audio::kMinGainDb - 5.0f);
  SetOtherGain(10.0f);
  SetGainWithRamp(media_audio::kMinGainDb, zx::sec(1));

  EXPECT_TRUE(gain_.IsSilent());
}
// Setting a static gain during ramping should cancel the ramp
TEST_F(SourceDestGainTest, WhenIsSilentShouldBeTrue) { TestRampsForSilence(); }
TEST_F(DestSourceGainTest, WhenIsSilentShouldBeTrue) { TestRampsForSilence(); }
TEST_F(SourceAdjustmentGainTest, WhenIsSilentShouldBeTrue) { TestRampsForSilence(); }
TEST_F(AdjustmentSourceGainTest, WhenIsSilentShouldBeTrue) { TestRampsForSilence(); }

void GainBase::TestRampsForNonSilence() {
  // Above the silence threshold, ramping downward
  SetGain(-79.0f);
  SetOtherGain(-80.0f);
  SetGainWithRamp(-90.0f, zx::sec(1));

  EXPECT_FALSE(gain_.IsSilent());

  // Below the silence threshold, ramping upward
  SetGain(-100.0f);
  SetOtherGain(-65.0f);
  SetGainWithRamp(-90.0f, zx::sec(1));

  EXPECT_FALSE(gain_.IsSilent());

  // Ramping from below to above mute threshold
  SetGain(media_audio::kMinGainDb - 5.0f);
  SetOtherGain(10.0f);
  SetGainWithRamp(media_audio::kMinGainDb + 1.0f, zx::sec(1));

  EXPECT_FALSE(gain_.IsSilent());

  // The following is not considered silence, because we expect clients to advance the ramp
  SetGain(-100.0f);
  SetOtherGain(-120.0f);
  SetGainWithRamp(-60.0f, zx::sec(1));

  EXPECT_FALSE(gain_.IsSilent());
}
TEST_F(SourceDestGainTest, WhenIsSilentShouldBeFalse) { TestRampsForNonSilence(); }
TEST_F(DestSourceGainTest, WhenIsSilentShouldBeFalse) { TestRampsForNonSilence(); }
TEST_F(SourceAdjustmentGainTest, WhenIsSilentShouldBeFalse) { TestRampsForNonSilence(); }
TEST_F(AdjustmentSourceGainTest, WhenIsSilentShouldBeFalse) { TestRampsForNonSilence(); }

// ScaleArray-related tests
//
void GainBase::TestCalculateScaleArrayNoRamp() {
  Gain::AScale scale_arr[3];
  SetGain(-42.0f);
  SetOtherGain(-68.0f);

  auto max_gain_scale =
      gain_.CalculateScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  EXPECT_THAT(scale_arr, Each(FloatEq(media_audio::DbToScale(-110.0f))));

  switch (adjustment_control_position()) {
    case AdjustmentControlPosition::None:
      EXPECT_FLOAT_EQ(max_gain_scale, media_audio::DbToScale(-110.0f));
      break;
    case AdjustmentControlPosition::First:
      EXPECT_FLOAT_EQ(max_gain_scale, media_audio::DbToScale(-68.0f));
      break;
    case AdjustmentControlPosition::Second:
      EXPECT_FLOAT_EQ(max_gain_scale, media_audio::DbToScale(-42.0f));
      break;
  }

  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());
}
// If no ramp, all vals returned by CalculateScaleArray should equal GetGainScale().
TEST_F(SourceDestGainTest, CalculateScaleArrayNoRampEqualsGetScale) {
  TestCalculateScaleArrayNoRamp();
}
TEST_F(DestSourceGainTest, CalculateScaleArrayNoRampEqualsGetScale) {
  TestCalculateScaleArrayNoRamp();
}
TEST_F(SourceAdjustmentGainTest, CalculateScaleArrayNoRampEqualsGetScale) {
  TestCalculateScaleArrayNoRamp();
}
TEST_F(AdjustmentSourceGainTest, CalculateScaleArrayNoRampEqualsGetScale) {
  TestCalculateScaleArrayNoRamp();
}

void GainBase::TestCalculateScaleArray() {
  Gain::AScale scale_arr[6];
  Gain::AScale expect_arr[6] = {0.5f, 0.42f, 0.34f, 0.26f, 0.18f, 0.10f};

  SetGain(media_audio::ScaleToDb(0.5f));
  SetGainWithRamp(media_audio::ScaleToDb(0.1f), zx::msec(5));

  auto max_gain_scale =
      gain_.CalculateScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  EXPECT_THAT(scale_arr, Pointwise(FloatEq(), expect_arr));

  switch (adjustment_control_position()) {
    case AdjustmentControlPosition::None:
    case AdjustmentControlPosition::Second:
      EXPECT_FLOAT_EQ(max_gain_scale, 0.5f);
      break;
    case AdjustmentControlPosition::First:
      // The internal control is ramping, while source and dest are unity.
      EXPECT_FLOAT_EQ(max_gain_scale, 1.0f);
      break;
  }

  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());
}
// Validate when ramp and CalculateScaleArray are identical length.
TEST_F(SourceDestGainTest, CalculateScaleArrayRamp) { TestCalculateScaleArray(); }
TEST_F(DestSourceGainTest, CalculateScaleArrayRamp) { TestCalculateScaleArray(); }
TEST_F(SourceAdjustmentGainTest, CalculateScaleArrayRamp) { TestCalculateScaleArray(); }
TEST_F(AdjustmentSourceGainTest, CalculateScaleArrayRamp) { TestCalculateScaleArray(); }

void GainBase::TestScaleArrayLongRamp() {
  Gain::AScale scale_arr[4];  // At 1kHz this is less than the ramp duration.
  Gain::AScale expect_arr[4] = {1.000f, 0.901f, 0.802f, 0.703f};

  SetGainWithRamp(-40, zx::msec(10));
  auto max_gain_scale =
      gain_.CalculateScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  EXPECT_THAT(scale_arr, Pointwise(FloatEq(), expect_arr));
  EXPECT_FLOAT_EQ(max_gain_scale, expect_arr[0]);

  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());
}
// Validate when ramp duration is greater than CalculateScaleArray.
TEST_F(SourceDestGainTest, CalculateScaleArrayLongRamp) { TestScaleArrayLongRamp(); }
TEST_F(DestSourceGainTest, CalculateScaleArrayLongRamp) { TestScaleArrayLongRamp(); }

void GainBase::TestScaleArrayShortRamp() {
  Gain::AScale scale_arr[9];  // At 1kHz this is longer than the ramp duration.
  Gain::AScale expect_arr[9] = {1.00f, 0.82f, 0.64f, 0.46f, 0.28f, 0.10f, 0.10f, 0.10f, 0.10f};

  SetGainWithRamp(-20, zx::msec(5));
  auto max_gain_scale =
      gain_.CalculateScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  EXPECT_THAT(scale_arr, Pointwise(FloatEq(), expect_arr));
  EXPECT_FLOAT_EQ(max_gain_scale, expect_arr[0]);

  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());
}
// Validate when ramp duration is shorter than CalculateScaleArray.
TEST_F(SourceDestGainTest, CalculateScaleArrayShortRamp) { TestScaleArrayShortRamp(); }
TEST_F(DestSourceGainTest, CalculateScaleArrayShortRamp) { TestScaleArrayShortRamp(); }

void GainBase::TestScaleArrayWithoutAdvance() {
  SetGainWithRamp(-123.45678f, zx::msec(9));

  Gain::AScale scale_arr[10];
  auto max_gain_scale =
      gain_.CalculateScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);
  EXPECT_FLOAT_EQ(max_gain_scale, media_audio::kUnityGainScale);

  Gain::AScale scale_arr2[10];
  max_gain_scale = gain_.CalculateScaleArray(scale_arr2, std::size(scale_arr2), rate_1khz_output_);
  EXPECT_FLOAT_EQ(max_gain_scale, media_audio::kUnityGainScale);

  EXPECT_THAT(scale_arr, Pointwise(FloatEq(), scale_arr2));
}
// Successive CalculateScaleArray calls without Advance should return same results.
TEST_F(SourceDestGainTest, CalculateScaleArrayWithoutAdvance) { TestScaleArrayWithoutAdvance(); }
TEST_F(DestSourceGainTest, CalculateScaleArrayWithoutAdvance) { TestScaleArrayWithoutAdvance(); }

void GainBase::TestScaleArrayBigAdvance() {
  Gain::AScale scale_arr[6];
  Gain::AScale expect = media_audio::kUnityGainScale * 2;

  SetGainWithRamp(6.0205999f, zx::msec(5));
  auto max_gain_scale =
      gain_.CalculateScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  EXPECT_THAT(scale_arr, Not(Each(FloatEq(expect))));
  EXPECT_FLOAT_EQ(max_gain_scale, expect);
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());

  gain_.Advance(rate_1khz_output_.Scale(ZX_SEC(10)), rate_1khz_output_);
  max_gain_scale = gain_.CalculateScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  EXPECT_THAT(scale_arr, Each(FloatEq(expect)));
  EXPECT_FLOAT_EQ(max_gain_scale, expect);
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());
}
// Advances that exceed ramp durations should lead to end-to-ramp conditions.
TEST_F(SourceDestGainTest, CalculateScaleArrayBigAdvance) { TestScaleArrayBigAdvance(); }
TEST_F(DestSourceGainTest, CalculateScaleArrayBigAdvance) { TestScaleArrayBigAdvance(); }

void GainBase::TestRampCompletion() {
  Gain::AScale scale_arr[6];
  Gain::AScale scale_arr2[6];

  constexpr float target_gain_db = -30.1029995f;
  const float target_gain_scale = media_audio::DbToScale(target_gain_db);

  // With a 5ms duration and 1 frame per ms, scale_arr will perfectly fit
  // each frame such that scale_arr[5] == target_gain_scale.
  SetGainWithRamp(target_gain_db, zx::msec(5));
  auto max_gain_scale =
      gain_.CalculateScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  EXPECT_FLOAT_EQ(max_gain_scale, media_audio::kUnityGainScale);
  for (size_t k = 0; k < std::size(scale_arr); k++) {
    const float diff = media_audio::kUnityGainScale - target_gain_scale;
    const float want = media_audio::kUnityGainScale - diff * static_cast<float>(k) / 5.0f;
    EXPECT_FLOAT_EQ(want, scale_arr[k]) << "index " << k;
  }

  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_EQ(media_audio::kUnityGainDb, gain_.GetGainDb());
  EXPECT_EQ(media_audio::kUnityGainScale, gain_.GetGainScale());

  // After clearing the ramp, scale_arr should be constant.
  CompleteRamp();
  max_gain_scale = gain_.CalculateScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  EXPECT_THAT(scale_arr, Each(FloatEq(target_gain_scale)));
  EXPECT_FLOAT_EQ(max_gain_scale, target_gain_scale);
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_EQ(target_gain_db, gain_.GetGainDb());
  EXPECT_EQ(target_gain_scale, gain_.GetGainScale());
  EXPECT_FLOAT_EQ(target_gain_db, gain_.GetGainDb());

  // Without a ramp, scale_arr should be constant even after Advance.
  gain_.Advance(10, rate_1khz_output_);
  max_gain_scale = gain_.CalculateScaleArray(scale_arr2, std::size(scale_arr2), rate_1khz_output_);

  EXPECT_THAT(scale_arr, Each(FloatEq(target_gain_scale)));
  EXPECT_FLOAT_EQ(max_gain_scale, target_gain_scale);
  EXPECT_FALSE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_EQ(target_gain_db, gain_.GetGainDb());
  EXPECT_EQ(target_gain_scale, gain_.GetGainScale());
}

// Completing a ramp should fast-forward any in-process ramps.
TEST_F(SourceDestGainTest, CompleteSourceRamp) { TestRampCompletion(); }
TEST_F(DestSourceGainTest, CompleteDestRamp) { TestRampCompletion(); }

void GainBase::TestAdvanceHalfwayThroughRamp() {
  Gain::AScale scale_arr[4];  // At 1kHz this is less than the ramp duration.
  Gain::AScale expect_arr[4];

  SetGainWithRamp(-20.0f, zx::msec(9));
  auto max_gain_scale =
      gain_.CalculateScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  EXPECT_FLOAT_EQ(gain_.GetGainScale(), media_audio::kUnityGainScale);
  EXPECT_FLOAT_EQ(max_gain_scale, media_audio::kUnityGainScale);

  // When comparing buffers, do it within the tolerance of 32-bit float
  Gain::AScale expect_scale = media_audio::kUnityGainScale;
  for (auto& val : expect_arr) {
    val = expect_scale;
    expect_scale -= 0.1f;
  }

  EXPECT_THAT(scale_arr, Pointwise(FloatEq(), expect_arr));
  EXPECT_FALSE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_TRUE(gain_.IsRamping());

  // Advance only partially through the duration of the ramp.
  const auto kFramesToAdvance = 2;
  gain_.Advance(kFramesToAdvance, rate_1khz_output_);
  max_gain_scale = gain_.CalculateScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

  expect_scale = expect_arr[kFramesToAdvance];

  EXPECT_FLOAT_EQ(expect_scale, gain_.GetGainScale());
  EXPECT_FLOAT_EQ(max_gain_scale, expect_scale);

  for (auto& val : expect_arr) {
    val = expect_scale;
    expect_scale -= 0.1f;
  }

  EXPECT_THAT(scale_arr, Pointwise(FloatEq(), expect_arr));
  EXPECT_TRUE(gain_.IsRamping());
  EXPECT_FALSE(gain_.IsUnity());
  EXPECT_FALSE(gain_.IsSilent());
}
// After partial Advance through a ramp, instantaneous gain should be accurate.
TEST_F(SourceDestGainTest, AdvanceHalfwayThroughRamp) { TestAdvanceHalfwayThroughRamp(); }
TEST_F(DestSourceGainTest, AdvanceHalfwayThroughRamp) { TestAdvanceHalfwayThroughRamp(); }

// After partial Advance through a ramp, followed by a second ramp, the second ramp
// ramp should start where the first ramp left off.
void GainBase::TestSuccessiveRamps() {
  SetGainWithRamp(-20.0f, zx::msec(10));
  auto scale_start = media_audio::kUnityGainScale;

  EXPECT_FLOAT_EQ(scale_start, gain_.GetGainScale());
  EXPECT_TRUE(gain_.IsRamping());

  // Advance only partially through the duration of the ramp.
  gain_.Advance(2, rate_1khz_output_);  // 1 frame == 1ms
  float expect_scale = scale_start + (media_audio::DbToScale(-20.f) - scale_start) * 2.0f / 10.0f;

  EXPECT_FLOAT_EQ(expect_scale, gain_.GetGainScale());
  EXPECT_TRUE(gain_.IsRamping());

  // A new ramp should start at the same spot.
  SetGainWithRamp(-80.0f, zx::msec(10));
  scale_start = expect_scale;

  EXPECT_FLOAT_EQ(expect_scale, gain_.GetGainScale());
  EXPECT_TRUE(gain_.IsRamping());

  // Advance again.
  gain_.Advance(2, rate_1khz_output_);
  expect_scale = scale_start + (media_audio::DbToScale(-80.f) - scale_start) * 2.0f / 10.0f;

  EXPECT_FLOAT_EQ(expect_scale, gain_.GetGainScale());
  EXPECT_TRUE(gain_.IsRamping());
}
// After partial Advance through a ramp, followed by a second ramp, the second ramp
// ramp should start where the first ramp left off.
TEST_F(SourceDestGainTest, TwoRamps) { TestSuccessiveRamps(); }
TEST_F(DestSourceGainTest, TwoRamps) { TestSuccessiveRamps(); }

void GainBase::TestCombinedRamps() {
  Gain::AScale scale_arr[11];

  {
    // Two arbitrary ramps of the same length, starting at the same time
    SetGainWithRamp(-20, zx::msec(10));
    SetOtherGainWithRamp(+10, zx::msec(10));
    auto max_gain_scale =
        gain_.CalculateScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

    // Source gain ramps linearly from 0 dB (scale 1.0) to -20 dB (0.1)
    // Dest gain ramps linearly from 0 dB (1.0) to 10 dB (3.16227766)
    //
    // source 1.0 0.91000 0.82000 0.73000 0.64000 0.55000 0.46000 0.37000 0.28000 0.19000 0.10000
    // dest   1.0 1.22623 1.43246 1.64868 1.86491 2.08114 2.29737 2.51359 2.72982 2.94605 3.16228
    //
    // These scale values are multiplied to get the following expect_arr
    Gain::AScale expect_arr[11] = {
        1.0f,       1.1067673f, 1.1746135f, 1.2035388f, 1.1935431f, 1.1446264f,
        1.0567886f, 0.9300299f, 0.7643502f, 0.5597495f, 0.3162278f,
    };
    EXPECT_THAT(scale_arr, Pointwise(FloatEq(), expect_arr));
    EXPECT_FLOAT_EQ(max_gain_scale, expect_arr[3]);
  }

  {
    // Now check two ramps of differing lengths and start times
    SetGain(0.0);
    SetOtherGain(-40);
    SetGainWithRamp(-80, zx::msec(10));
    gain_.Advance(5, rate_1khz_output_);

    // At the source-ramp midpoint, source * dest contributions are 0.50005 * 0.01
    EXPECT_FLOAT_EQ(gain_.GetGainScale(), 0.005000501f);
    SetOtherGainWithRamp(15, zx::msec(7));
    auto max_gain_scale =
        gain_.CalculateScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

    // source ramp continues onward, finalizing at 0.0001 on frame 5. dest ramp ends on frame 7 at
    // 5.6234133. They combine for 0.0005623413 which should be set for the remaining array.
    Gain::AScale expect_arr[11] = {
        0.005000501f,   0.32481519f,    0.48426268f,    0.48334297f,
        0.32205606f,    0.00040195809f, 0.00048214971f, 0.00056234133f,
        0.00056234133f, 0.00056234133f, 0.00056234133f,
    };
    EXPECT_THAT(scale_arr, Pointwise(FloatEq(), expect_arr));
    EXPECT_FLOAT_EQ(max_gain_scale, expect_arr[2]);
  }
}

// Test that source-ramping and dest-ramping combines correctly
TEST_F(SourceDestGainTest, CombinedRamps) { TestCombinedRamps(); }
TEST_F(DestSourceGainTest, CombinedRamps) { TestCombinedRamps(); }

void GainBase::TestCrossFades() {
  Gain::AScale scale_arr[11];

  constexpr float kInitialGainDb1 = -20.0f;
  constexpr float kInitialGainDb2 = 0.0f;
  constexpr float kGainChangeDb = 8.0f;
  for (size_t ramp_length = 4; ramp_length <= 8; ramp_length += 2) {
    SCOPED_TRACE("GainBase::TestCrossFades for ramp_length " + std::to_string(ramp_length));

    ASSERT_EQ(ramp_length % 2, 0u) << "Test miscalculation - test assumes ramp_length is even";

    // We set the two ramps with equal duration and offsetting gain-change.
    // Scale-linear crossfading is not equal-power, so although the initial and final gain_db values
    // are equal, the intervening values actually rise to a local max at fade's midpoint.
    SetGain(kInitialGainDb1);
    SetOtherGain(kInitialGainDb2);
    SetGainWithRamp(kInitialGainDb1 + kGainChangeDb, zx::msec(ramp_length));
    SetOtherGainWithRamp(kInitialGainDb2 - kGainChangeDb, zx::msec(ramp_length));
    auto max_gain_scale =
        gain_.CalculateScaleArray(scale_arr, std::size(scale_arr), rate_1khz_output_);

    // scale values are given below for the ramp_length = 4 case:
    // source 0.10000000  0.13779716  0.17559432  0.21339148  0.25118864  0.25118864 ...
    // dest   1.00000000  0.84952679  0.69905359  0.54858038  0.39810717  0.39810717 ...
    // multiplied to get:
    // expect 0.10000000  0.11706238  0.12274984  0.11706238  0.10000000  0.10000000 ...

    // Rather than comparing strictly, check the logical shape:
    // * At either end of the ramps, the gains are equal
    EXPECT_FLOAT_EQ(scale_arr[0], media_audio::DbToScale(kInitialGainDb1 + kInitialGainDb2));
    EXPECT_FLOAT_EQ(scale_arr[ramp_length], scale_arr[0]);
    EXPECT_FLOAT_EQ(max_gain_scale, scale_arr[ramp_length / 2]);

    // * Gain increases monotonically to the midpoint of the ramps
    EXPECT_GT(scale_arr[ramp_length / 2 - 1], scale_arr[ramp_length / 2 - 2]);
    EXPECT_GT(scale_arr[ramp_length / 2], scale_arr[ramp_length / 2 - 1]);

    // * Gain decreases monotonically as we move beyond the midpoint of the ramps
    EXPECT_GT(scale_arr[ramp_length / 2], scale_arr[ramp_length / 2 + 1]);
    EXPECT_GT(scale_arr[ramp_length / 2 + 1], scale_arr[ramp_length / 2 + 2]);

    // * The end-ramp gain holds constant to the end of scale_arr
    EXPECT_FLOAT_EQ(scale_arr[std::size(scale_arr) - 1], scale_arr[ramp_length]);
  }
}
// Check two coincident ramps that offset each other. Because scale-linear ramping is not
// equal-power, the result won't be constant-gain, but it will have a predictable shape.
TEST_F(SourceDestGainTest, CrossFades) { TestCrossFades(); }
TEST_F(DestSourceGainTest, CrossFades) { TestCrossFades(); }

void GainBase::TestScaleArrayForMinScale() {
  Gain::AScale scale_arr[6];

  // Already below the silence threshold and ramping downward
  SetGain(-80.0f);
  SetOtherGain(-80.0f);
  SetGainWithRamp(-90.0f, zx::msec(5));
  gain_.CalculateScaleArray(scale_arr, std::size(scale_arr), TimelineRate(1000, ZX_SEC(1)));

  EXPECT_THAT(scale_arr, Each(FloatEq(Gain::kMuteScale)));
  EXPECT_TRUE(gain_.IsSilent());
  EXPECT_TRUE(gain_.IsRamping());

  // Ramping upward, but other stage is below mute threshold
  SetGain(10.0f);
  SetOtherGain(media_audio::kMinGainDb);
  SetGainWithRamp(12.0f, zx::sec(1));
  gain_.CalculateScaleArray(scale_arr, std::size(scale_arr), TimelineRate(1000, ZX_SEC(1)));

  EXPECT_THAT(scale_arr, Each(FloatEq(Gain::kMuteScale)));
  EXPECT_TRUE(gain_.IsSilent());
  EXPECT_TRUE(gain_.IsRamping());

  // Ramping upward, to a target below mute threshold
  SetGain(media_audio::kMinGainDb - 5.0f);
  SetOtherGain(10.0f);
  SetGainWithRamp(media_audio::kMinGainDb, zx::sec(1));
  gain_.CalculateScaleArray(scale_arr, std::size(scale_arr), TimelineRate(1000, ZX_SEC(1)));

  EXPECT_THAT(scale_arr, Each(FloatEq(Gain::kMuteScale)));
  EXPECT_TRUE(gain_.IsSilent());
  EXPECT_FALSE(gain_.IsRamping());  // entirely below mute threshold, regardless of other stage
}
// Setting a static gain during ramping should cancel the ramp
TEST_F(SourceDestGainTest, ScaleBelowMinShouldBeMuteScale) { TestScaleArrayForMinScale(); }
TEST_F(DestSourceGainTest, ScaleBelowMinShouldBeMuteScale) { TestScaleArrayForMinScale(); }

// Tests for Set{Min,Max}Gain.

// GetGainDb cannot go lower than .min_gain_db (unless <= media_audio::kMinGainDb)
TEST(GainLimitsTest, LimitedByMinGain) {
  Gain gain({
      .min_gain_db = -30,
  });
  gain.SetSourceGain(-20);
  gain.SetDestGain(-20);

  EXPECT_FLOAT_EQ(gain.GetGainDb(), -30);
  EXPECT_FALSE(gain.IsSilent());
}

// GetGainDb cannot go higher than .max_gain_db
TEST(GainLimitsTest, LimitedByMaxGain) {
  Gain gain({
      .max_gain_db = 3,
  });
  gain.SetSourceGain(2);
  gain.SetDestGain(2);

  EXPECT_FLOAT_EQ(gain.GetGainDb(), 3);
}

TEST(GainLimitsTest, AllowedWhenSourceDestInRange) {
  Gain gain({
      .min_gain_db = -40,
      .max_gain_db = -10,
  });
  gain.SetSourceGain(-15);
  gain.SetDestGain(-15);

  EXPECT_FLOAT_EQ(gain.GetGainDb(), -30);
}

// Even if dest gain in isolation is less than .min_gain_db,
// gain is only limited if the combined gain is outside the specified limits
TEST(GainLimitsTest, AllowedWhenSourceInRange) {
  Gain gain({
      .min_gain_db = -10,
      .max_gain_db = 10,
  });
  gain.SetSourceGain(5);
  gain.SetDestGain(-11);

  EXPECT_FLOAT_EQ(gain.GetGainDb(), -6);
}

// Even if source gain in isolation is less than .min_gain_db,
// gain is only limited if the combined gain is outside the specified limits
TEST(GainLimitsTest, AllowedWhenDestInRange) {
  Gain gain({
      .min_gain_db = -10,
      .max_gain_db = 10,
  });
  gain.SetSourceGain(-11);
  gain.SetDestGain(5);

  EXPECT_FLOAT_EQ(gain.GetGainDb(), -6);
}

// Even if source gain and dest gain are both individually greater than .max_gain_db,
// gain is only limited if the combined gain is outside the specified limits
TEST(GainLimitsTest, AllowedWhenSourceDestHigh) {
  Gain gain({
      .min_gain_db = -20,
      .max_gain_db = -10,
  });
  gain.SetSourceGain(-6);
  gain.SetDestGain(-6);

  EXPECT_FLOAT_EQ(gain.GetGainDb(), -12);
}

// Even if source gain and dest gain are both individually less than .min_gain_db,
// gain is only limited if the combined gain is outside the specified limits
TEST(GainLimitsTest, AllowedWhenSourceDestLow) {
  Gain gain({
      .min_gain_db = 5,
      .max_gain_db = 10,
  });
  gain.SetSourceGain(3);
  gain.SetDestGain(3);

  EXPECT_FLOAT_EQ(gain.GetGainDb(), 6);
}

// The only value below the min_gain limit that can be returned is kMuteScale or
// media_audio::kMinGainDb.

// kMuteScale should be returned if the source gain is less than or equal to
// media_audio::kMinGainDb.
TEST(GainLimitsTest, PreserveSourceMuteGain) {
  Gain gain({
      .min_gain_db = -10,
  });
  gain.SetSourceGain(media_audio::kMinGainDb);

  EXPECT_FLOAT_EQ(gain.GetGainScale(), Gain::kMuteScale);
  EXPECT_FLOAT_EQ(gain.GetGainDb(), media_audio::kMinGainDb);
  EXPECT_TRUE(gain.IsSilent());
}

// kMuteScale should be returned if the dest gain is less than or equal to media_audio::kMinGainDb.
TEST(GainLimitsTest, PreserveDestMuteGain) {
  Gain gain({
      .min_gain_db = -10,
  });
  gain.SetDestGain(media_audio::kMinGainDb);

  EXPECT_FLOAT_EQ(gain.GetGainScale(), Gain::kMuteScale);
  EXPECT_FLOAT_EQ(gain.GetGainDb(), media_audio::kMinGainDb);
  EXPECT_TRUE(gain.IsSilent());
}

// kMuteScale should be returned if the source mute is set, regardless of source gain
TEST(GainLimitsTest, PreserveSourceMute) {
  Gain gain({
      .min_gain_db = -10,
  });
  gain.SetSourceGain(-15);
  gain.SetSourceMute(true);

  EXPECT_FLOAT_EQ(gain.GetGainScale(), Gain::kMuteScale);
  EXPECT_FLOAT_EQ(gain.GetGainDb(), media_audio::kMinGainDb);
  EXPECT_TRUE(gain.IsSilent());
}

// A gain-limit range that includes unity gain should allow this, whether by default ctor or by
// combination of source and dest gain values that may individually exceed gain limits.
TEST(GainLimitsTest, PreserveIsUnity) {
  Gain gain({
      .min_gain_db = -4.0f,
      .max_gain_db = 1.0f,
  });

  EXPECT_FLOAT_EQ(gain.GetGainScale(), media_audio::kUnityGainScale);
  EXPECT_FLOAT_EQ(gain.GetGainDb(), media_audio::kUnityGainDb);
  EXPECT_TRUE(gain.IsUnity());

  // source below the limit, dest above the limit
  gain.SetSourceGain(-6.0f);
  gain.SetDestGain(6.0f);

  EXPECT_FLOAT_EQ(gain.GetGainScale(), media_audio::kUnityGainScale);
  EXPECT_FLOAT_EQ(gain.GetGainDb(), media_audio::kUnityGainDb);
  EXPECT_TRUE(gain.IsUnity());

  // source above the limit, dest below the limit
  gain.SetSourceGain(12.0f);
  gain.SetDestGain(-12.0f);

  EXPECT_FLOAT_EQ(gain.GetGainScale(), media_audio::kUnityGainScale);
  EXPECT_FLOAT_EQ(gain.GetGainDb(), media_audio::kUnityGainDb);
  EXPECT_TRUE(gain.IsUnity());
}

// A gain-limit range that excludes unity gain should never return kUnityGainDb or kUnityGainScale,
// whether by default ctor or combination of source and dest values.
TEST(GainLimitsTest, PreventIsUnity) {
  Gain gain({
      .max_gain_db = -5.0f,
  });

  EXPECT_FLOAT_EQ(gain.GetGainDb(), -5.0f);
  EXPECT_FALSE(gain.IsUnity());

  gain.SetSourceGain(media_audio::kUnityGainDb);
  gain.SetDestGain(media_audio::kUnityGainDb);

  EXPECT_FLOAT_EQ(gain.GetGainDb(), -5.0f);
  EXPECT_FALSE(gain.IsUnity());

  gain.SetSourceGain(media_audio::kUnityGainDb + 1.0f);
  gain.SetDestGain(media_audio::kUnityGainDb - 1.0f);

  EXPECT_FLOAT_EQ(gain.GetGainDb(), -5.0f);
  EXPECT_FALSE(gain.IsUnity());
}

// To simplify the following gain ramp tests, we use frame rate 1kHz: 1 frame per millisec.

// Gain ramping that begins outside gain limits is constrained to the range, even at ramp-start.
// Gain ramping that ends outside gain limits is constrained to the range, thru to ramp-end.

// Source gain (ramping from below gain-limit range, to above gain-limit range) is constrained.
TEST(GainLimitsTest, SourceRampUp) {
  Gain::AScale scale_arr[6];
  // With no limits, would be: {0.10f, 0.28f, 0.46f, 0.64f, 0.82f, 1.00f};
  Gain::AScale expect_arr[6] = {0.30f, 0.30f, 0.46f, 0.64f, 0.80f, 0.80f};

  Gain gain({
      .min_gain_db = media_audio::ScaleToDb(0.30f),
      .max_gain_db = media_audio::ScaleToDb(0.80f),
  });
  gain.SetSourceGain(-20);
  gain.SetSourceGainWithRamp(0, zx::msec(5));
  auto max_gain_scale =
      gain.CalculateScaleArray(scale_arr, std::size(scale_arr), TimelineRate(1000, ZX_SEC(1)));

  EXPECT_THAT(scale_arr, Pointwise(FloatEq(), expect_arr));
  EXPECT_FLOAT_EQ(max_gain_scale, expect_arr[5]);
  EXPECT_TRUE(gain.IsRamping());
}

// Dest gain (ramping from above gain-limit range, to below gain-limit range) is constrained.
TEST(GainLimitsTest, DestRampDown) {
  Gain::AScale scale_arr[6];
  // With no limits, would be: {1.00f, 0.82f, 0.64f, 0.46f, 0.28f, 0.10f};
  Gain::AScale expect_arr[6] = {0.80f, 0.80f, 0.64f, 0.46f, 0.30f, 0.30f};

  Gain gain({
      .min_gain_db = media_audio::ScaleToDb(0.30f),
      .max_gain_db = media_audio::ScaleToDb(0.80f),
  });
  gain.SetDestGainWithRamp(-20, zx::msec(5));
  auto max_gain_scale =
      gain.CalculateScaleArray(scale_arr, std::size(scale_arr), TimelineRate(1000, ZX_SEC(1)));

  EXPECT_THAT(scale_arr, Pointwise(FloatEq(), expect_arr));
  EXPECT_FLOAT_EQ(max_gain_scale, expect_arr[0]);
  EXPECT_TRUE(gain.IsRamping());
}

// Gain ramping that begins and remains entirely outside gain limits is constrained to range. This
// must still be considered "ramping", because a subsequent change to the companion dest or source
// gain might bring total gain into range, and thus the client must advance the ramp normally.
TEST(GainLimitsTest, SourceRampEntirelyBelowMin) {
  Gain gain({
      .min_gain_db = -11,
  });
  gain.SetSourceGain(-15);
  gain.SetSourceGainWithRamp(-16, zx::sec(1));

  EXPECT_FLOAT_EQ(gain.GetGainDb(), -11);
  EXPECT_TRUE(gain.IsRamping());
}

TEST(GainLimitsTest, DestRampEntirelyBelowMin) {
  Gain gain({
      .min_gain_db = -11,
  });
  gain.SetDestGain(-15);
  gain.SetDestGainWithRamp(-16, zx::sec(1));

  EXPECT_FLOAT_EQ(gain.GetGainDb(), -11);
  EXPECT_TRUE(gain.IsRamping());
}

// CalculateScaleArray is callable even if no ramp is active; the returned array must obey
// gain-limits.
TEST(GainLimitsTest, GainScaleArrayRespectsMinWhenNotRamping) {
  Gain::AScale scale_arr[6];
  Gain::AScale expect_arr[6] = {0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f};

  Gain gain({
      .min_gain_db = media_audio::ScaleToDb(0.20f),
      .max_gain_db = media_audio::ScaleToDb(0.80f),
  });
  gain.SetSourceGain(media_audio::ScaleToDb(0.1f));
  auto max_gain_scale =
      gain.CalculateScaleArray(scale_arr, std::size(scale_arr), TimelineRate(1000, ZX_SEC(1)));

  EXPECT_THAT(scale_arr, Pointwise(FloatEq(), expect_arr));
  EXPECT_FLOAT_EQ(max_gain_scale, expect_arr[0]);
  EXPECT_FALSE(gain.IsRamping());
}

TEST(GainLimitsTest, GainScaleArrayRespectsMaxWhenNotRamping) {
  Gain::AScale scale_arr[6];
  Gain::AScale expect_arr[6] = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};

  Gain gain({
      .min_gain_db = media_audio::ScaleToDb(0.20f),
      .max_gain_db = media_audio::ScaleToDb(0.80f),
  });
  gain.SetDestGain(media_audio::ScaleToDb(0.9f));
  auto max_gain_scale =
      gain.CalculateScaleArray(scale_arr, std::size(scale_arr), TimelineRate(1000, ZX_SEC(1)));

  EXPECT_THAT(scale_arr, Pointwise(FloatEq(), expect_arr));
  EXPECT_FLOAT_EQ(max_gain_scale, expect_arr[0]);
  EXPECT_FALSE(gain.IsRamping());
}

}  // namespace
}  // namespace media::audio
