// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A simple "stopwatch" for measuring elapsed time.

#ifndef LIB_FTL_TIME_STOP_WATCH_H_
#define LIB_FTL_TIME_STOP_WATCH_H_

#include "lib/ftl/macros.h"
#include "lib/ftl/time/time.h"

namespace ftl {

// A simple "stopwatch" for measuring time elapsed from a given starting point.
class Stopwatch final {
 public:
  Stopwatch() {}
  ~Stopwatch() {}

  void Start();

  // Returns the amount of time elapsed since the last call to |Start()| (in
  // microseconds).
  Duration Elapsed();

 private:
  TimePoint start_time_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Stopwatch);
};

}  // namespace ftl

#endif  // LIB_FTL_TIME_STOP_WATCH_H_
