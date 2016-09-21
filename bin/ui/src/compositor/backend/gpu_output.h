// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_GFX_COMPOSITOR_BACKEND_GPU_OUTPUT_H_
#define SERVICES_GFX_COMPOSITOR_BACKEND_GPU_OUTPUT_H_

#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "apps/compositor/src/backend/gpu_rasterizer.h"
#include "apps/compositor/src/backend/output.h"
#include "apps/compositor/src/backend/vsync_scheduler.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/synchronization/waitable_event.h"
#include "lib/ftl/tasks/task_runner.h"
#include "mojo/services/gpu/interfaces/context_provider.mojom.h"

namespace compositor {

// Renderer backed by a ContextProvider.
class GpuOutput : public Output, private GpuRasterizer::Callbacks {
 public:
  GpuOutput(mojo::InterfaceHandle<mojo::ContextProvider> context_provider,
            const SchedulerCallbacks& scheduler_callbacks,
            const ftl::Closure& error_callback);
  ~GpuOutput() override;

  Scheduler* GetScheduler() override;
  void SubmitFrame(const ftl::RefPtr<RenderFrame>& frame) override;

 private:
  struct FrameData {
    enum class State {
      Pending,  // initial state waiting for draw to start
      Drawing,  // draw has started
      Finished  // draw has finished
    };

    FrameData(const ftl::RefPtr<RenderFrame>& frame, int64_t submit_time);
    ~FrameData();

    void ResetDrawState();

    const ftl::RefPtr<RenderFrame> frame;
    const int64_t submit_time;
    State state = State::Pending;
    int64_t draw_started_time = 0;  // time when drawing began
    int64_t draw_issued_time = 0;   // time when awaiting for finish began

   private:
    FTL_DISALLOW_COPY_AND_ASSIGN(FrameData);
  };

  // |GpuRasterizer::Callbacks|:
  void OnRasterizerReady(int64_t vsync_timebase,
                         int64_t vsync_interval) override;
  void OnRasterizerSuspended() override;
  void OnRasterizerFinishedDraw(bool presented) override;
  void OnRasterizerError() override;

  void ScheduleDrawLocked();
  void OnDraw();

  void InitializeRasterizer(
      mojo::InterfaceHandle<mojo::ContextProvider> context_provider);
  void DestroyRasterizer();
  void PostErrorCallback();

  ftl::RefPtr<ftl::TaskRunner> compositor_task_runner_;
  ftl::RefPtr<VsyncScheduler> vsync_scheduler_;
  ftl::Closure error_callback_;

  // Maximum number of frames to hold in the drawing pipeline.
  // Any more than this and we start dropping them.
  uint32_t pipeline_depth_;

  // The rasterizer itself runs on its own thread.
  std::thread rasterizer_thread_;
  ftl::RefPtr<ftl::TaskRunner> rasterizer_task_runner_;
  ftl::ManualResetWaitableEvent rasterizer_initialized_;
  std::unique_ptr<GpuRasterizer> rasterizer_;

  // Holds state shared between the compositor and rasterizer threads.
  struct {
    std::mutex mutex;  // guards all shared state

    // Queue of frames.
    //
    // The head of this queue consists of up to |pipeline_depth| frames
    // which are drawn and awaiting finish.  These frames are popped off
    // the queue when finished unless the queue would become empty (such
    // that we always retain the current frame as the tail).
    //
    // The tail of this queue is a single frame which is either drawn or
    // finished and represents the current (most recently submitted)
    // content.
    //
    // The queue is only ever empty until the first frame is submitted.
    // Subsequently, it always contains at least one frame.
    std::queue<std::unique_ptr<FrameData>> frames;

    // Set to true when the rasterizer is ready to draw.
    bool rasterizer_ready = false;

    // Set to true when a request to draw has been scheduled.
    bool draw_scheduled = false;
  } shared_state_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GpuOutput);
};

}  // namespace compositor

#endif  // SERVICES_GFX_COMPOSITOR_BACKEND_GPU_OUTPUT_H_
