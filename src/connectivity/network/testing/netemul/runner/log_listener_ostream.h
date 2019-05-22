// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_LOG_LISTENER_OSTREAM_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_LOG_LISTENER_OSTREAM_H_

#include <ostream>

#include "log_listener.h"

namespace netemul {
namespace internal {

// LogListenerImpl that writes parsed logs to a std::ostream.
class LogListenerOStreamImpl : public LogListenerImpl {
 public:
  LogListenerOStreamImpl(
      fidl::InterfaceRequest<fuchsia::logger::LogListener> request,
      std::string prefix, bool klogs_enabled, std::ostream* stream,
      async_dispatcher_t* dispatcher = nullptr);

 protected:
  virtual void LogImpl(fuchsia::logger::LogMessage m) override;

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

  // stream_
  //
  // Output stream where formatted logs will be sent to.
  std::ostream* stream_;
};

}  // namespace internal
}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_LOG_LISTENER_OSTREAM_H_
