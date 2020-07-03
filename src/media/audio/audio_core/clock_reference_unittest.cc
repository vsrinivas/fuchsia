// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/clock_reference.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/clone_mono.h"

namespace media::audio {
namespace {

// Verify copy ctor and copy assignment
TEST(ClockReferenceTest, ClockRefsAreCopyable) {
  // These two clocks may be precisely in-sync, but they are not the same object.
  auto clock = clock::AdjustableCloneOfMonotonic();
  auto clock2 = clock::CloneOfMonotonic();

  auto clock_ref = ClockReference::MakeAdjustable(clock);

  ClockReference copied_clock_ref(clock_ref);
  EXPECT_EQ(clock_ref.get().get_handle(), copied_clock_ref.get().get_handle());

  auto assigned_clock_ref = ClockReference::MakeReadonly(clock2);
  EXPECT_NE(clock_ref.get().get_handle(), assigned_clock_ref.get().get_handle());

  assigned_clock_ref = clock_ref;
  EXPECT_EQ(clock_ref.get().get_handle(), assigned_clock_ref.get().get_handle());
}

// Verify operator bool and is_valid()
TEST(ClockReferenceTest, IsValid) {
  ClockReference default_ref;
  EXPECT_FALSE(default_ref);
  EXPECT_FALSE(default_ref.is_valid());

  zx::clock uninitialized;
  auto uninitialized_ref = ClockReference::MakeReadonly(uninitialized);
  EXPECT_FALSE(uninitialized_ref);
  EXPECT_FALSE(uninitialized_ref.is_valid());

  auto clock = clock::CloneOfMonotonic();
  auto clock_ref = ClockReference::MakeReadonly(clock);
  EXPECT_TRUE(clock_ref);
  EXPECT_TRUE(clock_ref.is_valid());
}

TEST(ClockReferenceTest, ClockCanSubsequentlyBeSet) {
  zx::clock future_mono_clone;
  auto clock_ref = ClockReference::MakeReadonly(future_mono_clone);

  // Uninitialized clock is not yet running. This will set the clock in motion.
  clock::CloneMonotonicInto(&future_mono_clone);

  auto time1 = clock_ref.Read();
  auto time2 = clock_ref.Read();
  EXPECT_LT(time1, time2);
}

TEST(ClockReferenceTest, ClockMonoToRefClock) {
  auto clock = clock::AdjustableCloneOfMonotonic();
  auto clock_ref = ClockReference::MakeAdjustable(clock);

  auto tl_func = clock_ref.ref_clock_to_clock_mono();
  EXPECT_EQ(tl_func.reference_delta(), tl_func.subject_delta()) << "rate should be 1:1";

  zx::clock::update_args args;
  args.reset().set_rate_adjust(-1000);
  EXPECT_EQ(clock.update(args), ZX_OK) << "clock.update with rate_adjust failed";

  auto quick_tl_func = clock_ref.quick_ref_clock_to_clock_mono();
  EXPECT_EQ(quick_tl_func.reference_delta(), quick_tl_func.subject_delta())
      << "rate should still be 1:1";

  auto post_update_tl_func = clock_ref.ref_clock_to_clock_mono();
  EXPECT_LT(post_update_tl_func.reference_delta(), post_update_tl_func.subject_delta())
      << "rate should be less than 1:1";

  auto post_update_quick_tl_func = clock_ref.ref_clock_to_clock_mono();
  EXPECT_LT(post_update_quick_tl_func.reference_delta(), post_update_quick_tl_func.subject_delta())
      << "rate should be less than 1:1";
}

}  // namespace
}  // namespace media::audio
