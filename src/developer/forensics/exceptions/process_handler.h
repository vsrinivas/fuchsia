// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_EXCEPTIONS_PROCESS_HANDLER_H_
#define SRC_DEVELOPER_FORENSICS_EXCEPTIONS_PROCESS_HANDLER_H_

#include <fuchsia/exception/internal/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/exception.h>
#include <lib/zx/process.h>

namespace forensics {
namespace exceptions {

// Handles a pending exception by handing it off to a subprocess. The lifetime of the subprocess
// is automatically managed and it is replaced if it crashes.
class ProcessHandler {
 public:
  ProcessHandler(async_dispatcher_t* dispatcher, fit::closure on_available);
  ~ProcessHandler();

  ProcessHandler(ProcessHandler&&) = default;
  ProcessHandler& operator=(ProcessHandler&&) = default;

  void Handle(const std::string& crashed_process_name, zx_koid_t crashed_process_koid,
              zx::exception exception);

 private:
  async_dispatcher_t* dispatcher_;
  fit::closure on_available_;

  zx::process subprocess_;
  fuchsia::exception::internal::CrashReporterPtr crash_reporter_;
};

}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_PROCESS_HANDLER_H_
