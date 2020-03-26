// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_SYSTEM_LOG_RECORDER_WRITER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_SYSTEM_LOG_RECORDER_WRITER_H_

#include <string>
#include <vector>

#include "src/developer/feedback/feedback_agent/system_log_recorder/log_message_store.h"
#include "src/developer/feedback/utils/file_size.h"
#include "src/developer/feedback/utils/rotating_file_set.h"

namespace feedback {

// Consumes the full content of a store on request, writing it to a rotating set of files.
class SystemLogWriter {
 public:
  SystemLogWriter(const std::vector<const std::string>& log_file_paths, FileSize total_log_size,
                  LogMessageStore* store);

  void Write();

 private:
  RotatingFileSetWriter logs_;
  LogMessageStore* store_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_SYSTEM_LOG_RECORDER_WRITER_H_
