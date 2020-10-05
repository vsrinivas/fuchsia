// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_WRITER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_WRITER_H_

#include <deque>
#include <string>
#include <vector>

#include "src/developer/forensics/feedback_data/system_log_recorder/log_message_store.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

// Consumes the full content of a store on request, writing it to a rotating set of files.
class SystemLogWriter {
 public:
  SystemLogWriter(const std::string& logs_dir, size_t max_num_files, LogMessageStore* store);

  void Write();

 private:
  // Truncates the first file to start anew.
  void StartNewFile();

  // Returns the path the |file_num|'th file created.
  std::string Path(size_t file_num) const;

  const std::string logs_dir_;
  const size_t max_num_files_;
  std::deque<size_t> file_queue_;

  int current_file_descriptor_ = -1;
  LogMessageStore* store_;
};

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_WRITER_H_
