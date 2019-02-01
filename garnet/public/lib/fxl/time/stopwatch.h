// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A simple "stopwatch" for measuring elapsed time.

#ifndef LIB_FXL_TIME_STOPWATCH_H_
#define LIB_FXL_TIME_STOPWATCH_H_

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fxl/time/time_point.h"

namespace fxl {

// A simple "stopwatch" for measuring time elapsed from a given starting point.
class FXL_EXPORT Stopwatch final {
 public:
  Stopwatch() {}
  ~Stopwatch() {}

  void Start();

  // Returns the amount of time elapsed since the last call to |Start()|.
  TimeDelta Elapsed();

 private:
  TimePoint start_time_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Stopwatch);
};

}  // namespace fxl

#endif  // LIB_FXL_TIME_STOPWATCH_H_
