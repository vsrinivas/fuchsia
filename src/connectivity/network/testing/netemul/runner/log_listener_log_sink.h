// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_LOG_LISTENER_LOG_SINK_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_LOG_LISTENER_LOG_SINK_H_

#include "log_listener.h"

namespace netemul {

namespace internal {

// LogListenerImpl that forwards logs to a log sink.
//
// If a valid log sink socket is passed to the constructor, log
// messages will be sent to that socket. Otherwise, if an invalid
// log sink socket is passed to the constructor, the parent
// environment's log sink will be used. All log messages will
// be tagged with the supplied prefix.
class LogListenerLogSinkImpl : public LogListenerImpl {
 public:
  LogListenerLogSinkImpl(
      fidl::InterfaceRequest<fuchsia::logger::LogListener> request,
      std::string prefix, bool klogs_enabled, zx::socket log_sink,
      async_dispatcher_t* dispatcher = nullptr);

 protected:
  virtual void LogImpl(fuchsia::logger::LogMessage m) override;

 private:
  // log_sock_
  //
  // System log socket that logs will be redirected to.
  zx::socket log_sock_;
};

}  // namespace internal
}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_LOG_LISTENER_LOG_SINK_H_
