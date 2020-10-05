// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/system_log_recorder/system_log_recorder.h"

#include <lib/async/cpp/task.h>

#include <functional>

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

SystemLogRecorder::SystemLogRecorder(async_dispatcher_t* write_dispatcher,
                                     std::shared_ptr<sys::ServiceDirectory> services,
                                     WriteParameters write_parameters,
                                     std::unique_ptr<Encoder> encoder)
    : write_dispatcher_(write_dispatcher),
      write_period_(write_parameters.period),
      store_(write_parameters.total_log_size_bytes / write_parameters.max_num_files,
             write_parameters.max_write_size_bytes, std::move(encoder)),
      listener_(services, &store_),
      writer_(write_parameters.logs_dir, write_parameters.max_num_files, &store_) {}

void SystemLogRecorder::Start() {
  listener_.StartListening();
  async::PostTask(write_dispatcher_, [this] { PeriodicWriteTask(); });
}

void SystemLogRecorder::PeriodicWriteTask() {
  writer_.Write();
  async::PostDelayedTask(
      write_dispatcher_, [this] { PeriodicWriteTask(); }, write_period_);
}

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
