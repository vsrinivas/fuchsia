// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_AGENT_LOG_LISTENER_H_
#define SRC_DEVELOPER_FEEDBACK_AGENT_LOG_LISTENER_H_

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <inttypes.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <vector>

namespace fuchsia {
namespace feedback {

// Collects system log.
//
// fuchsia::logger::Log is expected to be in |services|.
fit::promise<fuchsia::mem::Buffer> CollectSystemLog(
    std::shared_ptr<::sys::ServiceDirectory> services);

class LogListener : public fuchsia::logger::LogListener {
 public:
  explicit LogListener(std::shared_ptr<::sys::ServiceDirectory> services);

  // Collects the logs and returns a promise to when the collection is done.
  fit::promise<void> CollectLogs();

  // Returns the logs that have been collected so far.
  std::string CurrentLogs() { return logs_; }

 private:
  // |fuchsia::logger::LogListener|
  void LogMany(::std::vector<fuchsia::logger::LogMessage> log) override;
  void Log(fuchsia::logger::LogMessage log) override;
  void Done() override;

  const std::shared_ptr<::sys::ServiceDirectory> services_;
  fidl::Binding<fuchsia::logger::LogListener> binding_;

  std::string logs_;

  fit::bridge<void, void> done_;
};

}  // namespace feedback
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FEEDBACK_AGENT_LOG_LISTENER_H_
