// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_COMPONENT_CONTEXT_FAKE_H_
#define PERIDOT_LIB_TESTING_COMPONENT_CONTEXT_FAKE_H_

#include <string>

#include "lib/component/fidl/component_context.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fxl/macros.h"
#include "peridot/lib/testing/entity_resolver_fake.h"

namespace modular {

// A fake implementation of ComponentContext for tests. ComponentContext gives
// clients access to further services. This implementation returns fake
// versions of the various services in question.
//
// Implemented:
//
//  * GetEntityResolver() -> returns a FakeEntityResolver (see
//    lib/entity/cpp/testing/fake_entity_resolver.h).
//  * CreateEntityWithData() -> returns a reference that the FakeEntityResolver
//    will resolve.
class ComponentContextFake : public ComponentContext {
 public:
  ComponentContextFake();
  ~ComponentContextFake() override;

  void Connect(f1dl::InterfaceRequest<ComponentContext> request);

  EntityResolverFake& entity_resolver_fake() { return entity_resolver_; }

 private:
  // |ComponentContext|
  void GetLedger(f1dl::InterfaceRequest<ledger::Ledger> request,
                 const GetLedgerCallback& result) override;

  // |ComponentContext|
  void ConnectToAgent(const f1dl::String& url,
                      f1dl::InterfaceRequest<component::ServiceProvider>
                          incoming_services_request,
                      f1dl::InterfaceRequest<AgentController>
                          agent_controller_request) override;

  // |ComponentContext|
  void ObtainMessageQueue(
      const f1dl::String& name,
      f1dl::InterfaceRequest<MessageQueue> request) override;

  // |ComponentContext|
  void DeleteMessageQueue(const f1dl::String& name) override;

  // |ComponentContext|
  void GetMessageSender(const f1dl::String& queue_token,
                        f1dl::InterfaceRequest<MessageSender> request) override;

  // |ComponentContext|
  void GetEntityResolver(
      f1dl::InterfaceRequest<EntityResolver> request) override;

  // |ComponentContext|
  void CreateEntityWithData(
      f1dl::Array<TypeToDataEntryPtr> type_to_data,
      const CreateEntityWithDataCallback& result) override;

  EntityResolverFake entity_resolver_;

  f1dl::BindingSet<modular::ComponentContext> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ComponentContextFake);
};

}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_COMPONENT_CONTEXT_FAKE_H_
