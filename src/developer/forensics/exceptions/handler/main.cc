// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/processargs.h>

#include <string>

#include "src/developer/forensics/exceptions/constants.h"
#include "src/developer/forensics/exceptions/handler/handler.h"

int main(int argc, char** argv) {
  using namespace forensics::exceptions;
  using namespace forensics::exceptions::handler;

  syslog::SetTags({"forensics", "exception"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());

  fit::promise<> handle_exception;

  // Besides the process name, we are expecting either (1) no argument and the exception on the
  // startup handle or (2) the crashed process name as argument, indicating an expired exception.
  if (argc == 1) {
    zx::exception exception(zx_take_startup_handle(PA_HND(PA_USER0, 0)));
    if (!exception.is_valid()) {
      FX_LOGS(FATAL) << "Received invalid exception";
      return EXIT_FAILURE;
    }

    handle_exception =
        Handle(std::move(exception), loop.dispatcher(),
               sys::ServiceDirectory::CreateFromNamespace(), kComponentLookupTimeout);
  } else if (argc == 3) {
    handle_exception = Handle(
        /*process_name=*/argv[1], /*process_koid=*/std::stoul(argv[2]), loop.dispatcher(),
        sys::ServiceDirectory::CreateFromNamespace(), kComponentLookupTimeout);
  } else {
    FX_LOGS(FATAL) << "Wrong number of arguments";
    return EXIT_FAILURE;
  }

  executor.schedule_task(
      handle_exception.then([&loop](const ::fit::result<>& result) { loop.Shutdown(); }));

  loop.Run();

  return EXIT_SUCCESS;
}
