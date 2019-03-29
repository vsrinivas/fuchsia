// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/testing/component_context_fake.h"

#include <src/lib/fxl/logging.h>

namespace modular {

ComponentContextFake::ComponentContextFake() {}

ComponentContextFake::~ComponentContextFake() = default;

void ComponentContextFake::Connect(
    fidl::InterfaceRequest<fuchsia::modular::ComponentContext> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ComponentContextFake::GetLedger(
    fidl::InterfaceRequest<fuchsia::ledger::Ledger> request) {
  FXL_NOTIMPLEMENTED();
}

void ComponentContextFake::ConnectToAgent(
    std::string url,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>
        incoming_services_request,
    fidl::InterfaceRequest<fuchsia::modular::AgentController>
        agent_controller_request) {
  FXL_NOTIMPLEMENTED();
}

void ComponentContextFake::ObtainMessageQueue(
    std::string name,
    fidl::InterfaceRequest<fuchsia::modular::MessageQueue> request) {
  FXL_NOTIMPLEMENTED();
}

void ComponentContextFake::DeleteMessageQueue(std::string name) {
  FXL_NOTIMPLEMENTED();
}

void ComponentContextFake::GetMessageSender(
    std::string queue_token,
    fidl::InterfaceRequest<fuchsia::modular::MessageSender> request) {
  FXL_NOTIMPLEMENTED();
}

void ComponentContextFake::GetEntityResolver(
    fidl::InterfaceRequest<fuchsia::modular::EntityResolver> request) {
  entity_resolver_.Connect(std::move(request));
}

void ComponentContextFake::CreateEntityWithData(
    std::vector<fuchsia::modular::TypeToDataEntry> type_to_data,
    CreateEntityWithDataCallback result) {
  FXL_NOTIMPLEMENTED();
}

void ComponentContextFake::GetPackageName(GetPackageNameCallback result) {
  FXL_NOTIMPLEMENTED();
}

}  // namespace modular
