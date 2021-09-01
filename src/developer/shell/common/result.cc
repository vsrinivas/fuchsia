// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/common/result.h"

#include <lib/syslog/cpp/macros.h>

#include <ostream>

#include "fidl/fuchsia.shell/cpp/wire.h"

namespace shell::common {

std::unique_ptr<ResultNode> DeserializeResult::Deserialize(
    const fidl::VectorView<fuchsia_shell::wire::Node>& nodes) {
  FX_DCHECK(!nodes.empty());
  return DeserializeNode(nodes, nodes.count());
}

std::unique_ptr<ResultNode> DeserializeResult::DeserializeNode(
    const fidl::VectorView<fuchsia_shell::wire::Node>& nodes, uint64_t node_id) {
  FX_DCHECK(node_id <= nodes.count());
  const fuchsia_shell::wire::Node& node = nodes[node_id - 1];
  if (node.is_integer_literal()) {
    return std::make_unique<ResultNodeIntegerLiteral>(node.integer_literal().absolute_value,
                                                      node.integer_literal().negative);
  }
  if (node.is_string_literal()) {
    return std::make_unique<ResultNodeStringLiteral>(node.string_literal());
  }
  if (node.is_object()) {
    auto schema = DeserializeSchema(nodes, node.object().object_schema.node_id);
    if (schema != nullptr) {
      auto result = std::make_unique<ResultNodeObject>(schema);
      for (const auto& field_id : node.object().fields) {
        FX_DCHECK(field_id.file_id == 1U);
        FX_DCHECK(field_id.node_id <= nodes.count());
        const fuchsia_shell::wire::Node& field_node = nodes[field_id.node_id - 1];
        FX_DCHECK(field_node.is_object_field());
        FX_DCHECK(field_node.object_field().object_field_schema.file_id == 1U);
        auto field_schema =
            schema->SearchField(field_node.object_field().object_field_schema.node_id);
        FX_DCHECK(field_schema != nullptr);
        auto field_value = DeserializeNode(nodes, field_node.object_field().value.node_id);
        if (field_value != nullptr) {
          result->AddField(field_schema, std::move(field_value));
        }
      }
      return result;
    }
  }
  return nullptr;
}

std::shared_ptr<ResultSchema> DeserializeResult::DeserializeSchema(
    const fidl::VectorView<fuchsia_shell::wire::Node>& nodes, uint64_t node_id) {
  if (node_id == 0) {
    return nullptr;
  }
  auto schema = schemas_.find(node_id);
  if (schema != schemas_.end()) {
    return schema->second;
  }
  FX_DCHECK(node_id <= nodes.count());
  const fuchsia_shell::wire::Node& schema_node = nodes[node_id - 1];
  if (!schema_node.is_object_schema()) {
    return nullptr;
  }
  auto result = std::make_shared<ResultSchema>();
  schemas_.emplace(std::pair(node_id, result));
  for (const auto& field : schema_node.object_schema().fields) {
    FX_DCHECK(field.file_id == 1U);
    uint64_t field_id = field.node_id;
    FX_DCHECK(field_id <= nodes.count());
    const fuchsia_shell::wire::Node& schema_field_node = nodes[field_id - 1];
    if (schema_field_node.is_field_schema()) {
      result->AddField(field_id, schema_field_node.field_schema().name,
                       DeserializeType(nodes, schema_field_node.field_schema().type));
    }
  }
  return result;
}

std::unique_ptr<ResultType> DeserializeResult::DeserializeType(
    const fidl::VectorView<fuchsia_shell::wire::Node>& nodes,
    const fuchsia_shell::wire::ShellType& shell_type) {
  if (shell_type.is_builtin_type()) {
    switch (shell_type.builtin_type()) {
      case fuchsia_shell::wire::BuiltinType::kUint64:
        return std::make_unique<ResultTypeUint64>();
      case fuchsia_shell::wire::BuiltinType::kString:
        return std::make_unique<ResultTypeString>();
      default:
        return nullptr;
    }
  }
  return nullptr;
}

}  // namespace shell::common
