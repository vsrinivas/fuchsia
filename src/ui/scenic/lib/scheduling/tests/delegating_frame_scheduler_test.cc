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
  uint32_t num_register_present_callbacks = 0;
  scheduling::PresentId last_present_id = 0;

  uint32_t num_schedule_update_callbacks = 0;
  uint32_t num_set_render_continuosly_callbacks = 0;
  uint32_t num_get_future_presentation_infos_callbacks = 0;
  uint32_t num_set_on_frame_presented_callback_for_session_callbacks = 0;
  {
    frame_scheduler1->set_register_present_callback(
        [&](SessionId, std::variant<OnPresentedCallback, Present2Info>, std::vector<zx::event>,
            PresentId present_id) {
          num_register_present_callbacks++;
          last_present_id = present_id;
        });
    frame_scheduler1->set_schedule_update_for_session_callback(
        [&](auto...) { num_schedule_update_callbacks++; });
    frame_scheduler1->set_set_render_continuously_callback(
        [&](auto...) { num_set_render_continuosly_callbacks++; });
    frame_scheduler1->set_get_future_presentation_infos_callback(
        [&](auto...) -> std::vector<fuchsia::scenic::scheduling::PresentationInfo> {
          num_get_future_presentation_infos_callbacks++;
          return {};
        });
    frame_scheduler1->set_set_on_frame_presented_callback_for_session_callback(
        [&](auto, auto) { num_set_on_frame_presented_callback_for_session_callbacks++; });
  }

  const scheduling::SessionId kSessionId = 1;

  // Call public methods on the DelegatingFrameScheduler.
  const auto present_id1 = delegating_frame_scheduler.RegisterPresent(kSessionId, {}, {});
  delegating_frame_scheduler.ScheduleUpdateForSession(
      /*presentation_time=*/zx::time(0), {.session_id = kSessionId, .present_id = present_id1});
  delegating_frame_scheduler.SetRenderContinuously(true);
  delegating_frame_scheduler.GetFuturePresentationInfos(zx::duration(0), [](auto infos) {});
  delegating_frame_scheduler.SetOnFramePresentedCallbackForSession(0, [](auto info) {});

  EXPECT_EQ(0u, num_register_present_callbacks);
  EXPECT_EQ(0u, num_schedule_update_callbacks);
  EXPECT_EQ(0u, num_set_render_continuosly_callbacks);
  EXPECT_EQ(0u, num_get_future_presentation_infos_callbacks);
  EXPECT_EQ(0u, num_set_on_frame_presented_callback_for_session_callbacks);

  // Set a frame scheduler, mock method callbacks fired.
  delegating_frame_scheduler.SetFrameScheduler(frame_scheduler1);

  EXPECT_EQ(1u, num_register_present_callbacks);
  EXPECT_NE(last_present_id, 0u);
  EXPECT_EQ(1u, num_schedule_update_callbacks);
  EXPECT_EQ(1u, num_set_render_continuosly_callbacks);
  EXPECT_EQ(1u, num_get_future_presentation_infos_callbacks);
  EXPECT_EQ(1u, num_set_on_frame_presented_callback_for_session_callbacks);

  // Set a different frame scheduler, no effect.
  auto frame_scheduler2 = std::make_shared<MockFrameScheduler>();
  delegating_frame_scheduler.SetFrameScheduler(frame_scheduler2);
  EXPECT_EQ(1u, num_register_present_callbacks);
  EXPECT_EQ(1u, num_schedule_update_callbacks);
  EXPECT_EQ(1u, num_set_render_continuosly_callbacks);
  EXPECT_EQ(1u, num_get_future_presentation_infos_callbacks);
  EXPECT_EQ(1u, num_set_on_frame_presented_callback_for_session_callbacks);

  // Invoke method after initialized, invoked immediately.
  const auto present_id2 = delegating_frame_scheduler.RegisterPresent(kSessionId, {}, {});
  delegating_frame_scheduler.ScheduleUpdateForSession(
      /*presentation_time=*/zx::time(0), {.session_id = kSessionId, .present_id = present_id2});
  EXPECT_EQ(2u, num_schedule_update_callbacks);
}

}  // namespace test
}  // namespace scheduling
