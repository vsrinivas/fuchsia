// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/zx/time.h>

#include <string>
#include <vector>

#include "src/lib/fxl/logging.h"

namespace fuchsia {
namespace feedback {

// Returns a LogMessage with the given severity, message and optional tags.
// The process and thread ids are constants. The timestamp is a constant plus
// the provided offset.
fuchsia::logger::LogMessage BuildLogMessage(
    const int32_t severity, const std::string& text,
    const zx_time_t timestamp_offset,
    const std::vector<std::string>& tags = {});

// Stub Log service to return canned responses to Log::DumpLogs().
class StubLogger : public fuchsia::logger::Log {
 public:
  // Returns a request handler for binding to this stub service.
  fidl::InterfaceRequestHandler<fuchsia::logger::Log> GetHandler() {
    return bindings_.GetHandler(this);
  }

  // fuchsia::logger::Log methods.
  void Listen(
      fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
      std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override {
    FXL_NOTIMPLEMENTED();
  }
  void DumpLogs(
      fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
      std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;

  // Stub injection methods.
  void set_messages(const std::vector<fuchsia::logger::LogMessage>& messages) {
    messages_ = messages;
  }

 private:
  fidl::BindingSet<fuchsia::logger::Log> bindings_;
  std::vector<fuchsia::logger::LogMessage> messages_;
};

}  // namespace feedback
}  // namespace fuchsia
