// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_TESTS_FRAME_SCHEDULER_MOCKS_H_
#define GARNET_LIB_UI_GFX_TESTS_FRAME_SCHEDULER_MOCKS_H_

#include "garnet/lib/ui/gfx/displays/display.h"
#include "garnet/lib/ui/gfx/engine/frame_scheduler.h"
#include "garnet/lib/ui/gfx/engine/frame_timings.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class MockFrameScheduler : public FrameScheduler {
 private:
  uint32_t frame_presented_call_count_ = 0;
  uint32_t frame_rendered_call_count_ = 0;

 public:
  MockFrameScheduler() = default;
  void SetDelegate(FrameSchedulerDelegate delegate) override{};
  void SetRenderContinuously(bool render_continuously) override{};
  void ScheduleUpdateForSession(zx_time_t presentation_time,
                                scenic_impl::SessionId session) override{};

  void OnFramePresented(const FrameTimings& timings) override {
    ++frame_presented_call_count_;
  };
  void OnFrameRendered(const FrameTimings& timings) override {
    ++frame_rendered_call_count_;
  };

  uint32_t frame_presented_call_count() { return frame_presented_call_count_; }
  uint32_t frame_rendered_call_count() { return frame_rendered_call_count_; }
};

class FakeDisplay : public Display {
 public:
  FakeDisplay()
      : Display(/* id */ 0,
                /* width_in_px */ 0,
                /* height_in_px */ 0) {}

  // Manually sets the values returned by
  // GetVsyncInterval() and GetLastVsyncTime().
  void SetVsyncInterval(zx_duration_t new_interval) {
    vsync_interval_ = new_interval;
  }
  void SetLastVsyncTime(zx_duration_t new_last_vsync) {
    last_vsync_time_ = new_last_vsync;
  }
};

class MockSessionUpdater : public SessionUpdater {
 public:
  MockSessionUpdater() : weak_factory_(this) {}

  bool UpdateSessions(std::vector<SessionUpdate> sessions_to_update,
                      uint64_t frame_number, zx_time_t presentation_time,
                      zx_duration_t presentation_interval) override {
    ++update_sessions_call_count_;
    last_requested_updates_ = std::move(sessions_to_update);
    return update_sessions_return_value_;
  };

  // Manually set value returned from UpdateSessions.
  void SetUpdateSessionsReturnValue(bool new_value) {
    update_sessions_return_value_ = new_value;
  }
  uint32_t update_sessions_call_count() { return update_sessions_call_count_; }
  const std::vector<SessionUpdate>& last_requested_updates() {
    return last_requested_updates_;
  }

  fxl::WeakPtr<MockSessionUpdater> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  bool update_sessions_return_value_ = true;
  uint32_t update_sessions_call_count_ = 0;
  std::vector<SessionUpdate> last_requested_updates_;

  fxl::WeakPtrFactory<MockSessionUpdater> weak_factory_;  // must be last
};

class MockFrameRenderer : public FrameRenderer {
 public:
  MockFrameRenderer() : weak_factory_(this) {}

  // |FrameRenderer|
  bool RenderFrame(const FrameTimingsPtr& frame_timings,
                   zx_time_t presentation_time,
                   zx_duration_t presentation_interval);

  // Need to call this in order to trigger the OnFramePresented() callback in
  // FrameScheduler, but is not valid to do until after RenderFrame has returned
  // to FrameScheduler. Hence this separate method.
  void EndFrame(size_t frame_index);

  // Signal frame |frame_index| that it has been rendered.
  void SignalFrameRendered(size_t frame_index);

  // Signal frame |frame_index| that it has been presented.
  void SignalFramePresented(size_t frame_index);

  // Manually set value returned from RenderFrame.
  void SetRenderFrameReturnValue(bool new_value) {
    render_frame_return_value_ = new_value;
  }
  uint32_t render_frame_call_count() { return render_frame_call_count_; }
  size_t pending_frames() { return frames_.size(); }

  fxl::WeakPtr<MockFrameRenderer> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  bool render_frame_return_value_ = true;
  uint32_t render_frame_call_count_ = 0;

  struct Timings {
    FrameTimings* frame_timings;
    uint32_t frame_rendered = false;
    uint32_t frame_presented = false;
    Timings(FrameTimings* ft) { frame_timings = ft; }
  };
  std::vector<Timings> frames_;

  uint64_t last_frame_number_ = -1;

  fxl::WeakPtrFactory<MockFrameRenderer> weak_factory_;  // must be last
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_TESTS_FRAME_SCHEDULER_MOCKS_H_
