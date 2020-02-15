// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/tests/mocks/frame_scheduler_mocks.h"

#include <lib/gtest/test_loop_fixture.h>

namespace scheduling {
namespace test {

PresentId MockFrameScheduler::RegisterPresent(
    SessionId session_id, std::variant<OnPresentedCallback, Present2Info> present_information,
    std::vector<zx::event> release_fences, PresentId present_id) {
  if (register_present_callback_) {
    register_present_callback_(session_id, std::move(present_information),
                               std::move(release_fences), present_id);
  }

  return present_id != 0 ? present_id : next_present_id_++;
}

void MockFrameScheduler::SetRenderContinuously(bool render_continuously) {
  if (set_render_continuously_callback_) {
    set_render_continuously_callback_(render_continuously);
  }
}

void MockFrameScheduler::ScheduleUpdateForSession(zx::time presentation_time,
                                                  SchedulingIdPair id_pair) {
  if (schedule_update_for_session_callback_) {
    schedule_update_for_session_callback_(presentation_time, id_pair);
  }
}

void MockFrameScheduler::GetFuturePresentationInfos(
    zx::duration requested_prediction_span,
    FrameScheduler::GetFuturePresentationInfosCallback presentation_infos_callback) {
  if (get_future_presentation_infos_callback_) {
    presentation_infos_callback(get_future_presentation_infos_callback_(requested_prediction_span));
  }
  presentation_infos_callback({});
}

void MockFrameScheduler::SetOnFramePresentedCallbackForSession(
    SessionId session, OnFramePresentedCallback frame_presented_callback) {
  if (set_on_frame_presented_callback_for_session_callback_) {
    set_on_frame_presented_callback_for_session_callback_(session,
                                                          std::move(frame_presented_callback));
  }
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
  FXL_CHECK(find_it != frames_.end()) << "Couldn't find frame_number " << frame_number;

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
