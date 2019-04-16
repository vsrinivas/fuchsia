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

namespace netemul {

class ManagedLogger {
 public:
  using Ptr = std::unique_ptr<ManagedLogger>;
  using ClosedCallback = fit::function<void(ManagedLogger*)>;

  ManagedLogger(std::string env_name, std::string prefix, std::ostream* stream);

  zx::handle CreateHandle();

  void OnRx(async_dispatcher_t* dispatcher, async::WaitBase* wait,
            zx_status_t status, const zx_packet_signal_t* signal);

  void Start(ClosedCallback callback);

 private:
  void ConsumeBuffer();
  void FormatTime();

  async_dispatcher_t* dispatcher_;
  // pointer to output stream, not owned
  std::ostream* stream_;
  ClosedCallback closed_callback_;
  std::string env_name_;
  std::string prefix_;
  zx::socket out_;
  // buffer is lazily created to decrease memory consumption
  std::unique_ptr<char[]> buffer_;
  size_t buffer_pos_;
  async::WaitMethod<ManagedLogger, &ManagedLogger::OnRx> wait_;
};

class ManagedLoggerCollection {
 public:
  explicit ManagedLoggerCollection(std::string environment_name)
      : environment_name_(std::move(environment_name)), counter_(0) {}

  void IncrementCounter() { counter_++; }
  fuchsia::sys::FileDescriptorPtr CreateLogger(const std::string& url,
                                               bool err);

 private:
  std::string environment_name_;
  uint32_t counter_;
  std::vector<ManagedLogger::Ptr> loggers_;
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MANAGED_LOGGER_H_
