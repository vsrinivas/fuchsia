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

#include "src/developer/forensics/exceptions/exception_handler/handler.h"

int main(int argc, char** argv) {
  using namespace forensics::exceptions;

  syslog::SetTags({"exception-broker"});
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Executor executor(loop.dispatcher());

  zx::exception exception(zx_take_startup_handle(PA_HND(PA_USER0, 0)));
  if (!exception.is_valid()) {
    FX_LOGS(FATAL) << "Received invalid exception";
    return EXIT_FAILURE;
  }

  // The handler waits on responses from the component lookup service and crash reporting services
  // for 30 seconds each. Note, 60 seconds is not the upper bound on how long crash reporting takes
  // in total because minidump generation happens in parallel with component lookup, but may take 
  // longer than 30 seconds.
  constexpr zx::duration kComponentLookupTimeout{zx::sec(30)};
  constexpr zx::duration kCrashReporterTimeout{zx::sec(30)};

  executor.schedule_task(forensics::exceptions::Handle(std::move(exception), loop.dispatcher(),
                                                       sys::ServiceDirectory::CreateFromNamespace(),
                                                       kComponentLookupTimeout,
                                                       kCrashReporterTimeout)
                             .then([&loop](const ::fit::result<>& result) { loop.Shutdown(); }));

  loop.Run();

  return EXIT_SUCCESS;
}
