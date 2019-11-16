// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/tests/mocks/frame_scheduler_mocks.h"

#include <lib/gtest/test_loop_fixture.h>

namespace scheduling {
namespace test {

SessionUpdater::UpdateResults MockSessionUpdater::UpdateSessions(
    std::unordered_set<SessionId> sessions_to_update, zx::time presentation_time,
    zx::time wakeup_time, uint64_t trace_id) {
  ++update_sessions_call_count_;

  UpdateResults results;

  for (auto session_id : sessions_to_update) {
    if (dead_sessions_.find(session_id) != dead_sessions_.end()) {
      continue;
    }

    // A client can only be using either Present or Present2 at one time.
    FXL_CHECK(updates_.find(session_id) == updates_.end() ||
              present2_updates_.find(session_id) == present2_updates_.end());

    // Handle Present2 updates separately from Present1 updates.
    if (present2_updates_.find(session_id) != present2_updates_.end()) {
      auto& queue = present2_updates_[session_id];
      while (!queue.empty()) {
        auto& update = queue.front();

        if (update.target_presentation_time > presentation_time) {
          // Wait until the target presentation_time is reached before "updating".
          break;
        } else if (update.fences_done_time > presentation_time) {
          // Fences aren't ready, so reschedule this session.
          results.sessions_to_reschedule.insert(session_id);
          break;
        } else {
          // "Apply update" and push the Present2Info.
          results.present2_infos.push_back(std::move(update.present2_info));

          // Since an update was applied, the scene must be re-rendered (unless this is suppressed
          // for testing purposes).
          results.needs_render = !rendering_suppressed_;
        }
        queue.pop();
      }

      // Skip the remaining Present1 logic.
      continue;
    }

    if (updates_.find(session_id) == updates_.end() || updates_[session_id].empty()) {
      EXPECT_TRUE(be_relaxed_about_unexpected_session_updates_)
          << "wasn't expecting update for session: " << session_id;
      continue;
    }

    auto& queue = updates_[session_id];
    while (!queue.empty()) {
      auto& update = queue.front();

      if (update.target > presentation_time) {
        // Wait until the target presentation_time is reached before "updating".
        break;
      } else if (update.fences_done > presentation_time) {
        // Fences aren't ready, so reschedule this session.
        results.sessions_to_reschedule.insert(session_id);
        ++update.status->reschedule_count;
        break;
      } else {
        // "Apply update" and push the notification callback.
        FXL_CHECK(!update.status->callback_passed);
        update.status->callback_passed = true;

        // Wrap the test-provided callback in a closure that updates |num_callback_invocations_|.
        FXL_CHECK(update.callback);
        results.present1_callbacks.push_back(std::move(update.callback));
        FXL_CHECK(!update.callback);

        // Since an update was applied, the scene must be re-rendered (unless this is suppressed for
        // testing purposes).
        results.needs_render = !rendering_suppressed_;
      }

      queue.pop();
    }
  }

  return results;
}

std::shared_ptr<const MockSessionUpdater::CallbackStatus> MockSessionUpdater::AddCallback(
    SessionId session_id, zx::time presentation_time, zx::time acquire_fence_time) {
  auto status = std::make_shared<CallbackStatus>();
  status->session_id = session_id;

  updates_[session_id].push(
      {presentation_time, acquire_fence_time, status,
       [status, weak_this{weak_factory_.GetWeakPtr()}](PresentationInfo presentation_info) {
         if (weak_this) {
           ++weak_this->signal_successful_present_callback_count_;
         } else {
           status->updater_disappeared = true;
         }
         EXPECT_FALSE(status->callback_invoked);
         status->callback_invoked = true;
         status->presentation_info = presentation_info;
       }});

  return status;
}

void MockSessionUpdater::AddPresent2Info(scenic_impl::Present2Info info, zx::time presentation_time,
                                         zx::time acquire_fence_time) {
  present2_updates_[info.session_id()].push(
      {presentation_time, acquire_fence_time, std::move(info)});
}

RenderFrameResult MockFrameRenderer::RenderFrame(fxl::WeakPtr<FrameTimings> frame_timings,
                                                 zx::time presentation_time) {
  FXL_CHECK(frame_timings);

  const uint64_t frame_number = frame_timings->frame_number();
  FXL_CHECK(frames_.find(frame_number) == frames_.end());
  // Check that no frame numbers were skipped.
  FXL_CHECK(frame_number == last_frame_number_ + 1);
  last_frame_number_ = frame_number;

  ++render_frame_call_count_;
  frame_timings->RegisterSwapchains(1);
  frames_[frame_number] = {.frame_timings = std::move(frame_timings), .swapchain_index = 0};

  return render_frame_return_value_;
}

void MockFrameRenderer::EndFrame(uint64_t frame_number, zx::time time_done) {
  SignalFrameRendered(frame_number, time_done);
  SignalFrameCpuRendered(frame_number, time_done);
  SignalFramePresented(frame_number, time_done);
}

void MockFrameRenderer::SignalFrameRendered(uint64_t frame_number, zx::time time_done) {
  auto find_it = frames_.find(frame_number);
  FXL_CHECK(find_it != frames_.end());

  auto& frame = find_it->second;
  // Frame can't be rendered twice.
  FXL_CHECK(!frame.frame_rendered);
  frame.frame_rendered = true;
  frame.frame_timings->OnFrameRendered(frame.swapchain_index, time_done);

  CleanUpFrame(frame_number);
}

void MockFrameRenderer::SignalFrameCpuRendered(uint64_t frame_number, zx::time time_done) {
  auto find_it = frames_.find(frame_number);
  FXL_CHECK(find_it != frames_.end());

  auto& frame = find_it->second;
  frame.frame_cpu_rendered = true;
  frame.frame_timings->OnFrameCpuRendered(time_done);

  CleanUpFrame(frame_number);
}

void MockFrameRenderer::SignalFramePresented(uint64_t frame_number, zx::time time_done) {
  auto find_it = frames_.find(frame_number);
  FXL_CHECK(find_it != frames_.end());

  auto& frame = find_it->second;
  // Frame can't be dropped/presented twice.
  FXL_CHECK(!frame.frame_presented);
  frame.frame_presented = true;
  frame.frame_timings->OnFramePresented(frame.swapchain_index, time_done);

  CleanUpFrame(frame_number);
}

void MockFrameRenderer::SignalFrameDropped(uint64_t frame_number) {
  auto find_it = frames_.find(frame_number);
  FXL_CHECK(find_it != frames_.end());

  auto& frame = find_it->second;
  // Frame can't be dropped/presented twice.
  FXL_CHECK(!frame.frame_presented);
  frame.frame_presented = true;
  frame.frame_timings->OnFrameDropped(frame.swapchain_index);

  CleanUpFrame(frame_number);
}

void MockFrameRenderer::CleanUpFrame(uint64_t frame_number) {
  auto find_it = frames_.find(frame_number);
  FXL_CHECK(find_it != frames_.end());

  auto& frame = find_it->second;
  if (!frame.frame_rendered || !frame.frame_presented || !frame.frame_cpu_rendered) {
    return;
  }
  frames_.erase(frame_number);
}

}  // namespace test
}  // namespace scheduling
