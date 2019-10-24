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

// Sort |Present2Info|s regardless of timestamp order.
TEST(Present2InfoTest, SortPresent2Infos) {
  constexpr zx::time kInitialPresentReceivedTime = zx::time(30);
  constexpr zx::time kInitialLatchedTime = zx::time(50);
  constexpr zx::time kActualPresentationTime = zx::time(70);

  constexpr uint64_t kNumSessions = 9;
  constexpr uint64_t kPresentsPerSession = 3;

  // Create the queue of |Present2Info|s in unsorted order, by having the outer loop be
  // |kPresentsPerSession|. We also, by subtracting |i| and |j| from the initial time, ensure
  // that the |Present2Info|s are in order of decreasing timestamps.
  std::queue<Present2Info> present2_infos;
  for (uint64_t i = 0; i < kPresentsPerSession; ++i) {
    for (uint64_t j = 0; j < kNumSessions; ++j) {
      Present2Info info = Present2Info(j);

      info.SetPresentReceivedTime(zx::time(kInitialPresentReceivedTime.get() - (i + j)));
      info.SetLatchedTime(zx::time(kInitialLatchedTime.get() - (i + j)));

      present2_infos.push(std::move(info));
    }
  }

  EXPECT_EQ(present2_infos.size(), kNumSessions * kPresentsPerSession);

  auto present2_info_map = Present2Info::SortPresent2Infos(std::move(present2_infos));
  EXPECT_EQ(present2_info_map.size(), kNumSessions);

  std::map<SessionId, std::vector<Present2Info>>::iterator it = present2_info_map.begin();
  while (it != present2_info_map.end()) {
    EXPECT_EQ(it->second.size(), kPresentsPerSession);

    int session_id = static_cast<int>(it->first);
    std::vector<Present2Info> infos = std::move(it->second);

    for (uint64_t i = 0; i < kPresentsPerSession; ++i) {
      fuchsia::scenic::scheduling::PresentReceivedInfo info = infos[i].TakePresentReceivedInfo();

      EXPECT_EQ(info.present_received_time(),
                static_cast<int64_t>(kInitialPresentReceivedTime.get() - (i + session_id)));
      EXPECT_EQ(info.latched_time(),
                static_cast<int64_t>(kInitialLatchedTime.get() - (i + session_id)));
    }

    ++it;
  }
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

// Have two sessions interleave multiple Present2s with each other.
TEST(Present2InfoTest, MultiplePresent2s) {
  constexpr zx::time kInitialPresentReceivedTime = zx::time(10);
  constexpr zx::time kInitialLatchedTime = zx::time(20);
  constexpr zx::time kActualPresentationTime = zx::time(30);

  constexpr SessionId session1_id = 6;
  constexpr SessionId session2_id = 7;
  OnFramePresentedCallback session1_callback;
  OnFramePresentedCallback session2_callback;
  bool session1_callback_signaled = false;
  bool session2_callback_signaled = false;

  constexpr uint64_t kPresentsPerSession = 3;
  constexpr int kNumSessions = 2;
  constexpr int kTotalPresents = kNumSessions * kPresentsPerSession;

  // Create a callback per Session.
  for (int i = 0; i < kNumSessions; ++i) {
    bool& callback_signaled = (i == 0) ? session1_callback_signaled : session2_callback_signaled;

    OnFramePresentedCallback callback = [kInitialPresentReceivedTime, kInitialLatchedTime,
                                         kPresentsPerSession, kActualPresentationTime, i,
                                         &callback_signaled](
                                            fuchsia::scenic::scheduling::FramePresentedInfo info) {
      EXPECT_EQ(info.actual_presentation_time, kActualPresentationTime.get());

      // Ensure correctness for all included presentation_infos, including they are in submitted
      // order.
      EXPECT_EQ(info.presentation_infos.size(), kPresentsPerSession);
      for (uint64_t j = 0; j < kPresentsPerSession; ++j) {
        // Calculate the present received and latched times from the initial times.
        const zx::time kPresentReceivedTime =
            zx::time(kInitialPresentReceivedTime.get() + (j * kNumSessions) + i);
        const zx::time kLatchedTime = zx::time(kInitialLatchedTime.get() + (j * kNumSessions) + i);

        EXPECT_EQ(zx::time(info.presentation_infos[j].latched_time()), kLatchedTime);
        EXPECT_EQ(zx::time(info.presentation_infos[j].present_received_time()),
                  kPresentReceivedTime);
      }

      callback_signaled = true;
    };

    if (i == 0) {
      session1_callback = std::move(callback);
    } else {
      session2_callback = std::move(callback);
    }
  }

  // Create a Present2Info per Present2.
  std::queue<Present2Info> present2_infos = {};
  for (int i = 0; i < kTotalPresents; ++i) {
    // Calculate the present received and latched times from the initial times.
    const zx::time kPresentReceivedTime = zx::time(kInitialPresentReceivedTime.get() + i);
    const zx::time kLatchedTime = zx::time(kInitialLatchedTime.get() + i);
    const SessionId session_id = (i % 2 == 0) ? session1_id : session2_id;

    Present2Info present2_info = Present2Info(session_id);

    present2_info.SetLatchedTime(kLatchedTime);
    present2_info.SetPresentReceivedTime(kPresentReceivedTime);

    present2_infos.push(std::move(present2_info));
  }

  // Generate the map of SessionId->Vector<Present2Info> and then call the associated callbacks.
  auto present2_info_map = Present2Info::SortPresent2Infos(std::move(present2_infos));
  std::map<SessionId, std::vector<Present2Info>>::iterator it = present2_info_map.begin();

  while (it != present2_info_map.end()) {
    SessionId session_id = it->first;
    auto vec = std::move(it->second);

    auto frame_presented_info =
        Present2Info::CoalescePresent2Infos(std::move(vec), kActualPresentationTime);
    if (session_id == session1_id) {
      session1_callback(std::move(frame_presented_info));
    } else if (session_id == session2_id) {
      session2_callback(std::move(frame_presented_info));
    } else {
      ASSERT_TRUE(false);
    }

    ++it;
  }

  EXPECT_TRUE(session1_callback_signaled && session2_callback_signaled);
}

}  // namespace test
}  // namespace scenic_impl
