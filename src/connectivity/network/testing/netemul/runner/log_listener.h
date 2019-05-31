// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_LOG_LISTENER_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_LOG_LISTENER_H_

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
class LogListenerImpl : public fuchsia::logger::LogListener {
 public:
  LogListenerImpl(fidl::InterfaceRequest<fuchsia::logger::LogListener> request,
                  std::string prefix, async_dispatcher_t* dispatcher = nullptr);

  /* Actual implementation (overrides) of fuchsia::logger::LogListener stubs */

  virtual void Log(fuchsia::logger::LogMessage m) override;

  virtual void LogMany(std::vector<fuchsia::logger::LogMessage> ms) override;

  virtual void Done() override;

 protected:
  virtual void LogImpl(fuchsia::logger::LogMessage m) = 0;

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

  // dropped_logs_
  //
  // counter for number of dropped logs when logging to
  // log_sock_.
  uint32_t dropped_logs_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LogListenerImpl);
};

}  // namespace internal

// LogListener
//
// This class holds a LogListenerImpl to get logs from a provided
// ManagedEnvironment which starts a Log service (fuchsia.logger.Log)
class LogListener final {
 public:
  // LogListener
  //
  // Constructs the LogListener object.
  LogListener(
      std::unique_ptr<fuchsia::logger::LogFilterOptions> log_filter_options,
      fidl::InterfaceHandle<fuchsia::logger::LogListener> loglistener_handle,
      std::shared_ptr<internal::LogListenerImpl> impl);

  // Returns true if we can bind to a LogSink in a managed environment.
  bool Bindable() const;

  // Binds our log listener to a log service
  void BindToLogService(fuchsia::netemul::environment::ManagedEnvironment* env);

  // Returns our LogListenerImpl.
  std::shared_ptr<internal::LogListenerImpl> GetLogListenerImpl() const;

  // Create a LogListener in the managed environment |env|.
  //
  // Create a LogListener instance that listens to logs
  // from the ManagedEnvironment |env|. |prefix| will be prepended
  // before each and every log from the created log listener.
  static std::unique_ptr<LogListener> Create(
      fuchsia::netemul::environment::LoggerOptions logger_options,
      const std::string& prefix, async_dispatcher_t* dispatcher = nullptr);

  /// Checks whether klogs is enabled based on environment options in
  /// |env_options|
  static bool IsKlogsEnabled(
      const fuchsia::netemul::environment::EnvironmentOptions& env_options) {
    return env_options.has_logger_options() &&
           env_options.logger_options().has_klogs_enabled() &&
           env_options.logger_options().klogs_enabled();
  }

 private:
  // Log filter options for when we bind.
  std::unique_ptr<fuchsia::logger::LogFilterOptions> log_filter_options_;

  // Client handle for log listener.
  fidl::InterfaceHandle<fuchsia::logger::LogListener> loglistener_handle_;

  // Implementation of the LogListener interface (fuchsia.logger.LogListener)
  std::shared_ptr<internal::LogListenerImpl> loglistener_impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LogListener);
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_LOG_LISTENER_H_
