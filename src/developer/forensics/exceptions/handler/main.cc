// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/exceptions/handler/main.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/processargs.h>

#include <memory>
#include <string>

#include "src/developer/forensics/exceptions/constants.h"
#include "src/developer/forensics/exceptions/handler/crash_reporter.h"

namespace forensics {
namespace exceptions {
namespace handler {

int main() {
  using forensics::exceptions::kComponentLookupTimeout;
  using Binding = fidl::Binding<forensics::exceptions::handler::CrashReporter,
                                std::unique_ptr<fuchsia::exception::internal::CrashReporter>>;

  syslog::SetTags({"forensics", "exception"});

  // We receive a channel that we interpret as a fuchsia.exception.internal.CrashReporter
  // connection.
  zx::channel channel(zx_take_startup_handle(PA_HND(PA_USER0, 0)));
  if (!channel.is_valid()) {
    FX_LOGS(FATAL) << "Received invalid channel";
    return EXIT_FAILURE;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto crash_reporter = std::make_unique<forensics::exceptions::handler::CrashReporter>(
      loop.dispatcher(), sys::ServiceDirectory::CreateFromNamespace(), kComponentLookupTimeout);

  Binding crash_reporter_binding(std::move(crash_reporter), std::move(channel), loop.dispatcher());
  crash_reporter_binding.set_error_handler([&loop](const zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to client";
    loop.Shutdown();
  });

  loop.Run();
  crash_reporter_binding.Close(ZX_OK);

  return EXIT_SUCCESS;
}

}  // namespace handler
}  // namespace exceptions
}  // namespace forensics
