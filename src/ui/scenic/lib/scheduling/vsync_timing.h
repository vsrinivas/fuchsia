// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_VSYNC_TIMING_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_VSYNC_TIMING_H_

#include <lib/zx/time.h>

#include "src/lib/fxl/memory/weak_ptr.h"

namespace scheduling {

class VsyncTiming {
 public:
  VsyncTiming();

  // Obtain the time of the last Vsync, in nanoseconds.
  zx::time last_vsync_time() const { return last_vsync_time_; }

  // Obtain the interval between Vsyncs, in nanoseconds.
  zx::duration vsync_interval() const { return vsync_interval_; }

  void set_last_vsync_time(zx::time last_vsync_time) { last_vsync_time_ = last_vsync_time; }
  void set_vsync_interval(zx::duration vsync_interval) { vsync_interval_ = vsync_interval; }

 private:
  // Vsync interval of a 60 Hz screen.
  // Used as a default value before real timings arrive.
  static constexpr zx::duration kNsecsFor60fps = zx::nsec(16'666'667);  // 16.666667ms

  zx::time last_vsync_time_;
  zx::duration vsync_interval_;
};

}  // namespace scheduling

#endif  // SRC_UI_SCENIC_LIB_SCHEDULING_VSYNC_TIMING_H_
