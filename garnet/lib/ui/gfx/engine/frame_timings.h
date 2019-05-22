// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_FRAME_TIMINGS_H_
#define GARNET_LIB_UI_GFX_ENGINE_FRAME_TIMINGS_H_

#include <lib/zx/time.h>

#include <vector>

#include "src/ui/lib/escher/base/reffable.h"

namespace scenic_impl {
namespace gfx {

class FrameTimings;
class FrameScheduler;
class Swapchain;
using FrameTimingsPtr = fxl::RefPtr<FrameTimings>;

// Each frame, an instance of FrameTimings is used by the FrameScheduler to
// collect timing information about all swapchains that were rendered to during
// the frame.  Once all swapchains have finished rendering/presenting, the
// FrameScheduler is notified via OnFramePresented().
//
// TODO(SCN-1324) This class currently handles one frame scheduler outputting to
// n swapchains, and computes the slowest time values for any swapchain. Figure
// out how to decouple multiple swapchains.
//
// TODO(SCN-1443) Refactor FrameTimings, FrameScheduler, and Swapchain
// interactions. There are implicit assumptions about when a swapchain is added
// to FrameTimings, and the availability of swapchain buffers that should be
// formalized and properly handled.
class FrameTimings : public escher::Reffable {
 public:
  // Time value used to signal the time measurement has not yet been recorded.
  static constexpr zx_time_t kTimeUninitialized = ZX_TIME_INFINITE_PAST;
  // Time value used to signal the time measurement was dropped.
  static constexpr zx_time_t kTimeDropped = ZX_TIME_INFINITE;

  // Timestamps of all points managed by FrameTimings.
  struct Timestamps {
    zx_time_t latch_point_time;
    zx_time_t update_done_time;
    zx_time_t render_start_time;
    zx_time_t render_done_time;
    zx_time_t target_presentation_time;
    zx_time_t actual_presentation_time;
  };

  // Constructor
  //
  // |frame_scheduler| The FrameScheduler that should be notified of frame
  //     render and frame drop times.
  // |frame_number| The frame number used to identify the drawn frame.
  // |target_presentation_time| The presentation time this frame is attempting
  //     to be displayed by.
  // |latch_time| The time the frame "latches". Typically this is the update
  //     start time.
  // |rendering_started_time| The time this frame started rendering.
  FrameTimings(FrameScheduler* frame_scheduler, uint64_t frame_number,
               zx_time_t target_presentation_time, zx_time_t latch_time,
               zx_time_t rendering_started_time);

  // Registers a swapchain that is used as a render target this frame.  Return
  // an index that can be used to indicate when rendering for that swapchain is
  // finished, and when the frame is actually presented on that swapchain. Each
  // swapchain must only call |RegisterSwapchain()| once.
  // TODO(SCN-1443) Refactor how swapchains and FrameTimings interact.
  size_t RegisterSwapchain();

  // Called by the updater to record the update done time. This must be later
  // than or equal to the previously supplied |latch_time|.
  // Note: there is no associated swapchain because this time is associated for
  // the frame update CPU work only.
  void OnFrameUpdated(zx_time_t time);
  // Called by the swapchain to record the render done time. This must be later
  // than or equal to the previously supplied |rendering_started_time|.
  void OnFrameRendered(size_t swapchain_index, zx_time_t time);
  // Called by the swapchain to record the frame's presentation time. A
  // presented frame is assumed to have been presented on the display, and was
  // not dropped. This must be later  than or equal to the previously supplied
  // |target_presentation_time|.
  void OnFramePresented(size_t swapchain_index, zx_time_t time);
  // Called by the swapchain to record that this frame has been dropped. A
  // dropped frame is assumed to have been presented on the display, and was
  // not dropped. This must be later  than or equal to the previously supplied
  // |target_presentation_time|
  void OnFrameDropped(size_t swapchain_index);

  // Provide direct access to FrameTimings constant values.
  uint64_t frame_number() const { return frame_number_; }
  zx_time_t target_presentation_time() const {
    return target_presentation_time_;
  }
  zx_time_t latch_point_time() const { return latch_point_time_; }
  zx_time_t rendering_started_time() const { return rendering_started_time_; }

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

 private:
  // Helper function when FrameTimings is finalized to validate the render time
  // is less than or equal to the frame presented time.
  void ValidateRenderTime();
  // Called once all swapchains have reported back with their render-finished
  // and presentation times.
  void Finalize();

  bool received_all_frame_rendered_callbacks() {
    return frame_rendered_count_ == swapchain_records_.size();
  }

  bool received_all_callbacks() {
    return frame_rendered_count_ == swapchain_records_.size() &&
           frame_presented_count_ == swapchain_records_.size();
  }

  struct SwapchainRecord {
    zx_time_t frame_rendered_time = kTimeUninitialized;
    zx_time_t frame_presented_time = kTimeUninitialized;
  };
  std::vector<SwapchainRecord> swapchain_records_;
  size_t frame_rendered_count_ = 0;
  size_t frame_presented_count_ = 0;

  FrameScheduler* const frame_scheduler_;
  const uint64_t frame_number_;

  // Frame start times.
  const zx_time_t target_presentation_time_;
  const zx_time_t latch_point_time_;
  const zx_time_t rendering_started_time_;
  // Frame end times.
  zx_time_t actual_presentation_time_ = kTimeUninitialized;
  zx_time_t updates_finished_time_ = kTimeUninitialized;
  zx_time_t rendering_finished_time_ = kTimeUninitialized;

  bool frame_was_dropped_ = false;
  bool finalized_ = false;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_FRAME_TIMINGS_H_
