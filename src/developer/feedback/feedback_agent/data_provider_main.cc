// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <cstdlib>
#include <memory>

#include "src/developer/feedback/feedback_agent/data_provider.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"

namespace {

constexpr zx::duration kTimeout = zx::min(10);

}  // namespace

int main(int argc, const char** argv) {
  syslog::InitLogger({"feedback"});

  FXL_CHECK(argc == 2) << "feedback_agent is supposed to spawn us with two arguments";
  const std::string process_identifier = fxl::StringPrintf("%s (connection %s)", argv[0], argv[1]);
  FX_LOGS(INFO) << "Client opened a new connection to fuchsia.feedback.DataProvider. Spawned "
                << process_identifier;

  // This process is spawned by feedback_agent, which forwards it the incoming request through
  // PA_USER0.
  fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request(
      zx::channel(zx_take_startup_handle(PA_HND(PA_USER0, 0))));
  if (!request.is_valid()) {
    FX_LOGS(ERROR) << "Invalid incoming fuchsia.feedback.DataProvider request";
    return EXIT_FAILURE;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  std::unique_ptr<fidl::Binding<fuchsia::feedback::DataProvider>> binding;
  // Set |data_provider| to close the channel and kill the process after 10 minutes of
  // inactivity.
  std::unique_ptr<feedback::DataProvider> data_provider = feedback::DataProvider::TryCreate(
      loop.dispatcher(), context->svc(),
      [&] {
        // We destroy |data_provider| before shutting down the loop so it cleans up its connection
        // handlers and does not trigger their error handlers.
        data_provider.reset();
        loop.Shutdown();
        if (const auto status = binding->Close(ZX_ERR_TIMED_OUT) != ZX_OK) {
          FX_PLOGS(ERROR, status) << "Error closing connection to client";
        }
        FX_LOGS(INFO) << fxl::StringPrintf(
            "Last client call to fuchsia.feedback.DataProvider was %lu minutes ago. Exiting %s",
            kTimeout.to_mins(), process_identifier.c_str());
        exit(EXIT_FAILURE);
      },
      /*timeout=*/kTimeout);

  if (!data_provider) {
    return EXIT_FAILURE;
  }

  binding = std::make_unique<fidl::Binding<fuchsia::feedback::DataProvider>>(data_provider.get());
  binding->set_error_handler([&](zx_status_t status) {
    // We destroy |data_provider| before shutting down the loop so it cleans up its connection
    // handlers and does not trigger their error handlers.
    data_provider.reset();
    loop.Shutdown();
    // We exit successfully when the client closes the connection.
    if (status == ZX_ERR_PEER_CLOSED) {
      FX_LOGS(INFO) << "Client closed the connection to fuchsia.feedback.DataProvider. Exiting "
                    << process_identifier;
      exit(EXIT_SUCCESS);
    } else {
      FX_PLOGS(ERROR, status) << "Received channel error. Exiting " << process_identifier;
      exit(EXIT_FAILURE);
    }
  });
  binding->Bind(std::move(request));

  loop.Run();

  return EXIT_SUCCESS;
}
