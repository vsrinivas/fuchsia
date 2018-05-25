// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_FRAME_TIMINGS_H_
#define GARNET_LIB_UI_GFX_ENGINE_FRAME_TIMINGS_H_

#include <lib/zx/time.h>
#include <vector>

#include "lib/escher/base/reffable.h"

namespace scenic {
namespace gfx {

class FrameTimings;
class FrameScheduler;
class Swapchain;
using FrameTimingsPtr = fxl::RefPtr<FrameTimings>;

// Each frame, an instance of FrameTimings is used by the FrameScheduler to
// collect timing information about all swapchains that were rendered to during
// the frame.  Once all swapchains have finished rendering/presenting, the
// FrameScheduler is notified via OnFramePresented().
class FrameTimings : public escher::Reffable {
 public:
  FrameTimings();
  FrameTimings(FrameScheduler* frame_scheduler, uint64_t frame_number,
               zx_time_t target_presentation_time);

  // Add a swapchain that is used as a render target this frame.  Return an
  // index that can be used to indicate when rendering for that swapchain is
  // finished, and when the frame is actually presented on that swapchain.
  size_t AddSwapchain(Swapchain* swapchain);

  void OnFrameRendered(size_t swapchain_index, zx_time_t time);
  void OnFramePresented(size_t swapchain_index, zx_time_t time);

  uint64_t frame_number() const { return frame_number_; }
  zx_time_t target_presentation_time() const {
    return target_presentation_time_;
  }

  zx_time_t actual_presentation_time() const {
    return actual_presentation_time_;
  }

 private:
  // Called once all swapchains have reported back with their render-finished
  // and presentation times.
  void Finalize();

  struct Record {
    zx_time_t frame_rendered_time = 0;
    zx_time_t frame_presented_time = 0;
  };
  std::vector<Record> swapchain_records_;
  FrameScheduler* const frame_scheduler_;
  const uint64_t frame_number_;
  const zx_time_t target_presentation_time_;
  zx_time_t actual_presentation_time_ = 0;
  size_t frame_rendered_count_ = 0;
  size_t frame_presented_count_ = 0;
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_ENGINE_FRAME_TIMINGS_H_
