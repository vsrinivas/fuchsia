// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_SYSTEM_LOG_RECORDER_LISTENER_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_SYSTEM_LOG_RECORDER_LISTENER_H_

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <vector>

#include "src/developer/feedback/feedback_agent/system_log_recorder/log_message_store.h"

namespace feedback {

// Listens to incoming logs and immediately adds them to a store.
class SystemLogListener : public fuchsia::logger::LogListener {
 public:
  SystemLogListener(std::shared_ptr<sys::ServiceDirectory> services, LogMessageStore* store);

  void StartListening();

 private:
  // |fuchsia::logger::LogListener|
  void Log(fuchsia::logger::LogMessage message) override;
  void LogMany(std::vector<fuchsia::logger::LogMessage> messages) override;
  void Done() override;

  const std::shared_ptr<sys::ServiceDirectory> services_;
  LogMessageStore* store_;
  fidl::Binding<fuchsia::logger::LogListener> binding_;

  fuchsia::logger::LogPtr logger_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_SYSTEM_LOG_RECORDER_LISTENER_H_
