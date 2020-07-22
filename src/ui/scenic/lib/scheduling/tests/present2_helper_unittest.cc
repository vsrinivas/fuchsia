// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/present2_helper.h"

#include <lib/async/default.h>
#include <lib/async/time.h>

#include <gtest/gtest.h>

namespace scheduling {
namespace test {

// Register three presents and see that they fire at the right time with the right arguments.
TEST(Present2HelperTest, OnPresented_ShouldTriggerCallbacksCorrectly) {
  std::optional<fuchsia::scenic::scheduling::FramePresentedInfo> presented_info;
  Present2Helper helper(
      /*on_frame_presented_event*/ [&presented_info](
                                       fuchsia::scenic::scheduling::FramePresentedInfo info) {
        presented_info.emplace(std::move(info));
      });

  helper.RegisterPresent(/*present_id*/ 1, /*present_received_time*/ zx::time(4));
  helper.RegisterPresent(/*present_id*/ 2, /*present_received_time*/ zx::time(5));
  helper.RegisterPresent(/*present_id*/ 3, /*present_received_time*/ zx::time(6));

  EXPECT_FALSE(presented_info);

  {  // Trigger callbacks for present_id 1 and 2.
    std::map<PresentId, zx::time> latched_times;
    latched_times.emplace(1u, zx::time(7));
    latched_times.emplace(2u, zx::time(8));
    PresentTimestamps present_times{
        .presented_time = zx::time(9),
        .vsync_interval = zx::duration(10),
    };

    helper.OnPresented(latched_times, present_times, /*num_presents_allowed*/ 2);
    ASSERT_TRUE(presented_info);
    auto& info = presented_info.value();
    EXPECT_EQ(info.actual_presentation_time, 9);
    EXPECT_EQ(info.num_presents_allowed, 2u);
    ASSERT_EQ(info.presentation_infos.size(), 2u);
    EXPECT_EQ(info.presentation_infos[0].present_received_time(), 4);
    EXPECT_EQ(info.presentation_infos[0].latched_time(), 7);
    EXPECT_EQ(info.presentation_infos[1].present_received_time(), 5);
    EXPECT_EQ(info.presentation_infos[1].latched_time(), 8);
  }

  {  // Trigger callback for 3.
    std::map<PresentId, zx::time> latched_times;
    latched_times.emplace(3u, zx::time(55));
    PresentTimestamps present_times{
        .presented_time = zx::time(111),
        .vsync_interval = zx::duration(222),
    };

    helper.OnPresented(latched_times, present_times, /*num_presents_allowed*/ 4);
    auto& info = presented_info.value();
    EXPECT_EQ(info.actual_presentation_time, 111);
    EXPECT_EQ(info.num_presents_allowed, 4u);
    ASSERT_EQ(info.presentation_infos.size(), 1u);
    EXPECT_EQ(info.presentation_infos[0].present_received_time(), 6);
    EXPECT_EQ(info.presentation_infos[0].latched_time(), 55);
  }
}

}  // namespace test
}  // namespace scheduling
