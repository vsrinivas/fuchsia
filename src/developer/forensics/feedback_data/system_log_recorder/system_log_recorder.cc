// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/system_log_recorder/system_log_recorder.h"

#include <lib/async/cpp/task.h>

#include <functional>

#include "src/lib/diagnostics/stream/cpp/log_message.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

SystemLogRecorder::SystemLogRecorder(async_dispatcher_t* archive_dispatcher,
                                     async_dispatcher_t* write_dispatcher,
                                     std::shared_ptr<sys::ServiceDirectory> services,
                                     WriteParameters write_parameters,
                                     std::unique_ptr<Encoder> encoder)
    : write_dispatcher_(write_dispatcher),
      write_period_(write_parameters.period),
      store_(write_parameters.total_log_size_bytes / write_parameters.max_num_files,
             write_parameters.max_write_size_bytes, std::move(encoder)),
      archive_accessor_(archive_dispatcher, services, fuchsia::diagnostics::DataType::LOGS,
                        fuchsia::diagnostics::StreamMode::SNAPSHOT_THEN_SUBSCRIBE),
      writer_(write_parameters.logs_dir, write_parameters.max_num_files, &store_) {}

void SystemLogRecorder::Start() {
  archive_accessor_.Collect([this](fuchsia::diagnostics::FormattedContent chunk) {
    auto log_messages = diagnostics::stream::ConvertFormattedContentToLogMessages(std::move(chunk));
    if (log_messages.is_error()) {
      store_.Add(::fit::error(log_messages.take_error()));
      return;
    }

    for (auto& log_message : log_messages.value()) {
      store_.Add(std::move(log_message));
    }
  });

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
