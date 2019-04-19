// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_AGENT_LOG_LISTENER_H_
#define SRC_DEVELOPER_FEEDBACK_AGENT_LOG_LISTENER_H_

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <inttypes.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>

#include <vector>

namespace fuchsia {
namespace feedback {

// Collects system log.
//
// fuchsia::logger::Log is expected to be in |services|.
std::optional<fuchsia::mem::Buffer> CollectSystemLog(
    std::shared_ptr<::sys::ServiceDirectory> services);

class LogListener : public fuchsia::logger::LogListener {
 public:
  LogListener(fidl::InterfaceRequest<fuchsia::logger::LogListener> request,
              async_dispatcher_t* dispatcher, fit::function<void()> done);

  std::string CurrentLogs() { return logs_; }

  // |fuchsia::logger::LogListener|
  void LogMany(::std::vector<fuchsia::logger::LogMessage> log) override;
  void Log(fuchsia::logger::LogMessage log) override;
  void Done() override;

 private:
  fidl::Binding<fuchsia::logger::LogListener> binding_;
  const fit::function<void()> done_;

  std::string logs_;
};

}  // namespace feedback
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FEEDBACK_AGENT_LOG_LISTENER_H_
