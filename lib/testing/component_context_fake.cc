// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/testing/component_context_fake.h"

#include "lib/fxl/logging.h"

namespace modular {

ComponentContextFake::ComponentContextFake() {}

ComponentContextFake::~ComponentContextFake() = default;

void ComponentContextFake::Connect(
    f1dl::InterfaceRequest<ComponentContext> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ComponentContextFake::GetLedger(
    f1dl::InterfaceRequest<ledger::Ledger> request,
    const GetLedgerCallback& result) {
  FXL_NOTIMPLEMENTED();
}

void ComponentContextFake::ConnectToAgent(
    const f1dl::String& url,
    f1dl::InterfaceRequest<app::ServiceProvider> incoming_services_request,
    f1dl::InterfaceRequest<AgentController> agent_controller_request) {
  FXL_NOTIMPLEMENTED();
}

void ComponentContextFake::ObtainMessageQueue(
    const f1dl::String& name,
    f1dl::InterfaceRequest<MessageQueue> request) {
  FXL_NOTIMPLEMENTED();
}

void ComponentContextFake::DeleteMessageQueue(const f1dl::String& name) {
  FXL_NOTIMPLEMENTED();
}

void ComponentContextFake::GetMessageSender(
    const f1dl::String& queue_token,
    f1dl::InterfaceRequest<MessageSender> request) {
  FXL_NOTIMPLEMENTED();
}

void ComponentContextFake::GetEntityResolver(
    f1dl::InterfaceRequest<EntityResolver> request) {
  entity_resolver_.Connect(std::move(request));
}

void ComponentContextFake::CreateEntityWithData(
    f1dl::Array<TypeToDataEntryPtr> type_to_data,
    const CreateEntityWithDataCallback& result) {
  FXL_NOTIMPLEMENTED();
}

}  // namespace modular
