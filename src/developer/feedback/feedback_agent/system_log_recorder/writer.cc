// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/system_log_recorder/writer.h"

#include <lib/trace/event.h>

namespace feedback {

SystemLogWriter::SystemLogWriter(const std::vector<const std::string>& file_paths,
                                 const FileSize total_log_size, LogMessageStore* store)
    : logs_(file_paths, total_log_size), store_(store) {}

void SystemLogWriter::Write() {
  TRACE_DURATION("feedback:io", "SystemLogWriter::Write");
  logs_.Write(store_->Consume());
}

}  // namespace feedback
