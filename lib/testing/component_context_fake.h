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

  void Connect(fidl::InterfaceRequest<ComponentContext> request);

  EntityResolverFake& entity_resolver_fake() { return entity_resolver_; }

 private:
  // |ComponentContext|
  void GetLedger(fidl::InterfaceRequest<ledger::Ledger> request,
                 const GetLedgerCallback& result) override;

  // |ComponentContext|
  void ConnectToAgent(
      const fidl::String& url,
      fidl::InterfaceRequest<app::ServiceProvider> incoming_services_request,
      fidl::InterfaceRequest<AgentController> agent_controller_request)
      override;

  // |ComponentContext|
  void ObtainMessageQueue(
      const fidl::String& name,
      fidl::InterfaceRequest<MessageQueue> request) override;

  // |ComponentContext|
  void DeleteMessageQueue(const fidl::String& name) override;

  // |ComponentContext|
  void GetMessageSender(const fidl::String& queue_token,
                        fidl::InterfaceRequest<MessageSender> request) override;

  // |ComponentContext|
  void GetEntityResolver(
      fidl::InterfaceRequest<EntityResolver> request) override;

  // |ComponentContext|
  void CreateEntityWithData(
      fidl::Map<fidl::String, fidl::String> type_to_data,
      const CreateEntityWithDataCallback& result) override;

  EntityResolverFake entity_resolver_;

  fidl::BindingSet<modular::ComponentContext> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ComponentContextFake);
};

}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_COMPONENT_CONTEXT_FAKE_H_
