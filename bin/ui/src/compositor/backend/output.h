// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_BACKEND_OUTPUT_H_
#define APPS_MOZART_SRC_COMPOSITOR_BACKEND_OUTPUT_H_

#include <functional>

#include "apps/mozart/services/composition/interfaces/renderers.mojom.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/ftl/time/time_point.h"

namespace compositor {

class RenderFrame;

// Renders snapshotted frames of the scene graph to a display output.
//
// The output object is created on the compositor's main thread and frames
// are submitted to it from there.  Behind the scenes, the implementation of
// Output may use some number of worker threads.  How this is accomplished
// is left up to the implementation of the Output to decide.
class Output {
 public:
  // Provides timing information for an anticipated upcoming frame.
  // The accuracy of this information is only guaranteed between one call
  // to schedule a frame and the next.
  struct FrameTiming {
    // The time when the next submitted frame is scheduled to be presented
    // (pixels physically appear on screen) assuming deadlines are met.
    ftl::TimePoint presentation_time;

    // The inter-frame presentation interval (refresh rate).
    // To simplify calculations, we may assume that choosing to skip ahead
    // by one frame will delay presentation by this amount.
    ftl::TimeDelta presentation_interval;

    // The amount of time to allow for a submitted frame to be rendered,
    // scanned out to the display, and light up pixels.  To ensure that a
    // frame appears on-screen at |presentation_time|, it must be submitted
    // to the output by |presentation_time - presentation_latency|.
    ftl::TimeDelta presentation_latency;
  };

  // Callback for receiving display information.
  using DisplayCallback = std::function<void(mozart::DisplayInfoPtr)>;

  // Callback for receiving frame timing information.
  using FrameCallback = std::function<void(const FrameTiming&)>;

  Output() = default;
  virtual ~Output() = default;

  // Gets display information when available.
  virtual void GetDisplayInfo(DisplayCallback callback) = 0;

  // Schedules the next frame.
  // Invokes the callback when processing for the next frame is allowed to
  // begin, and provides information about that frame's timing information.
  // This function should not be called again until the callback has fired.
  //
  // Note: The |callback| may be called immediately.
  virtual void ScheduleFrame(FrameCallback callback) = 0;

  // Submits a frame to be rendered to the display, or null for a blank frame.
  // This method should be called at most once per scheduled frame.
  virtual void SubmitFrame(ftl::RefPtr<RenderFrame> frame) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Output);
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_BACKEND_OUTPUT_H_
