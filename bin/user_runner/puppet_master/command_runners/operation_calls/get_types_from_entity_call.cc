// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/operation_calls/get_types_from_entity_call.h"

#include <lib/fsl/types/type_converters.h>
#include <lib/fxl/type_converter.h>

namespace modular {

GetTypesFromEntityCall::GetTypesFromEntityCall(
    fuchsia::modular::EntityResolver* const entity_resolver,
    const fidl::StringPtr& entity_reference, ResultCall result)
    : Operation("GetTypesFromEntityCall", std::move(result)),
      entity_resolver_(entity_resolver),
      entity_reference_(entity_reference) {}

void GetTypesFromEntityCall::Run() {
  entity_resolver_->ResolveEntity(entity_reference_, entity_.NewRequest());
  entity_->GetTypes([this](const fidl::VectorPtr<fidl::StringPtr>& types) {
    Done(fxl::To<std::vector<std::string>>(types));
  });
}

}  // namespace modular
