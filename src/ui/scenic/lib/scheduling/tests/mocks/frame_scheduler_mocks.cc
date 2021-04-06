// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/tests/mocks/frame_scheduler_mocks.h"

#include <lib/async/cpp/time.h>
#include <lib/async/default.h>
#include <lib/gtest/test_loop_fixture.h>

namespace {
void SignalAll(const std::vector<zx::event>& events) {
  for (auto& e : events) {
    e.signal(0u, ZX_EVENT_SIGNALED);
  }
}

zx::time Now() { return async::Now(async_get_default_dispatcher()); }
}  // namespace

namespace scheduling {
namespace test {

PresentId MockFrameScheduler::RegisterPresent(SessionId session_id,
                                              std::vector<zx::event> release_fences,
                                              PresentId present_id) {
  if (register_present_callback_) {
    register_present_callback_(session_id, std::move(release_fences), present_id);
  }

  return present_id != 0 ? present_id : next_present_id_++;
}

void MockFrameScheduler::SetRenderContinuously(bool render_continuously) {
  if (set_render_continuously_callback_) {
    set_render_continuously_callback_(render_continuously);
  }
}

void MockFrameScheduler::ScheduleUpdateForSession(zx::time presentation_time,
                                                  SchedulingIdPair id_pair, bool squashable) {
  if (schedule_update_for_session_callback_) {
    schedule_update_for_session_callback_(presentation_time, id_pair, squashable);
  }
}

void MockFrameScheduler::GetFuturePresentationInfos(
    zx::duration requested_prediction_span,
    FrameScheduler::GetFuturePresentationInfosCallback presentation_infos_callback) {
  if (get_future_presentation_infos_callback_) {
    presentation_infos_callback(get_future_presentation_infos_callback_(requested_prediction_span));
  } else {
    presentation_infos_callback({});
  }
}

void MockFrameScheduler::RemoveSession(SessionId session_id) {
  if (remove_session_callback_) {
    remove_session_callback_(session_id);
  }
}

void MockFrameRenderer::RenderScheduledFrame(uint64_t frame_number, zx::time presentation_time,
                                             FramePresentedCallback callback) {
  FX_CHECK(frame_number);
  FX_CHECK(callback);

  // Check that no frame numbers were skipped.
  FX_CHECK(frame_number == last_frame_number_ + 1);
  last_frame_number_ = frame_number;

  frames_.emplace_back(
      PendingFrame({.start = Now(), .callback = std::move(callback), .fences = {}}));
}

void MockFrameRenderer::SignalFencesWhenPreviousRendersAreDone(std::vector<zx::event> fences) {
  if (frames_.empty()) {
    SignalAll(std::move(fences));
  } else {
    FX_CHECK(!frames_.back().fences.size());
    frames_.back().fences = std::move(fences);
  }
}

void MockFrameRenderer::EndFrame(const Timestamps& timestamps) {
  FX_CHECK(!frames_.empty());
  auto& next_frame = frames_.front();

  next_frame.callback(timestamps);
  SignalAll(pending_fences_);
  pending_fences_.clear();
  SignalAll(next_frame.fences);
  frames_.pop_front();
}

void MockFrameRenderer::EndFrame() {
  FX_CHECK(!frames_.empty());
  auto& next_frame = frames_.front();

  FrameRenderer::Timestamps timestamps;
  timestamps.render_done_time = Now();
  timestamps.actual_presentation_time = Now();

  EndFrame(timestamps);
}

void MockFrameRenderer::DropFrame() {
  FX_CHECK(!frames_.empty());
  auto& next_frame = frames_.front();

  FrameRenderer::Timestamps timestamps;
  timestamps.render_done_time = Now();
  timestamps.actual_presentation_time = kTimeDropped;

  next_frame.callback(timestamps);
  std::move(std::begin(next_frame.fences), std::end(next_frame.fences),
            std::back_inserter(pending_fences_));
  frames_.pop_front();
}

}  // namespace test
}  // namespace scheduling
