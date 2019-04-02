// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETEMUL_RUNNER_LOG_LISTENER_H_
#define GARNET_BIN_NETEMUL_RUNNER_LOG_LISTENER_H_

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/netemul/environment/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <src/lib/fxl/macros.h>
#include <iostream>
#include <memory>

namespace netemul {

namespace internal {

// LogListenerImpl
//
// Implement the fuchsia::logger::LogListener interface.
// This is not a replacement for ManagedLogger, as ManagedLogger
// is used to handle the stdout and stderr of processes. This is
// used to handle the logs sent to the LogSink service
class LogListenerImpl final : public fuchsia::logger::LogListener {
 public:
  explicit LogListenerImpl(
      fidl::InterfaceRequest<fuchsia::logger::LogListener> request,
      std::string prefix, std::ostream* stream, bool klogs_enabled,
      async_dispatcher_t* dispatcher = nullptr);

  /* Actual implementation (overrides) of fuchsia::logger::LogListener stubs */

  virtual void Log(fuchsia::logger::LogMessage m) override;

  virtual void LogMany(std::vector<fuchsia::logger::LogMessage> ms) override;

  virtual void Done() override;

 private:
  // FormatTime
  //
  // Format the time to monotomic and send it to |stream_|.
  void FormatTime(const zx_time_t timestamp);

  // FormatTags
  //
  // Format the tags and send it to |stream_|.
  void FormatTags(const std::vector<std::string>& tags);

  // FormatLogLevel
  //
  // Format the log level and send it to |stream_|.
  void FormatLogLevel(const int32_t severity);

  // binding_
  //
  // Binding object that will listen for messages from a
  // channel and handle dispatching (call the appropriate
  // stub implemenation in this class).
  fidl::Binding<fuchsia::logger::LogListener> binding_;

  // prefix_
  //
  // Prefix to print before each and every log.
  std::string prefix_;

  // stream_
  //
  // Output stream where formatted logs will be sent to.
  std::ostream* stream_;

  // klogs_enabled_
  //
  // Klog log visibility.
  bool klogs_enabled_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LogListenerImpl);
};

}  // namespace internal

// LogListener
//
// This class implements a log listener to get logs from a provided
// ManagedEnvironment which starts a Log service (fuchsia.logger.Log)
class LogListener final {
 public:
  // LogListener
  //
  // Constructs the LogListener object. All logs will be sent
  // to `stream`. `prefix` will appear before each and every log
  LogListener(std::unique_ptr<internal::LogListenerImpl> impl);

  // Create
  //
  // Create a LogListener instance that listens to logs
  // from the ManagedEnvironment `env`. `prefix` will be prepended
  // before each and every log from the created log listener.
  static std::unique_ptr<LogListener> Create(
      fuchsia::netemul::environment::ManagedEnvironment* env,
      const fuchsia::netemul::environment::LoggerOptions& logger_options,
      const std::string& prefix, async_dispatcher_t* dispatcher = nullptr);

 private:
  // Implementation of the LogListener interface (fuchsia.logger.LogListener)
  std::unique_ptr<internal::LogListenerImpl> loglistener_impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LogListener);
};

}  // namespace netemul

#endif  // GARNET_BIN_NETEMUL_RUNNER_LOG_LISTENER_H_
