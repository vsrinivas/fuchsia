// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/lib/clock/testing/fake_audio_clock.h"

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/clock/testing/clock_test.h"

namespace media::audio::testing {

class FakeAudioClockTest : public ::testing::Test {
 protected:
  void SetUp() override { clock_factory_ = std::make_shared<FakeAudioClockFactory>(); }

  std::shared_ptr<FakeAudioClockFactory> clock_factory_;
};

TEST_F(FakeAudioClockTest, InitTransform) {
  auto under_test =
      clock_factory_->CreateClientAdjustable(audio::clock::AdjustableCloneOfMonotonic());

  auto ref_to_mono = under_test->ref_clock_to_clock_mono();
  EXPECT_EQ(ref_to_mono.subject_time(), 0);
  EXPECT_EQ(ref_to_mono.reference_time(), 0);
  EXPECT_EQ(ref_to_mono.subject_delta(), 1u);
  EXPECT_EQ(ref_to_mono.reference_delta(), 1u);
}

TEST_F(FakeAudioClockTest, InitTransform_Custom) {
  auto under_test =
      clock_factory_->CreateClientAdjustable(clock_factory_->mono_time() + zx::sec(5), 1000);

  auto ref_to_mono = under_test->ref_clock_to_clock_mono();
  EXPECT_EQ(ref_to_mono.subject_time(), 0);
  EXPECT_EQ(ref_to_mono.reference_time(), zx::sec(5).get());
  EXPECT_EQ(ref_to_mono.subject_delta(), 1000u);
  EXPECT_EQ(ref_to_mono.reference_delta(), 1001u);
}

//
// TimelineFunctions generate a piecewise linear transform, such that a TimelineFunction origin is
// at (subject_time, reference_time) and slope is the rate (subject_delta / reference_delta). In
// the following test cases, we verify clock rate updates by advancing mono_time on the new
// transform and checking the updated (subject_time, reference_time) values; the
// `ref_clock_to_clock_mono()` transform is only updated when `UpdateClockRate()` is called.
//

TEST_F(FakeAudioClockTest, UpdateClockRate) {
  auto clock = audio::clock::AdjustableCloneOfMonotonic();
  auto clock_id = audio::clock::GetKoid(clock);
  auto under_test = clock_factory_->CreateClientAdjustable(std::move(clock));

  clock_factory_->AdvanceMonoTimeBy(zx::sec(10));
  clock_factory_->UpdateClockRate(clock_id, /*rate_adjust_ppm=*/1000);

  auto ref_to_mono = under_test->ref_clock_to_clock_mono();
  EXPECT_EQ(ref_to_mono.subject_time(), zx::sec(10).get());
  EXPECT_EQ(ref_to_mono.reference_time(), zx::sec(10).get());
  EXPECT_EQ(ref_to_mono.subject_delta(), 1000u);
  EXPECT_EQ(ref_to_mono.reference_delta(), 1001u);

  clock_factory_->AdvanceMonoTimeBy(zx::sec(10));
  clock_factory_->UpdateClockRate(clock_id, /*rate_adjust_ppm=*/1);

  ref_to_mono = under_test->ref_clock_to_clock_mono();
  EXPECT_EQ(ref_to_mono.subject_time(), zx::sec(20).get());
  EXPECT_EQ(ref_to_mono.reference_time(), zx::msec(20010).get());
  EXPECT_EQ(ref_to_mono.subject_delta(), 1000000u);
  EXPECT_EQ(ref_to_mono.reference_delta(), 1000001u);
}

TEST_F(FakeAudioClockTest, UpdateRateAndAdvanceMono_CustomOffset) {
  auto under_test = clock_factory_->CreateClientFixed(clock_factory_->mono_time() + zx::sec(5), 0);
  auto clock_result = under_test->DuplicateClock();
  ASSERT_TRUE(clock_result.is_ok());
  auto clock_id = audio::clock::GetKoid(clock_result.take_value());

  auto ref_to_mono = under_test->ref_clock_to_clock_mono();
  EXPECT_EQ(ref_to_mono.subject_time(), 0);
  EXPECT_EQ(ref_to_mono.reference_time(), zx::sec(5).get());
  EXPECT_EQ(ref_to_mono.subject_delta(), 1u);
  EXPECT_EQ(ref_to_mono.reference_delta(), 1u);

  clock_factory_->AdvanceMonoTimeBy(zx::sec(10));
  clock_factory_->UpdateClockRate(clock_id, /*rate_adjust_ppm=*/1000);

  ref_to_mono = under_test->ref_clock_to_clock_mono();
  EXPECT_EQ(ref_to_mono.subject_time(), zx::sec(10).get());
  EXPECT_EQ(ref_to_mono.reference_time(), zx::sec(15).get());
  EXPECT_EQ(ref_to_mono.subject_delta(), 1000u);
  EXPECT_EQ(ref_to_mono.reference_delta(), 1001u);

  clock_factory_->AdvanceMonoTimeBy(zx::sec(10));
  clock_factory_->UpdateClockRate(clock_id, /*rate_adjust_ppm=*/100);

  ref_to_mono = under_test->ref_clock_to_clock_mono();
  EXPECT_EQ(ref_to_mono.subject_time(), zx::sec(20).get());
  EXPECT_EQ(ref_to_mono.reference_time(), zx::msec(25010).get());
  EXPECT_EQ(ref_to_mono.subject_delta(), 10000u);
  EXPECT_EQ(ref_to_mono.reference_delta(), 10001u);
}

TEST_F(FakeAudioClockTest, DupClockUpdates) {
  auto adjustable_clock = audio::clock::AdjustableCloneOfMonotonic();
  auto clock_id = audio::clock::GetKoid(adjustable_clock);
  auto adjustable_under_test = clock_factory_->CreateClientAdjustable(std::move(adjustable_clock));

  auto dup_result = adjustable_under_test->DuplicateClockReadOnly();
  ASSERT_TRUE(dup_result.is_ok());
  auto ref_under_test = clock_factory_->CreateClientFixed(dup_result.take_value());

  auto adjustable_tf = adjustable_under_test->ref_clock_to_clock_mono();
  auto ref_tf = ref_under_test->ref_clock_to_clock_mono();
  EXPECT_EQ(adjustable_tf.subject_time(), ref_tf.subject_time());
  EXPECT_EQ(adjustable_tf.reference_time(), ref_tf.reference_time());
  EXPECT_EQ(adjustable_tf.subject_delta(), ref_tf.subject_delta());
  EXPECT_EQ(adjustable_tf.reference_delta(), ref_tf.reference_delta());

  clock_factory_->AdvanceMonoTimeBy(zx::sec(10));
  clock_factory_->UpdateClockRate(clock_id, /*rate_adjust_ppm=*/-1000);

  adjustable_tf = adjustable_under_test->ref_clock_to_clock_mono();
  ref_tf = ref_under_test->ref_clock_to_clock_mono();
  EXPECT_EQ(adjustable_tf.subject_time(), zx::sec(10).get());
  EXPECT_EQ(adjustable_tf.reference_time(), zx::sec(10).get());
  EXPECT_EQ(ref_tf.subject_time(), zx::sec(10).get());
  EXPECT_EQ(ref_tf.reference_time(), zx::sec(10).get());
  EXPECT_EQ(adjustable_tf.subject_delta(), 1000u);
  EXPECT_EQ(adjustable_tf.reference_delta(), 999u);
  EXPECT_EQ(ref_tf.subject_delta(), 1000u);
  EXPECT_EQ(ref_tf.reference_delta(), 999u);

  clock_factory_->AdvanceMonoTimeBy(zx::sec(10));
  clock_factory_->UpdateClockRate(clock_id, /*rate_adjust_ppm=*/-10);

  adjustable_tf = adjustable_under_test->ref_clock_to_clock_mono();
  ref_tf = ref_under_test->ref_clock_to_clock_mono();
  EXPECT_EQ(adjustable_tf.subject_time(), zx::sec(20).get());
  EXPECT_EQ(adjustable_tf.reference_time(), zx::msec(19990).get());
  EXPECT_EQ(ref_tf.subject_time(), zx::sec(20).get());
  EXPECT_EQ(ref_tf.reference_time(), zx::msec(19990).get());
  EXPECT_EQ(adjustable_tf.subject_delta(), 100000u);
  EXPECT_EQ(adjustable_tf.reference_delta(), 99999u);
  EXPECT_EQ(ref_tf.subject_delta(), 100000u);
  EXPECT_EQ(ref_tf.reference_delta(), 99999u);
}

}  // namespace media::audio::testing
