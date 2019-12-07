// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/delegating_frame_scheduler.h"

#include "gtest/gtest.h"
#include "src/ui/scenic/lib/scheduling/tests/mocks/frame_scheduler_mocks.h"

namespace scheduling {
namespace test {

TEST(DelegatingFrameSchedulerTest, CallbacksFiredOnInitialization) {
  std::shared_ptr<MockFrameScheduler> empty_frame_scheduler;
  DelegatingFrameScheduler delegating_frame_scheduler(nullptr);

  auto frame_scheduler1 = std::make_shared<MockFrameScheduler>();

  // Set mock method callbacks.
  uint32_t num_schedule_update_callbacks = 0;
  uint32_t num_set_render_continuosly_callbacks = 0;
  uint32_t num_get_future_presentation_infos_callbacks = 0;
  uint32_t num_set_on_frame_presented_callback_for_session_callbacks = 0;
  {
    frame_scheduler1->set_schedule_update_for_session_callback(
        [&](auto, auto) { num_schedule_update_callbacks++; });
    frame_scheduler1->set_set_render_continuously_callback(
        [&](auto) { num_set_render_continuosly_callbacks++; });
    frame_scheduler1->set_get_future_presentation_infos_callback(
        [&](auto) -> std::vector<fuchsia::scenic::scheduling::PresentationInfo> {
          num_get_future_presentation_infos_callbacks++;
          return {};
        });
    frame_scheduler1->set_set_on_frame_presented_callback_for_session_callback(
        [&](auto, auto) { num_set_on_frame_presented_callback_for_session_callbacks++; });
  }

  // Call public methods on the DelegatingFrameScheduler.
  delegating_frame_scheduler.ScheduleUpdateForSession(zx::time(0), 0);
  delegating_frame_scheduler.SetRenderContinuously(true);
  delegating_frame_scheduler.GetFuturePresentationInfos(zx::duration(0), [](auto infos) {});
  delegating_frame_scheduler.SetOnFramePresentedCallbackForSession(0, [](auto info) {});

  EXPECT_EQ(0u, num_schedule_update_callbacks);
  EXPECT_EQ(0u, num_set_render_continuosly_callbacks);
  EXPECT_EQ(0u, num_get_future_presentation_infos_callbacks);
  EXPECT_EQ(0u, num_set_on_frame_presented_callback_for_session_callbacks);

  // Set a frame scheduler, mock method callbacks fired.
  delegating_frame_scheduler.SetFrameScheduler(frame_scheduler1);

  EXPECT_EQ(1u, num_schedule_update_callbacks);
  EXPECT_EQ(1u, num_set_render_continuosly_callbacks);
  EXPECT_EQ(1u, num_get_future_presentation_infos_callbacks);
  EXPECT_EQ(1u, num_set_on_frame_presented_callback_for_session_callbacks);

  // Set a different frame scheduler, no effect.
  auto frame_scheduler2 = std::make_shared<MockFrameScheduler>();
  delegating_frame_scheduler.SetFrameScheduler(frame_scheduler2);
  EXPECT_EQ(1u, num_schedule_update_callbacks);
  EXPECT_EQ(1u, num_set_render_continuosly_callbacks);
  EXPECT_EQ(1u, num_get_future_presentation_infos_callbacks);
  EXPECT_EQ(1u, num_set_on_frame_presented_callback_for_session_callbacks);

  // Invoke method after initialized, invoked immediately.
  delegating_frame_scheduler.ScheduleUpdateForSession(zx::time(0), 0);
  EXPECT_EQ(2u, num_schedule_update_callbacks);
}

}  // namespace test
}  // namespace scheduling
