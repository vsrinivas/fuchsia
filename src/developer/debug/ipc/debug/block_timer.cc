// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/debug/block_timer.h"

#include "src/developer/debug/ipc/debug/debug.h"

namespace debug_ipc {

BlockTimer::BlockTimer(FileLineFunction origin, const char* msg)
    : origin_(origin), should_log_(IsDebugModeActive()), msg_(msg) {
  if (should_log_)
    timer_.Start();
}

BlockTimer::~BlockTimer() { EndTimer(); }

void BlockTimer::EndTimer() {
  if (should_log_) {
    const char* unit = "ms";
    double time = timer_.Elapsed().ToMillisecondsF();
    // We see if seconds makes more sense.
    if (time > 1000) {
      time /= 1000;
      // We write the full word to make more evident that this is 1000 times
      // bigger that the normal numbers you normally see.
      unit = "seconds";
    }

    if (!msg_) {
      printf("\r[%.3fs]%s Took %.3f %s.\r\n", SecondsSinceStart(),
             origin_.ToStringWithBasename().c_str(), time, unit);
    } else {
      printf("\r[%.3fs]%s[%s] Took %.3f %s.\r\n", SecondsSinceStart(),
             origin_.ToStringWithBasename().c_str(), msg_, time, unit);
    }
    fflush(stdout);
  }

  // The timer won't trigger again.
  should_log_ = false;
}

}  // namespace debug_ipc
