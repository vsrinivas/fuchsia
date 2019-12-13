// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_VSYNC_TIMING_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_VSYNC_TIMING_H_

#include <lib/zx/time.h>

namespace scheduling {

class VsyncTiming {
 public:
  virtual ~VsyncTiming(){};

  // Obtain the time of the last Vsync, in nanoseconds.
  virtual zx::time GetLastVsyncTime() const = 0;

  // Obtain the interval between Vsyncs, in nanoseconds.
  virtual zx::duration GetVsyncInterval() const = 0;
};

}  // namespace scheduling

#endif  // SRC_UI_SCENIC_LIB_SCHEDULING_VSYNC_TIMING_H_
