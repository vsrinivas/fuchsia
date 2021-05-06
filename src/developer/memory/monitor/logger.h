// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_MEMORY_MONITOR_LOGGER_H_
#define SRC_DEVELOPER_MEMORY_MONITOR_LOGGER_H_

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>

#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/digest.h"
#include "src/developer/memory/monitor/pressure_observer.h"

namespace monitor {

class Logger {
 public:
  using CaptureCb = fit::function<zx_status_t(memory::Capture*)>;
  using DigestCb = fit::function<void(const memory::Capture&, memory::Digest*)>;

  Logger(async_dispatcher_t* dispatcher, CaptureCb capture_cb, DigestCb digest_cb)
      : dispatcher_(dispatcher),
        capture_cb_(std::move(capture_cb)),
        digest_cb_(std::move(digest_cb)) {}

  void SetPressureLevel(Level l);

 private:
  void Log();
   async_dispatcher_t* dispatcher_;
   CaptureCb capture_cb_;
   DigestCb digest_cb_;
   zx::duration duration_;
   async::TaskClosureMethod<Logger, &Logger::Log> task_{this};

   FXL_DISALLOW_COPY_AND_ASSIGN(Logger);
};

}  // namespace monitor

#endif  // SRC_DEVELOPER_MEMORY_MONITOR_LOGGER_H_
