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
                      uint64_t frame_number, uint64_t presentation_time,
                      uint64_t presentation_interval) override {
    ++update_sessions_call_count_;
    return update_sessions_return_value_;
  };

  // Manually set value returned from UpdateSessions.
  void SetUpdateSessionsReturnValue(bool new_value) {
    update_sessions_return_value_ = new_value;
  }
  uint32_t update_sessions_call_count() { return update_sessions_call_count_; }

  fxl::WeakPtr<MockSessionUpdater> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  bool update_sessions_return_value_ = true;
  uint32_t update_sessions_call_count_ = 0;

  fxl::WeakPtrFactory<MockSessionUpdater> weak_factory_;  // must be last
};

class MockFrameRenderer : public FrameRenderer {
 public:
  MockFrameRenderer() : weak_factory_(this) {}

  bool RenderFrame(const FrameTimingsPtr& frame_timings,
                   uint64_t presentation_time,
                   uint64_t presentation_interval) override {
    ++render_frame_call_count_;
    last_frame_timings_ = frame_timings.get();
    return render_frame_return_value_;
  };

  // Need to call this in order to trigger the OnFramePresented() callback in
  // FrameScheduler, but is not valid to do until after RenderFrame has returned
  // to FrameScheduler. Hence this separate method.
  void EndFrame() {
    if(last_frame_timings_) {
      last_frame_timings_->AddSwapchain(nullptr);
      last_frame_timings_->OnFrameRendered(/*swapchain index*/ 0, /*time*/ 1);
      last_frame_timings_->OnFramePresented(/*swapchain index*/ 0, /*time*/ 1);
      last_frame_timings_ = nullptr;
    }
  }

  // Manually set value returned from RenderFrame.
  void SetRenderFrameReturnValue(bool new_value) {
    render_frame_return_value_ = new_value;
  }
  uint32_t render_frame_call_count() { return render_frame_call_count_; }

  fxl::WeakPtr<MockFrameRenderer> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  bool render_frame_return_value_ = true;
  uint32_t render_frame_call_count_ = 0;
  FrameTimings* last_frame_timings_ = nullptr;

  fxl::WeakPtrFactory<MockFrameRenderer> weak_factory_;  // must be last
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_TESTS_FRAME_SCHEDULER_MOCKS_H_
