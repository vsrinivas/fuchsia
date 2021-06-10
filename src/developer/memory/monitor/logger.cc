

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/logger.h"

#include <lib/syslog/cpp/macros.h>

#include "lib/zx/time.h"
#include "src/developer/memory/metrics/printer.h"

namespace monitor {

using namespace memory;

const zx::duration log_durations[kNumLevels] = {zx::sec(30), zx::min(1), zx::min(5), zx::min(10)};

void Logger::SetPressureLevel(Level l) {
  duration_ = log_durations[l];
  task_.Cancel();
  task_.PostDelayed(dispatcher_, zx::usec(1));
}

void Logger::Log() {
  Capture c;
  auto s = capture_cb_(&c);
  if (s != ZX_OK) {
    FX_LOGS_FIRST_N(INFO, 1) << "Error getting Capture: " << s;
    return;
  }
  Digest d;
  digest_cb_(c, &d);
  std::ostringstream oss;
  Printer p(oss);

  p.PrintDigest(d);
  auto str = oss.str();
  std::replace(str.begin(), str.end(), '\n', ' ');
  FX_LOGS(INFO) << str;

  task_.PostDelayed(dispatcher_, duration_);
}

}  // namespace monitor
