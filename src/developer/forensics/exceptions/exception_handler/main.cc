// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/processargs.h>

#include "src/developer/forensics/exceptions/exception_handler/handler.h"

int main(int argc, char** argv) {
  using namespace forensics::exceptions;

  syslog::SetTags({"exception-broker"});
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  zx::exception exception(zx_take_startup_handle(PA_HND(PA_USER0, 0)));
  if (!exception.is_valid()) {
    FX_LOGS(FATAL) << "Received invalid exception";
    return EXIT_FAILURE;
  }

  Handler handler(sys::ServiceDirectory::CreateFromNamespace());
  handler.Handle(std::move(exception), [&loop] { loop.Shutdown(); });

  loop.Run();

  return EXIT_SUCCESS;
}
