// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/present1_helper.h"

#include <gtest/gtest.h>

namespace scheduling {
namespace test {

// Register three presents and see that they fire in the correct order, with the appropriate
// arguments.
TEST(Present1HelperTest, OnPresented_ShouldTriggerCallbacksCorrectly) {
  Present1Helper helper;

  int32_t callback_count = 0;
  int32_t callback1 = -1;
  int32_t callback2 = -1;
  int32_t callback3 = -1;
  fuchsia::images::PresentationInfo last_presentation_info;
  helper.RegisterPresent(
      /*present_id*/ 1,
      /*callback*/
      [&callback_count, &callback1,
       &last_presentation_info](fuchsia::images::PresentationInfo info) {
        callback1 = ++callback_count;
        last_presentation_info = std::move(info);
      });
  helper.RegisterPresent(
      /*present_id*/ 2,
      /*callback*/
      [&callback_count, &callback2,
       &last_presentation_info](fuchsia::images::PresentationInfo info) {
        callback2 = ++callback_count;
        last_presentation_info = std::move(info);
      });
  helper.RegisterPresent(
      /*present_id*/ 3,
      /*callback*/
      [&callback_count, &callback3,
       &last_presentation_info](fuchsia::images::PresentationInfo info) {
        callback3 = ++callback_count;
        last_presentation_info = std::move(info);
      });

  EXPECT_EQ(callback1, -1);
  EXPECT_EQ(callback2, -1);
  EXPECT_EQ(callback3, -1);

  {  // Trigger callbacks for present_id 1 and 2.
    std::map<PresentId, zx::time> latched_times;
    latched_times.emplace(1u, zx::time(1));
    latched_times.emplace(2u, zx::time(2));
    PresentTimestamps present_times{
        .presented_time = zx::time(23),
        .vsync_interval = zx::duration(124),
    };

    helper.OnPresented(latched_times, present_times);

    EXPECT_EQ(callback1, 1);
    EXPECT_EQ(callback2, 2);
    EXPECT_EQ(callback3, -1);
    EXPECT_EQ(last_presentation_info.presentation_time,
              static_cast<uint64_t>(present_times.presented_time.get()));
    EXPECT_EQ(last_presentation_info.presentation_interval,
              static_cast<uint64_t>(present_times.vsync_interval.get()));
  }

  {  // Trigger callbacks for present_id 3.
    std::map<PresentId, zx::time> latched_times;
    latched_times.emplace(3u, zx::time(5));
    PresentTimestamps present_times{
        .presented_time = zx::time(60),
        .vsync_interval = zx::duration(12),
    };

    helper.OnPresented(latched_times, present_times);

    EXPECT_EQ(callback1, 1);  // Should not be triggered again.
    EXPECT_EQ(callback2, 2);  // Should not be triggered again.
    EXPECT_EQ(callback3, 3);
    EXPECT_EQ(last_presentation_info.presentation_time,
              static_cast<uint64_t>(present_times.presented_time.get()));
    EXPECT_EQ(last_presentation_info.presentation_interval,
              static_cast<uint64_t>(present_times.vsync_interval.get()));
  }
}

}  // namespace test
}  // namespace scheduling
