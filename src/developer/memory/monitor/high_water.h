// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_MEMORY_MONITOR_HIGH_WATER_H_
#define SRC_DEVELOPER_MEMORY_MONITOR_HIGH_WATER_H_

#include <lib/fit/function.h>

#include <string>

#include <src/lib/fxl/macros.h>

#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/summary.h"
#include "src/developer/memory/metrics/watcher.h"

namespace monitor {

class HighWater {
 public:
  explicit HighWater(const std::string& dir, zx::duration poll_frequency,
                     uint64_t high_water_threshold, async_dispatcher_t* dispatcher,
                     fit::function<zx_status_t(memory::Capture&, memory::CaptureLevel)> capture_cb);
  ~HighWater() = default;

  void RecordHighWater(const memory::Capture& capture);
  std::string GetHighWater() const;
  std::string GetPreviousHighWater() const;

 private:
  const std::string dir_;
  memory::Watcher watcher_;
  memory::Namer namer_;
  FXL_DISALLOW_COPY_AND_ASSIGN(HighWater);
};

}  // namespace monitor

#endif  // SRC_DEVELOPER_MEMORY_MONITOR_HIGH_WATER_H_
