// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_WRITER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_WRITER_H_

#include <string>
#include <vector>

#include "src/developer/forensics/feedback_data/system_log_recorder/log_message_store.h"

namespace forensics {
namespace feedback_data {

// Consumes the full content of a store on request, writing it to a rotating set of files.
class SystemLogWriter {
 public:
  SystemLogWriter(const std::vector<const std::string>& log_file_paths, LogMessageStore* store);

  void Write();

 private:
  // Deletes the last log file and shifts the remaining log files by one position: The first file
  // becomes the second file, the second file becomes the third file, and so on.
  void RotateFilePaths();

  // Truncates the first file to start anew.
  void StartNewFile();

  const std::vector<const std::string> file_paths_;

  int current_file_descriptor_ = -1;
  LogMessageStore* store_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_WRITER_H_
