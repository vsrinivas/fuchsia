// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/system_log_recorder/system_log_recorder.h"

#include <lib/async/cpp/task.h>

#include <functional>

namespace feedback {

SystemLogRecorder::SystemLogRecorder(async_dispatcher_t* write_dispatcher,
                                     std::shared_ptr<sys::ServiceDirectory> services,
                                     WriteParameters write_parameters)
    : write_dispatcher_(write_dispatcher),
      write_period_(write_parameters.period),
      store_(write_parameters.max_write_size_bytes),
      listener_(services, &store_),
      writer_(write_parameters.log_file_paths, write_parameters.total_log_size, &store_) {}

void SystemLogRecorder::Start() {
  listener_.StartListening();
  async::PostTask(write_dispatcher_, [this] { PeriodicWriteTask(); });
}

void SystemLogRecorder::PeriodicWriteTask() {
  writer_.Write();
  async::PostDelayedTask(
      write_dispatcher_, [this] { PeriodicWriteTask(); }, write_period_);
}

}  // namespace feedback
