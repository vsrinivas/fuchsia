// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/console/ast_builder.h"

namespace shell::console {

uint64_t AstBuilder::AddNode(llcpp::fuchsia::shell::Node&& node, bool is_root) {
  llcpp::fuchsia::shell::NodeDefinition definition;
  definition.node = std::move(node);
  definition.root_node = is_root;
  llcpp::fuchsia::shell::NodeId id;
  id.file_id = 0;
  id.node_id = ++next_id_;
  definition.node_id = std::move(id);
  nodes_.push_back(std::move(definition));
  return id.node_id;
}

AstBuilder& AstBuilder::operator=(AstBuilder&& other) {
  this->next_id_ = other.next_id_;
  this->bufs_ = std::move(other.bufs_);
  other.bufs_.clear();
  this->nodes_ = std::move(other.nodes_);
  return *this;
}

AstBuilder::AstBuilder(AstBuilder&& other) {
  this->next_id_ = other.next_id_;
  this->bufs_ = std::move(other.bufs_);
  other.bufs_.clear();
  this->nodes_ = std::move(other.nodes_);
}

void AstBuilder::SetRoot(uint64_t node_id) {
  for (size_t i = 0; i < nodes_.size(); i++) {
    if (nodes_[i].node_id.node_id == node_id) {
      nodes_[i].root_node = true;
      return;
    }
  }
}

uint64_t AstBuilder::AddVariableDeclaration(const std::string& identifier,
                                            llcpp::fuchsia::shell::ShellType&& type,
                                            uint64_t node_id, bool is_const) {
  auto def = ManageNew<llcpp::fuchsia::shell::VariableDefinition>();
  char* name_buf = ManageCopyOf(identifier.c_str(), identifier.size());
  def->name.set_data(name_buf);
  def->name.set_size(identifier.size());
  def->type = std::move(type);
  def->mutable_value = !is_const;
  def->initial_value.node_id = node_id;
  def->initial_value.file_id = 0;  // Need to do something useful here.
  auto node = llcpp::fuchsia::shell::Node::WithVariableDefinition(fidl::unowned(def));
  return AddNode(std::move(node));
}

uint64_t AstBuilder::AddIntegerLiteral(int64_t i) {
  auto literal = ManageNew<llcpp::fuchsia::shell::IntegerLiteral>();

  uint64_t val;
  if (i == LLONG_MIN) {
    val = static_cast<uint64_t>(LLONG_MAX) + 1;
  } else {
    val = llabs(i);
  }
  uint64_t* managed_val = ManageCopyOf(&val);

  literal->absolute_value = ::fidl::VectorView<uint64_t>(managed_val, 1);
  literal->negative = (i < 0);
  auto node = llcpp::fuchsia::shell::Node::WithIntegerLiteral(fidl::unowned(literal));
  uint64_t node_id = AddNode(std::move(node));

  return node_id;
}

void AstBuilder::OpenObject() { object_stack_.emplace_back(); }

AstBuilder::NodePair AstBuilder::CloseObject(uint64_t file_id) {
  auto object_schema = ManageNew<llcpp::fuchsia::shell::ObjectSchemaDefinition>();
  auto object = ManageNew<llcpp::fuchsia::shell::ObjectDefinition>();

  // Create a vector of nodes for the fields.
  std::vector<FidlNodeIdPair>& fields = object_stack_.back();
  auto schema_mem = ManageNewArray<llcpp::fuchsia::shell::NodeId>(fields.size());
  auto value_mem = ManageNewArray<llcpp::fuchsia::shell::NodeId>(fields.size());
  for (size_t i = 0; i < fields.size(); i++) {
    schema_mem[i] = std::move(fields[i].schema_id);
    value_mem[i] = std::move(fields[i].value_id);
  }

  object_schema->fields =
      ::fidl::VectorView<llcpp::fuchsia::shell::NodeId>(schema_mem, fields.size());
  auto schema_node = llcpp::fuchsia::shell::Node::WithObjectSchema(fidl::unowned(object_schema));
  uint64_t schema_node_id = AddNode(std::move(schema_node), true);

  object->fields = ::fidl::VectorView<llcpp::fuchsia::shell::NodeId>(value_mem, fields.size());
  auto value_node = llcpp::fuchsia::shell::Node::WithObject(fidl::unowned(object));
  uint64_t value_node_id = AddNode(std::move(value_node));

  object->object_schema.file_id = file_id;
  object->object_schema.node_id = schema_node_id;

  object_stack_.pop_back();
  NodePair result;
  result.schema_node = schema_node_id;
  result.value_node = value_node_id;
  return result;
}

AstBuilder::NodePair AstBuilder::AddField(const std::string& key, uint64_t file_id,
                                          uint64_t expression_node_id,
                                          llcpp::fuchsia::shell::ShellType&& type) {
  NodePair result;

  // Create the type.
  auto field_schema = ManageNew<llcpp::fuchsia::shell::ObjectFieldSchemaDefinition>();
  char* name_buf = ManageCopyOf(key.c_str(), key.size());
  field_schema->name.set_data(name_buf);
  field_schema->name.set_size(key.size());
  field_schema->type = std::move(type);
  auto node = llcpp::fuchsia::shell::Node::WithFieldSchema(fidl::unowned(field_schema));
  llcpp::fuchsia::shell::NodeId type_id;
  type_id.file_id = file_id;
  result.schema_node = type_id.node_id = AddNode(std::move(node));

  // Create the object
  auto field = ManageNew<llcpp::fuchsia::shell::ObjectFieldDefinition>();
  field->object_field_schema.file_id = file_id;
  field->object_field_schema.node_id = type_id.node_id;
  field->value.file_id = file_id;
  field->value.node_id = expression_node_id;
  llcpp::fuchsia::shell::NodeId value_id;
  value_id.file_id = file_id;
  auto value_node = llcpp::fuchsia::shell::Node::WithObjectField(fidl::unowned(field));
  result.value_node = value_id.node_id = AddNode(std::move(value_node));

  std::vector<FidlNodeIdPair>& fields = object_stack_.back();
  fields.emplace_back(std::move(type_id), std::move(value_id));

  return result;
}

}  // namespace shell::console
