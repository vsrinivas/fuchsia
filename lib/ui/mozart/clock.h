// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_MOZART_CLOCK_H_
#define GARNET_LIB_UI_MOZART_CLOCK_H_

#include <zircon/types.h>

namespace mz {

// Clock supports querying the current time.  The default implementation returns
// zx_clock_get(ZX_CLOCK_MONOTONIC).  Subclasses may override this behavior, for
// testing and other purposes, so long as the returned times are monotonically
// non-decreasing.
class Clock {
 public:
  Clock();
  virtual ~Clock();

  virtual zx_time_t GetNanos();
};

}  // namespace mz

#endif  // GARNET_LIB_UI_MOZART_CLOCK_H_
