// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.inspect/cpp/wire.h>
#include <lib/inspect/component/cpp/component.h>
#include <lib/inspect/component/cpp/service.h>

namespace inspect {
ComponentInspector::ComponentInspector(component::OutgoingDirectory& out,
                                       async_dispatcher_t* dispatcher, Inspector inspector,
                                       TreeHandlerSettings settings)
    : inspector_(inspector) {
  auto status = out.AddProtocolAt<fuchsia_inspect::Tree>(
      "diagnostics", [dispatcher, inspector = std::move(inspector), settings = std::move(settings)](
                         fidl::ServerEnd<fuchsia_inspect::Tree> server_end) {
        TreeServer::StartSelfManagedServer(std::move(inspector), std::move(settings), dispatcher,
                                           std::move(server_end));
      });

  ZX_ASSERT(status.is_ok());
}

NodeHealth& ComponentInspector::Health() {
  if (!component_health_) {
    component_health_ = std::make_unique<NodeHealth>(&inspector()->GetRoot());
  }
  return *component_health_;
}

}  // namespace inspect
