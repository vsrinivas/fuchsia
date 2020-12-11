// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_EXCEPTIONS_TESTS_CRASHER_WRAPPER_H_
#define SRC_DEVELOPER_FORENSICS_EXCEPTIONS_TESTS_CRASHER_WRAPPER_H_

#include <lib/zx/channel.h>
#include <lib/zx/exception.h>
#include <lib/zx/job.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <zircon/syscalls/exception.h>

#include <string>

namespace forensics {
namespace exceptions {

// This struct represents all the state needed to keep correct track of an exception.
// It has the owning job and process from the exception.
// The thread can be obtained from the exception if needed.
struct ExceptionContext {
  zx::job job;
  zx::port port;
  zx::channel exception_channel;

  zx::exception exception;
  zx_exception_info_t exception_info;

  zx::process process;
  zx_koid_t process_koid;
  std::string process_name;

  zx::thread thread;
  zx_koid_t thread_koid;
  std::string thread_name;
};

// Spawns a process that will crash and waits for the exception.
// Returns |true| if the process was crashed and the exception was retrieved successfully.
bool SpawnCrasher(ExceptionContext* pe);

bool MarkExceptionAsHandled(ExceptionContext* pe);

}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_TESTS_CRASHER_WRAPPER_H_
