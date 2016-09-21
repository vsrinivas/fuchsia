// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_GFX_COMPOSITOR_BACKEND_OUTPUT_H_
#define SERVICES_GFX_COMPOSITOR_BACKEND_OUTPUT_H_

#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"

namespace compositor {

class RenderFrame;
class Scheduler;

// Renders snapshotted frames of the scene graph to a display output.
//
// The output object is created on the compositor's main thread and frames
// are submitted to it from there.  Behind the scenes, the implementation of
// Output may use some number of worker threads.  How this is accomplished
// is left up to the implementation of the Output to decide.
class Output {
 public:
  Output() = default;
  virtual ~Output() = default;

  // Gets the output's frame scheduler.
  virtual Scheduler* GetScheduler() = 0;

  // Submits a frame to be rendered to the display, or null if none.
  virtual void SubmitFrame(const ftl::RefPtr<RenderFrame>& frame) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Output);
};

}  // namespace compositor

#endif  // SERVICES_GFX_COMPOSITOR_BACKEND_OUTPUT_H_
