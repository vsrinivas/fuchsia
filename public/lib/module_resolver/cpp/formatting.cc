// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fsl/vmo/strings.h>
#include <lib/module_resolver/cpp/formatting.h>

namespace modular {

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::modular::Intent& intent) {
  os << "{ action: " << intent.action << ", parameters: [" << std::endl;
  for (auto it = intent.parameters->begin(); it != intent.parameters->end();
       ++it) {
    os << "    " << it->name << ": " << it->data << "," << std::endl;
  }
  os << "  ] }";
  return os;
}

std::ostream& operator<<(
    std::ostream& os,
    const fuchsia::modular::IntentParameterData& parameter_data) {
  if (parameter_data.is_json()) {
    std::string json;
    FXL_CHECK(fsl::StringFromVmo(parameter_data.json(), &json));
    os << json;
  } else if (parameter_data.is_entity_reference()) {
    os << "[ref: " << parameter_data.entity_reference() << "]";
  } else if (parameter_data.is_entity_type()) {
    for (const auto& type : *parameter_data.entity_type()) {
      os << type << ", ";
    }
  }
  return os;
}

}  // namespace modular
