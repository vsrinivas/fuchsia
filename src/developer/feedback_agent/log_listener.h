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
#include <zircon/time.h>

#include <vector>

#include "src/lib/fxl/functional/cancelable_callback.h"

namespace fuchsia {
namespace feedback {

// Collects system log.
//
// fuchsia::logger::Log is expected to be in |services|.
fit::promise<fuchsia::mem::Buffer> CollectSystemLog(
    std::shared_ptr<::sys::ServiceDirectory> services, zx::duration timeout);

class LogListener : public fuchsia::logger::LogListener {
 public:
  explicit LogListener(std::shared_ptr<::sys::ServiceDirectory> services);

  // Collects the logs and returns a promise to when the collection is done or
  // the timeout over.
  fit::promise<void> CollectLogs(zx::duration timeout);

  // Returns the logs that have been collected so far.
  std::string CurrentLogs() { return logs_; }

 private:
  // |fuchsia::logger::LogListener|
  void LogMany(::std::vector<fuchsia::logger::LogMessage> log) override;
  void Log(fuchsia::logger::LogMessage log) override;
  void Done() override;

  // Resets |done_| and |done_after_timeout_|.
  void Reset();

  const std::shared_ptr<::sys::ServiceDirectory> services_;
  fidl::Binding<fuchsia::logger::LogListener> binding_;

  // Wether LogMany() was called since the last call to CollectLogs().
  // This is to help debug FLK-179.
  bool log_many_called_ = false;

  std::string logs_;

  // We use a shared_ptr to share the bridge between this and the async loop on
  // which we post the delayed task to timeout.
  std::shared_ptr<fit::bridge<void, void>> done_;

  // We wrap the delayed task we post on the async loop to timeout in a
  // CancelableClosure so we can cancel it if we are done another way.
  fxl::CancelableClosure done_after_timeout_;
};

}  // namespace feedback
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FEEDBACK_AGENT_LOG_LISTENER_H_
