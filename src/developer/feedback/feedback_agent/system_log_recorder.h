// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_SYSTEM_LOG_RECORDER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_SYSTEM_LOG_RECORDER_H_

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/service_directory.h>

#include "src/developer/feedback/utils/file_size.h"
#include "src/developer/feedback/utils/log_message_queue.h"
#include "src/developer/feedback/utils/rotating_file_set.h"

namespace feedback {

// Get the device's system log and persist them to files, using at most a fixed number of bytes.
class SystemLogRecorder : public fuchsia::logger::LogListener {
 public:
  SystemLogRecorder(std::shared_ptr<sys::ServiceDirectory> services,
                    const std::vector<const std::string>& file_paths, FileSize total_log_size);

  void StartRecording();

 private:
  void StartListening();
  void SpawnWriterThread();

  // |fuchsia::logger::LogListener|
  void Log(fuchsia::logger::LogMessage message) override;
  void LogMany(std::vector<fuchsia::logger::LogMessage> messages) override;
  void Done() override;

  const std::shared_ptr<sys::ServiceDirectory> services_;
  fidl::Binding<fuchsia::logger::LogListener> binding_;
  fuchsia::logger::LogPtr logger_;

  LogMessageQueue queue_;
  RotatingFileSetWriter logs_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_SYSTEM_LOG_RECORDER_H_
