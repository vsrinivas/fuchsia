// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include "src/developer/exception_broker/exception_broker.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/syslog/cpp/logger.h"

using fuchsia::exception::ProcessLimbo;
using fuchsia::exception::ProcessLimboHandler;

namespace {

void LogProcessLimboStatus(const fuchsia::exception::ProcessLimboManager& limbo) {
  if (!limbo.active()) {
    FX_LOGS(INFO) << "Process Limbo is not active at startup.";
    return;
  }

  auto filters = fxl::JoinStrings(limbo.filters(), ", ");
  FX_LOGS(INFO) << "Process limbo is active at startup with the following filters: " << filters;
}

}  // namespace

int main() {
  syslog::InitLogger({"exception-broker"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  auto broker = fuchsia::exception::ExceptionBroker::Create(loop.dispatcher(), context->svc());
  if (!broker)
    return EXIT_FAILURE;

  // Create the bindings for the protocols.
  fidl::BindingSet<fuchsia::exception::Handler> handler_bindings;
  context->outgoing()->AddPublicService(handler_bindings.GetHandler(broker.get()));

  // Crete a new handler for each connection.
  fidl::BindingSet<ProcessLimbo, std::unique_ptr<ProcessLimboHandler>> limbo_bindings;
  auto& limbo_manager = broker->limbo_manager();

  // Everytime a new request comes for this service, we create a new handler. This permits us to
  // track per-connection state.
  context->outgoing()->AddPublicService(fidl::InterfaceRequestHandler<ProcessLimbo>(
      [&limbo_manager, &limbo_bindings](fidl::InterfaceRequest<ProcessLimbo> request) {
        // Create a new handler exclusive to this connection.
        auto handler = std::make_unique<ProcessLimboHandler>(limbo_manager.GetWeakPtr());

        // Track this handler in the limbo manager, so it can be notified about events.
        limbo_manager.AddHandler(handler->GetWeakPtr());

        // Add the handler to the bindings, which is where the fidl calls come through.
        limbo_bindings.AddBinding(std::move(handler), std::move(request));
      }));

  LogProcessLimboStatus(broker->limbo_manager());

  loop.Run();

  return EXIT_SUCCESS;
}
