// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/testing/component_context_fake.h"

#include "lib/fxl/logging.h"

namespace modular {

ComponentContextFake::ComponentContextFake() {}

ComponentContextFake::~ComponentContextFake() = default;

void ComponentContextFake::Connect(
    fidl::InterfaceRequest<ComponentContext> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ComponentContextFake::GetLedger(
    fidl::InterfaceRequest<ledger::Ledger> request,
    const GetLedgerCallback& result) {
  FXL_NOTIMPLEMENTED();
}

void ComponentContextFake::ConnectToAgent(
    const fidl::String& url,
    fidl::InterfaceRequest<app::ServiceProvider> incoming_services_request,
    fidl::InterfaceRequest<AgentController> agent_controller_request) {
  FXL_NOTIMPLEMENTED();
}

void ComponentContextFake::ObtainMessageQueue(
    const fidl::String& name,
    fidl::InterfaceRequest<MessageQueue> request) {
  FXL_NOTIMPLEMENTED();
}

void ComponentContextFake::DeleteMessageQueue(const fidl::String& name) {
  FXL_NOTIMPLEMENTED();
}

void ComponentContextFake::GetMessageSender(
    const fidl::String& queue_token,
    fidl::InterfaceRequest<MessageSender> request) {
  FXL_NOTIMPLEMENTED();
}

void ComponentContextFake::GetEntityResolver(
    fidl::InterfaceRequest<EntityResolver> request) {
  entity_resolver_.Connect(std::move(request));
}

void ComponentContextFake::CreateEntityWithData(
    fidl::Map<fidl::String, fidl::String> type_to_data,
    const CreateEntityWithDataCallback& result) {
  FXL_NOTIMPLEMENTED();
}

}  // namespace modular
