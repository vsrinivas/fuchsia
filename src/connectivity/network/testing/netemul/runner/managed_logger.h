// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MANAGED_LOGGER_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MANAGED_LOGGER_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/zx/socket.h>

#include <memory>
#include <string>
#include <vector>

#include "log_listener.h"

namespace netemul {

class ManagedLogger {
 public:
  static constexpr int BufferSize = 4096;

  using Ptr = std::unique_ptr<ManagedLogger>;
  using ClosedCallback = fit::function<void(ManagedLogger*)>;

  ManagedLogger(std::string name, bool is_err,
                std::shared_ptr<fuchsia::logger::LogListenerSafe> loglistener);
  ~ManagedLogger();

  zx::handle CreateHandle();

  void OnRx(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
            const zx_packet_signal_t* signal);

  void Start(ClosedCallback callback);

 private:
  zx_status_t ReadSocket();
  void ConsumeBuffer();

  // Send a msg to |loglistener_impl|
  void LogMessage(std::string msg);

  async_dispatcher_t* dispatcher_;
  ClosedCallback closed_callback_;
  std::string name_;
  bool is_err_;
  zx::socket out_;
  // buffer is lazily created to decrease memory consumption
  std::unique_ptr<char[]> buffer_;
  size_t buffer_pos_;
  async::WaitMethod<ManagedLogger, &ManagedLogger::OnRx> wait_;
  std::shared_ptr<fuchsia::logger::LogListenerSafe> loglistener_;
};

class ManagedLoggerCollection {
 public:
  explicit ManagedLoggerCollection(std::string environment_name,
                                   std::shared_ptr<fuchsia::logger::LogListenerSafe> loglistener)
      : environment_name_(std::move(environment_name)),
        counter_(0),
        loglistener_(std::move(loglistener)) {}

  void IncrementCounter() { counter_++; }
  fuchsia::sys::FileDescriptorPtr CreateLogger(const std::string& url, bool err);

 private:
  std::string environment_name_;
  uint32_t counter_;
  std::shared_ptr<fuchsia::logger::LogListenerSafe> loglistener_;
  std::vector<ManagedLogger::Ptr> loggers_;
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MANAGED_LOGGER_H_
