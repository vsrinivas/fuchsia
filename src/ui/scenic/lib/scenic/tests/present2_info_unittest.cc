// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scenic/present2_info.h"

#include "gtest/gtest.h"

namespace scenic_impl {
namespace test {

using OnFramePresentedCallback =
    fit::function<void(fuchsia::scenic::scheduling::FramePresentedInfo info)>;

TEST(Present2InfoTest, SinglePresent2) {
  bool callback_signaled = false;

  constexpr zx::time kPresentReceivedTime = zx::time(3);
  constexpr zx::time kLatchedTime = zx::time(5);
  constexpr zx::time kActualPresentationTime = zx::time(10);
  constexpr SessionId session_id = 4;

  std::vector<Present2Info> vec = {};
  Present2Info present2_info = Present2Info(session_id);

  // Create the Present2 callback.
  OnFramePresentedCallback callback = [kPresentReceivedTime, kLatchedTime, kActualPresentationTime,
                                       &callback_signaled](
                                          fuchsia::scenic::scheduling::FramePresentedInfo info) {
    EXPECT_EQ(info.actual_presentation_time, kActualPresentationTime.get());

    // Ensure correctness for all included presentation_infos.
    EXPECT_EQ(info.presentation_infos.size(), 1u);
    EXPECT_EQ(zx::time(info.presentation_infos[0].latched_time()), kLatchedTime);
    EXPECT_EQ(zx::time(info.presentation_infos[0].present_received_time()), kPresentReceivedTime);

    callback_signaled = true;
  };

  present2_info.SetLatchedTime(kLatchedTime);
  present2_info.SetPresentReceivedTime(kPresentReceivedTime);

  vec.push_back(std::move(present2_info));

  auto frame_presented_info =
      Present2Info::CoalescePresent2Infos(std::move(vec), kActualPresentationTime);
  callback(std::move(frame_presented_info));

  EXPECT_TRUE(callback_signaled);
}

// Ensure that coalesced |Present2Info|s stay in submitted order, regardless of timestamp order.
TEST(Present2InfoTest, CoalescePresent2InfosFromSingleSession) {
  constexpr zx::time kInitialPresentReceivedTime = zx::time(10);
  constexpr zx::time kInitialLatchedTime = zx::time(20);
  constexpr zx::time kActualPresentationTime = zx::time(30);

  constexpr SessionId session_id = 7;
  constexpr uint64_t kNumPresents = 5;

  std::vector<Present2Info> present2_infos = {};
  for (uint64_t i = 0; i < kNumPresents; ++i) {
    Present2Info info = Present2Info(session_id);

    info.SetPresentReceivedTime(zx::time(kInitialPresentReceivedTime.get() - i));
    info.SetLatchedTime(zx::time(kInitialLatchedTime.get() - i));

    present2_infos.push_back(Present2Info(std::move(info)));
  }

  auto frame_presented_info =
      Present2Info::CoalescePresent2Infos(std::move(present2_infos), kActualPresentationTime);

  EXPECT_EQ(frame_presented_info.actual_presentation_time, kActualPresentationTime.get());
  EXPECT_EQ(frame_presented_info.presentation_infos.size(), kNumPresents);

  for (uint64_t i = 0; i < kNumPresents; ++i) {
    auto& info = frame_presented_info.presentation_infos[i];

    EXPECT_EQ(info.present_received_time(),
              static_cast<int64_t>(kInitialPresentReceivedTime.get() - i));
    EXPECT_EQ(info.latched_time(), static_cast<int64_t>(kInitialLatchedTime.get() - i));
  }
}

}  // namespace test
}  // namespace scenic_impl
