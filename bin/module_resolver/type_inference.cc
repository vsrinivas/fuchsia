// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include "peridot/bin/module_resolver/type_inference.h"

#include "peridot/public/lib/entity/cpp/json.h"

namespace maxwell {

NounTypeInferenceHelper::NounTypeInferenceHelper() {}

std::vector<std::string> NounTypeInferenceHelper::GetEntityTypes(
    const modular::NounPtr& noun) {
  if (noun->is_entity_type()) {
    return std::vector<std::string>(noun->get_entity_type().begin(),
                                    noun->get_entity_type().end());
  } else if (noun->is_json()) {
    std::vector<std::string> types;
    if (!modular::ExtractEntityTypesFromJson(noun->get_json(), &types)) {
      FXL_LOG(WARNING) << "Mal-formed JSON in noun: " << noun->get_json();
      return {};
    }
    return types;
  }
  // TODO(thatguy): Add support for other methods of getting Entity types.
  return {};
}

}  // namespace maxwell
