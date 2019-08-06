// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/puppet_master/command_runners/operation_calls/get_types_from_entity_call.h"

#include <lib/fsl/types/type_converters.h>

namespace modular {

namespace {

class GetTypesFromEntityCall : public Operation<std::vector<std::string>> {
 public:
  GetTypesFromEntityCall(fuchsia::modular::EntityResolver* const entity_resolver,
                         const fidl::StringPtr& entity_reference, ResultCall result)
      : Operation("GetTypesFromEntityCall", std::move(result)),
        entity_resolver_(entity_resolver),
        entity_reference_(entity_reference) {}

 private:
  void Run() override {
    entity_resolver_->ResolveEntity(entity_reference_.value_or(""), entity_.NewRequest());
    entity_->GetTypes([this](const std::vector<std::string>& types) { Done(types); });
  }

  fuchsia::modular::EntityResolver* const entity_resolver_;
  fidl::StringPtr const entity_reference_;
  fuchsia::modular::EntityPtr entity_;
};

}  // namespace

void AddGetTypesFromEntityOperation(OperationContainer* const operation_container,
                                    fuchsia::modular::EntityResolver* const entity_resolver,
                                    const fidl::StringPtr& entity_reference,
                                    fit::function<void(std::vector<std::string>)> result_call) {
  operation_container->Add(std::make_unique<GetTypesFromEntityCall>(
      entity_resolver, entity_reference, std::move(result_call)));
}

}  // namespace modular
