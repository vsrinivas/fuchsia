// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_TESTING_COMPONENT_CONTEXT_FAKE_H_
#define SRC_MODULAR_LIB_TESTING_COMPONENT_CONTEXT_FAKE_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/string.h>

#include <string>

#include <src/lib/fxl/macros.h>

#include "src/modular/lib/testing/entity_resolver_fake.h"

namespace modular {

// A fake implementation of fuchsia::modular::ComponentContext for tests.
// fuchsia::modular::ComponentContext gives clients access to further services.
// This implementation returns fake versions of the various services in
// question.
//
// Implemented:
//
//  * GetEntityResolver() -> returns a FakeEntityResolver (see
//    lib/entity/cpp/testing/fake_entity_resolver.h).
class ComponentContextFake : public fuchsia::modular::ComponentContext {
 public:
  ComponentContextFake();
  ~ComponentContextFake() override;

  void Connect(fidl::InterfaceRequest<fuchsia::modular::ComponentContext> request);

  EntityResolverFake& entity_resolver_fake() { return entity_resolver_; }

 private:
  // |fuchsia::modular::ComponentContext|
  void GetLedger(fidl::InterfaceRequest<fuchsia::ledger::Ledger> request) override;

  // |fuchsia::modular::ComponentContext|
  void ConnectToAgent(
      std::string url,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services_request,
      fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request) override;

  // |fuchsia::modular::ComponentContext|
  void GetEntityResolver(fidl::InterfaceRequest<fuchsia::modular::EntityResolver> request) override;

  EntityResolverFake entity_resolver_;

  fidl::BindingSet<fuchsia::modular::ComponentContext> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ComponentContextFake);
};

}  // namespace modular

#endif  // SRC_MODULAR_LIB_TESTING_COMPONENT_CONTEXT_FAKE_H_
