// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/logging/block_timer.h"

#include "src/developer/debug/shared/logging/debug.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_ipc {

BlockTimer::BlockTimer(FileLineFunction origin) : origin_(origin) {
  should_log_ = false;
  if (!IsLogCategoryActive(LogCategory::kTiming))
    return;

  timer_.Start();
  should_log_ = true;
  time_ = SecondsSinceStart();
  PushLogEntry(nullptr);
}

BlockTimer::~BlockTimer() {
  if (!IsLogCategoryActive(LogCategory::kTiming))
    return;

  if (!should_log_)
    return;

  double time = EndTimer();

  const char* unit = "ms";
  // We see if seconds makes more sense.
  if (time > 1000) {
    time /= 1000;
    // We write the full word to make more evident that this is 1000 times bigger that the normal
    // numbers you normally see.
    unit = "seconds";
  }

  auto context = stream_.str();
  std::stringstream ss;
  if (!context.empty())
    ss << "[" << context << "] ";
  ss << fxl::StringPrintf("Took %.3f %s.", time, unit);

  PopLogEntry(LogCategory::kTiming, origin_, ss.str(), time_);
}

double BlockTimer::EndTimer() { return timer_.Elapsed().ToMillisecondsF(); }

}  // namespace debug_ipc
