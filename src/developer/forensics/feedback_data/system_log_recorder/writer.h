// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_WRITER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_WRITER_H_

#include <deque>
#include <string>
#include <vector>

#include <fbl/unique_fd.h>

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/log_message_store.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

// Consumes the full content of a store on request, writing it to a rotating set of files.
class SystemLogWriter {
 public:
  SystemLogWriter(const std::string& logs_dir, size_t max_num_files,
                  zx::duration min_fsync_interval_, LogMessageStore* store);

  void Write();

  // Instructs the class to call `fsync` after writes, if sufficient time, has passed to ensure
  // data makes it disk.
  //
  // If |finalized| is set to true, future calls to this method have no effect.
  void SetFsyncOnWrite(bool enable, bool finalized = false);

 private:
  // Truncates the first file to start anew.
  void StartNewFile();

  // Returns the path the |file_num|'th file created.
  std::string Path(size_t file_num) const;

  const std::string logs_dir_;
  const size_t max_num_files_;
  std::deque<size_t> file_queue_;

  bool call_fsync_{false};
  bool call_fsync_finalized_{false};

  zx::time last_fsync_time_{zx::time::infinite_past()};
  zx::duration min_fsync_interval_;

  fbl::unique_fd current_file_descriptor_;
  LogMessageStore* store_;
};

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_WRITER_H_
