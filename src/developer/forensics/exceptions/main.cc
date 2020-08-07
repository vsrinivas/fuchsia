// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/fidl/cpp/binding_set.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/exceptions/constants.h"
#include "src/developer/forensics/exceptions/exception_broker.h"
#include "src/developer/forensics/utils/component/component.h"
#include "src/lib/fxl/strings/join_strings.h"

namespace {

void LogProcessLimboStatus(const ::forensics::exceptions::ProcessLimboManager& limbo) {
  if (!limbo.active()) {
    return;
  }

  auto filters = fxl::JoinStrings(limbo.filters(), ", ");
  FX_LOGS(INFO) << "Process limbo is active at startup with the following filters: " << filters;
}

}  // namespace

int main() {
  using namespace ::forensics::exceptions;

  syslog::SetTags({"forensics", "exception"});

  forensics::component::Component component;

  auto broker =
      ExceptionBroker::Create(component.Dispatcher(), kMaxNumExceptionHandlers, kExceptionTtl);
  if (!broker)
    return EXIT_FAILURE;

  // Create the bindings for the protocols.
  fidl::BindingSet<fuchsia::exception::Handler> handler_bindings;
  component.AddPublicService(handler_bindings.GetHandler(broker.get()));

  // Crete a new handler for each connection.
  fidl::BindingSet<fuchsia::exception::ProcessLimbo, std::unique_ptr<ProcessLimboHandler>>
      limbo_bindings;
  auto& limbo_manager = broker->limbo_manager();

  // Everytime a new request comes for this service, we create a new handler. This permits us to
  // track per-connection state.
  component.AddPublicService(fidl::InterfaceRequestHandler<fuchsia::exception::ProcessLimbo>(
      [&limbo_manager,
       &limbo_bindings](fidl::InterfaceRequest<fuchsia::exception::ProcessLimbo> request) {
        // Create a new handler exclusive to this connection.
        auto handler = std::make_unique<ProcessLimboHandler>(limbo_manager.GetWeakPtr());

        // Track this handler in the limbo manager, so it can be notified about events.
        limbo_manager.AddHandler(handler->GetWeakPtr());

        // Add the handler to the bindings, which is where the fidl calls come through.
        limbo_bindings.AddBinding(std::move(handler), std::move(request));
      }));

  LogProcessLimboStatus(broker->limbo_manager());

  component.RunLoop();

  return EXIT_SUCCESS;
}
