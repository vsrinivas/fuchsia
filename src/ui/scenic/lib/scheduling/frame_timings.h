// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_FRAME_TIMINGS_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_FRAME_TIMINGS_H_

#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <vector>

#include "src/lib/fxl/memory/weak_ptr.h"

namespace scheduling {

// Each frame, an instance of FrameTimings is used by the FrameScheduler to
// collect timing information about all swapchains that were rendered to during
// the frame.  Once all swapchains have finished rendering/presenting, the
// FrameScheduler is notified via OnFramePresented().
//
// TODO(fxbug.dev/24518) This class currently handles one frame scheduler outputting to
// n swapchains, and computes the slowest time values for any swapchain. Figure
// out how to decouple multiple swapchains.
//
// TODO(fxbug.dev/24632) Refactor FrameTimings, FrameScheduler, and Swapchain
// interactions. There are implicit assumptions about when a swapchain is added
// to FrameTimings, and the availability of swapchain buffers that should be
// formalized and properly handled.
class FrameTimings {
 public:
  using OnTimingsRenderedCallback = fit::function<void(const FrameTimings&)>;
  using OnTimingsPresentedCallback = fit::function<void(const FrameTimings&)>;

  // Time value used to signal the time measurement has not yet been recorded.
  static constexpr zx::time kTimeUninitialized = zx::time(ZX_TIME_INFINITE_PAST);
  // Time value used to signal the time measurement was dropped.
  static constexpr zx::time kTimeDropped = zx::time(ZX_TIME_INFINITE);

  // Timestamps of all points managed by FrameTimings.
  //
  // Note that there potentially can be multiple times a frame was updated before
  // it was finally rendered, and |update_done_time| tracks the last of those
  // updates. See SCN-1482 for more details.
  struct Timestamps {
    zx::time latch_point_time;
    zx::time update_done_time;
    zx::time render_start_time;
    zx::time render_done_time;
    zx::time target_presentation_time;
    zx::time actual_presentation_time;
  };

  // Constructor
  //
  // |frame_number| The frame number used to identify the drawn frame.
  // |target_presentation_time| The presentation time this frame is attempting
  //     to be displayed by.
  // |latch_time| The time the frame "latches". Typically this is the update
  //     start time.
  // |rendering_started_time| The time this frame started rendering.
  // |timings_rendered_callback| Callback invoked when the frame has finished rendering.
  // |timings_presented_callback| Callback invoked when the frame has been presented or
  //     dropped.
  FrameTimings(uint64_t frame_number, zx::time target_presentation_time, zx::time latch_time,
               zx::time rendering_started_time, OnTimingsRenderedCallback timings_rendered_callback,
               OnTimingsPresentedCallback timings_presented_callback);

  fxl::WeakPtr<FrameTimings> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // Reserves |count| swapchain records, numbered 0 to count-1.
  // REQUIRES: OnFrame* has not been called.
  void RegisterSwapchains(size_t count);

  // Called by the frame scheduler to record the update done time. This must be later than or equal
  // to the previously supplied |latch_time|. Note: there is no associated swapchain because this
  // time is associated for the frame update CPU work only.
  void OnFrameUpdated(zx::time time);

  // Called by the swapchain to record the render done time. This must be later
  // than or equal to the previously supplied |rendering_started_time|.
  void OnFrameRendered(size_t swapchain_index, zx::time time);

  // Called by the swapchain to record the frame's presentation time. A
  // presented frame is assumed to have been presented on the display, and was
  // not dropped. This must be later  than or equal to the previously supplied
  // |target_presentation_time|.
  void OnFramePresented(size_t swapchain_index, zx::time time);

  // Called by the swapchain to record that this frame has been dropped. A
  // dropped frame is assumed to have been rendered but not presented on the
  // display.
  void OnFrameDropped(size_t swapchain_index);

  // Called by the frame scheduler to record that this frame was never rendered,
  // e.g. if there was no renderable content. This assumes that the swapchain count
  // is 0.
  void OnFrameSkipped();

  // It is possible for the GPU portion of the rendering of a frame to be
  // completed before the CPU portion. Therefore to ensure our frame scheduler
  // makes correct decisions, we need to account for such a possibility.
  void OnFrameCpuRendered(zx::time time);

  // Provide direct access to FrameTimings constant values.
  uint64_t frame_number() const { return frame_number_; }
  zx::time target_presentation_time() const { return target_presentation_time_; }
  zx::time latch_point_time() const { return latch_point_time_; }
  zx::time rendering_started_time() const { return rendering_started_time_; }

  // Returns true when all the swapchains this frame have reported
  // OnFrameRendered and either OnFramePresented or OnFrameDropped.
  //
  // Although the actual frame presentation depends on the actual frame
  // rendering, there is currently no guaranteed ordering between when the
  // two events are received by the engine (due to the redispatch
  // in EventTimestamper).
  bool finalized() const { return finalized_; }

  // Returns all the timestamps that this class is tracking. Values are subject
  // to change until this class is |finalized()|.
  Timestamps GetTimestamps() const;

  // Returns true if the frame was dropped by at least one swapchain that it was
  // submitted to. Value is subject to change until this class is |finalized()|.
  bool FrameWasDropped() const { return frame_was_dropped_; }

  // Returns true if this frame was skipped by the renderer, and never submitted
  // for rendering or presentation.
  bool FrameWasSkipped() const { return frame_was_skipped_; }

 private:
  // Helper function when FrameTimings is finalized to validate the render time
  // is less than or equal to the frame presented time.
  void ValidateRenderTime();
  // Called once all swapchains have reported back with their render-finished
  // and presentation times.
  void Finalize();

  bool received_all_frame_rendered_callbacks() const {
    return frame_rendered_count_ == swapchain_records_.size();
  }

  bool received_all_frame_presented_callbacks() const {
    return frame_presented_count_ == swapchain_records_.size();
  }

  bool received_all_callbacks() const {
    return received_all_frame_rendered_callbacks() && received_all_frame_presented_callbacks();
  }

  struct SwapchainRecord {
    zx::time frame_rendered_time = kTimeUninitialized;
    zx::time frame_presented_time = kTimeUninitialized;
  };
  std::vector<SwapchainRecord> swapchain_records_;
  size_t frame_rendered_count_ = 0;
  size_t frame_presented_count_ = 0;

  const uint64_t frame_number_;

  // Frame start times.
  const zx::time target_presentation_time_;
  const zx::time latch_point_time_;
  const zx::time rendering_started_time_;
  // Frame end times.
  zx::time actual_presentation_time_ = kTimeUninitialized;
  zx::time updates_finished_time_ = kTimeUninitialized;
  zx::time rendering_finished_time_ = kTimeUninitialized;
  zx::time rendering_cpu_finished_time_ = kTimeUninitialized;

  bool frame_was_dropped_ = false;
  bool frame_was_skipped_ = false;
  bool finalized_ = false;

  OnTimingsRenderedCallback timings_rendered_callback_;
  OnTimingsPresentedCallback timings_presented_callback_;

  fxl::WeakPtrFactory<FrameTimings> weak_factory_;  // must be last
};

}  // namespace scheduling

#endif  // SRC_UI_SCENIC_LIB_SCHEDULING_FRAME_TIMINGS_H_
