// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_SYSTEM_LOG_RECORDER_SYSTEM_LOG_RECORDER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_SYSTEM_LOG_RECORDER_SYSTEM_LOG_RECORDER_H_

#include <lib/sys/cpp/service_directory.h>

#include "lib/zx/time.h"
#include "src/developer/feedback/feedback_agent/system_log_recorder/listener.h"
#include "src/developer/feedback/feedback_agent/system_log_recorder/log_message_store.h"
#include "src/developer/feedback/feedback_agent/system_log_recorder/writer.h"
#include "src/developer/feedback/utils/file_size.h"

namespace feedback {

class SystemLogRecorder {
 public:
  struct WriteParameters {
    zx::duration period;
    size_t max_write_size_bytes;
    const std::vector<const std::string> log_file_paths;
    FileSize total_log_size;
  };

  SystemLogRecorder(async_dispatcher_t* write_dispatcher,
                    std::shared_ptr<sys::ServiceDirectory> services,
                    WriteParameters write_parameters);
  void Start();

 private:
  void PeriodicWriteTask();

  async_dispatcher_t* write_dispatcher_;
  const zx::duration write_period_;

  LogMessageStore store_;
  SystemLogListener listener_;
  SystemLogWriter writer_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_SYSTEM_LOG_RECORDER_SYSTEM_LOG_RECORDER_H_
