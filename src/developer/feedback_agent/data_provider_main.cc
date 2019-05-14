// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/channel.h>
#include <stdlib.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <memory>

#include "src/developer/feedback_agent/data_provider.h"
#include "src/lib/fxl/strings/string_printf.h"

int main(int argc, const char** argv) {
  syslog::InitLogger({"feedback"});

  // This process is spawned by the feedback_agent process, which forwards it
  // the incoming request through PA_USER0.
  fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request(
      zx::channel(zx_take_startup_handle(PA_HND(PA_USER0, 0))));
  if (!request.is_valid()) {
    FX_LOGS(ERROR) << "Invalid incoming fuchsia.feedback.DataProvider request";
    return EXIT_FAILURE;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = sys::ComponentContext::Create();
  std::unique_ptr<fuchsia::feedback::DataProviderImpl> data_provider =
      fuchsia::feedback::DataProviderImpl::TryCreate(loop.dispatcher(),
                                                     context->svc());
  if (!data_provider) {
    return EXIT_FAILURE;
  }

  fidl::Binding<fuchsia::feedback::DataProvider> binding(data_provider.get());
  // TODO(DX-1497): in addition to exiting the process when the connection is
  // closed, we should have an internal timeout since the last call and exit the
  // process then in case clients don't close the connection themselves.
  binding.set_error_handler([&loop](zx_status_t status) {
    loop.Shutdown();
    // We exit successfully when the client closes the connection.
    if (status == ZX_ERR_PEER_CLOSED) {
      exit(0);
    } else {
      FX_LOGS(ERROR) << fxl::StringPrintf("Received channel error: %d (%s)",
                                          status, zx_status_get_string(status));
      exit(1);
    }
  });
  binding.Bind(std::move(request));

  loop.Run();

  return EXIT_SUCCESS;
}
