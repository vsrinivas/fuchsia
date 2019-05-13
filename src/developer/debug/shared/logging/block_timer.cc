// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/logging/block_timer.h"

#include "src/developer/debug/shared/logging/debug.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_ipc {

BlockTimer::BlockTimer(FileLineFunction origin)
    : origin_(origin), should_log_(IsDebugModeActive()) {
  timer_.Start();
}

BlockTimer::~BlockTimer() { EndTimer(); }

double BlockTimer::EndTimer() {
  if (!should_log_)
    return 0;

  // The timer won't trigger again.
  should_log_ = false;

  if (!IsLogCategoryActive(LogCategory::kTiming))
    return 0;

  const char* unit = "ms";
  double time = timer_.Elapsed().ToMillisecondsF();
  // We see if seconds makes more sense.
  if (time > 1000) {
    time /= 1000;
    // We write the full word to make more evident that this is 1000 times
    // bigger that the normal numbers you normally see.
    unit = "seconds";
  }

  auto preamble = LogPreamble(LogCategory::kTiming, origin_);
  auto context = stream_.str();

  std::stringstream ss;
  ss << "\r" << preamble;
  if (!context.empty())
    ss << "[" << context << "] ";
  ss << fxl::StringPrintf("Took %.3f %s.\r\n", time, unit);
  printf("%s", ss.str().c_str());
  fflush(stdout);

  return time;
}

}  // namespace debug_ipc
