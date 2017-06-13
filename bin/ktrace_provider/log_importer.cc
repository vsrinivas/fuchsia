// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls/log.h>
#include <mx/log.h>

#include "apps/tracing/lib/trace/writer.h"
#include "apps/tracing/src/ktrace_provider/log_importer.h"
#include "lib/ftl/logging.h"

namespace ktrace_provider {

LogImporter::LogImporter() = default;
LogImporter::~LogImporter() {
  Stop();
}

void LogImporter::Start() {
  FTL_DCHECK(!is_running());
  mx::log log_handle;
  if (mx::log::create(&log_handle, 0) != MX_OK) {
    FTL_LOG(ERROR) << "Failed to open kernel log";
    return;
  }

  auto start_timestamp = tracing::GetTicksNow();
  stop_timestamp_.store(std::numeric_limits<tracing::Ticks>::max());
  worker_ = std::thread([
    this, start_timestamp = std::move(start_timestamp),
    log_handle = std::move(log_handle)
  ]() {
    FTL_VLOG(2) << "Kernel log reader thread starts.";
    char log_buffer[MX_LOG_RECORD_MAX];
    mx_log_record_t* log_record =
        reinterpret_cast<mx_log_record_t*>(log_buffer);

    for (;;) {
      mx_status_t status =
          mx_log_read(log_handle.get(), MX_LOG_RECORD_MAX, log_record, 0);
      if (status == MX_ERR_SHOULD_WAIT) {
        mx_object_wait_one(log_handle.get(), MX_LOG_READABLE, MX_TIME_INFINITE,
                           nullptr);
        continue;
      }
      if (status != MX_OK) {
        break;
      }
      if (log_record->timestamp < start_timestamp)
        continue;
      if (log_record->timestamp >=
          stop_timestamp_.load(std::memory_order_acquire))
        break;
      if (auto writer = tracing::writer::TraceWriter::Prepare()) {
        writer.WriteLogRecord(
            log_record->timestamp,
            writer.RegisterThread(log_record->pid, log_record->tid),
            log_record->data, log_record->datalen);
      } else {
        break;
      }
    }

    FTL_VLOG(2) << "Kernel log reader thread ends.";
  });
}

void LogImporter::Stop() {
  if (!is_running())
    return;

  stop_timestamp_.store(tracing::GetTicksNow(), std::memory_order_release);
  // This message makes sure that the log import thread sees
  // running false and thus ends correctly.
  // TODO: LogImporter should have a way to unblock an mx_log_read.
  FTL_LOG(INFO) << "Stopping log import thread";

  worker_.join();
}

}  // namespace ktrace_provider
