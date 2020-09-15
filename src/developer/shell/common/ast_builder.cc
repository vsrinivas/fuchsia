// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/common/ast_builder.h"

#include <algorithm>

namespace shell::console {

using NodeId = AstBuilder::NodeId;

NodeId AstBuilder::AddNode(llcpp::fuchsia::shell::Node&& node, bool is_root) {
  llcpp::fuchsia::shell::NodeDefinition definition;
  definition.node = std::move(node);
  definition.root_node = is_root;
  llcpp::fuchsia::shell::NodeId id;
  id.file_id = file_id_;
  id.node_id = ++next_id_;
  definition.node_id = std::move(id);
  nodes_.push_back(std::move(definition));
  return id;
}

fidl::VectorView<llcpp::fuchsia::shell::Node> AstBuilder::NodesAsVectorView() {
  struct {
    bool operator()(const llcpp::fuchsia::shell::NodeDefinition& a,
                    const llcpp::fuchsia::shell::NodeDefinition& b) const {
      return a.node_id.node_id < b.node_id.node_id;
    }
  } cmp;
  std::sort(nodes_.begin(), nodes_.end(), cmp);
  for (auto& node_def : nodes_) {
    raw_nodes_.push_back(std::move(node_def.node));
  }
  nodes_.clear();
  return fidl::unowned_vec(raw_nodes_);
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

void AstBuilder::SetRoot(NodeId node_id) {
  for (size_t i = 0; i < nodes_.size(); i++) {
    if (nodes_[i].node_id.node_id == node_id.node_id &&
        nodes_[i].node_id.file_id == node_id.file_id) {
      nodes_[i].root_node = true;
      return;
    }
  }
}

NodeId AstBuilder::AddVariableDeclaration(const std::string& identifier,
                                          llcpp::fuchsia::shell::ShellType&& type, NodeId node_id,
                                          bool is_const, bool is_root) {
  auto def = ManageNew<llcpp::fuchsia::shell::VariableDefinition>();
  char* name_buf = ManageCopyOf(identifier.c_str(), identifier.size());
  def->name.set_data(fidl::unowned_ptr(name_buf));
  def->name.set_size(identifier.size());
  def->type = std::move(type);
  def->mutable_value = !is_const;
  def->initial_value = node_id;
  auto node = llcpp::fuchsia::shell::Node::WithVariableDefinition(fidl::unowned_ptr(def));
  return AddNode(std::move(node), is_root);
}

NodeId AstBuilder::AddVariable(const std::string& identifier) {
  auto name = ManageNew<fidl::StringView>();
  char* name_buf = ManageCopyOf(identifier.c_str(), identifier.size());
  name->set_data(fidl::unowned_ptr(name_buf));
  name->set_size(identifier.size());
  auto node = llcpp::fuchsia::shell::Node::WithVariable(fidl::unowned_ptr(name));
  return AddNode(std::move(node));
}

NodeId AstBuilder::AddIntegerLiteral(uint64_t i, bool is_negative) {
  auto literal = ManageNew<llcpp::fuchsia::shell::IntegerLiteral>();

  uint64_t* managed_val = ManageCopyOf(&i);

  literal->absolute_value = ::fidl::VectorView(fidl::unowned_ptr(managed_val), 1);
  literal->negative = is_negative;
  auto node = llcpp::fuchsia::shell::Node::WithIntegerLiteral(fidl::unowned_ptr(literal));
  return AddNode(std::move(node));
}

NodeId AstBuilder::AddIntegerLiteral(int64_t i) {
  auto literal = ManageNew<llcpp::fuchsia::shell::IntegerLiteral>();

  uint64_t val;
  if (i == LLONG_MIN) {
    val = static_cast<uint64_t>(LLONG_MAX) + 1;
  } else {
    val = llabs(i);
  }
  uint64_t* managed_val = ManageCopyOf(&val);

  literal->absolute_value = ::fidl::VectorView(fidl::unowned_ptr(managed_val), 1);
  literal->negative = (i < 0);
  auto node = llcpp::fuchsia::shell::Node::WithIntegerLiteral(fidl::unowned_ptr(literal));
  return AddNode(std::move(node));
}

NodeId AstBuilder::AddStringLiteral(const std::string& s) {
  auto literal = ManageNew<fidl::StringView>();

  char* managed_val = ManageCopyOf(s.data(), s.size());
  literal->set_data(fidl::unowned_ptr(managed_val));
  literal->set_size(s.size());
  auto node = llcpp::fuchsia::shell::Node::WithStringLiteral(fidl::unowned_ptr(literal));
  return AddNode(std::move(node));
}

void AstBuilder::AddEmitResult(NodeId expression) {
  auto emit_result = ManageNew<llcpp::fuchsia::shell::NodeId>();
  *emit_result = expression;
  auto node = llcpp::fuchsia::shell::Node::WithEmitResult(fidl::unowned_ptr(emit_result));
  AddNode(std::move(node), /*root_node=*/true);
}

NodeId AstBuilder::AddAssignment(NodeId destination, NodeId source) {
  auto assignment = ManageNew<llcpp::fuchsia::shell::Assignment>();
  assignment->destination = std::move(destination);
  assignment->source = std::move(source);

  auto node = llcpp::fuchsia::shell::Node::WithAssignment(fidl::unowned_ptr(assignment));

  return AddNode(std::move(node), /*root_node=*/true);
}

NodeId AstBuilder::AddAddition(bool with_exceptions, NodeId left_id, NodeId right_id) {
  auto addition = ManageNew<llcpp::fuchsia::shell::Addition>();
  addition->left = std::move(left_id);
  addition->right = std::move(right_id);
  addition->with_exceptions = with_exceptions;

  auto node = llcpp::fuchsia::shell::Node::WithAddition(fidl::unowned_ptr(addition));

  return AddNode(std::move(node), /*root_node=*/false);
}

void AstBuilder::OpenObject() { object_stack_.emplace_back(); }

AstBuilder::NodePair AstBuilder::CloseObject() {
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

  object_schema->fields = ::fidl::VectorView<llcpp::fuchsia::shell::NodeId>(
      fidl::unowned_ptr(schema_mem), fields.size());
  auto schema_node =
      llcpp::fuchsia::shell::Node::WithObjectSchema(fidl::unowned_ptr(object_schema));
  // We construct an unnamed schema => local schema (only used by one object).
  NodeId schema_node_id = AddNode(std::move(schema_node), /*root_node=*/false);

  object->fields = ::fidl::VectorView<llcpp::fuchsia::shell::NodeId>(fidl::unowned_ptr(value_mem),
                                                                     fields.size());
  auto value_node = llcpp::fuchsia::shell::Node::WithObject(fidl::unowned_ptr(object));
  NodeId value_node_id = AddNode(std::move(value_node));

  object->object_schema = schema_node_id;

  object_stack_.pop_back();
  NodePair result;
  result.schema_node = schema_node_id;
  result.value_node = value_node_id;
  return result;
}

AstBuilder::NodePair AstBuilder::AddField(const std::string& key, NodeId expression_node_id,
                                          llcpp::fuchsia::shell::ShellType&& type) {
  NodePair result;

  // Create the type.
  auto field_schema = ManageNew<llcpp::fuchsia::shell::ObjectFieldSchemaDefinition>();
  char* name_buf = ManageCopyOf(key.c_str(), key.size());
  field_schema->name.set_data(fidl::unowned_ptr(name_buf));
  field_schema->name.set_size(key.size());
  field_schema->type = std::move(type);
  auto node = llcpp::fuchsia::shell::Node::WithFieldSchema(fidl::unowned_ptr(field_schema));
  llcpp::fuchsia::shell::NodeId type_id;
  result.schema_node = type_id = AddNode(std::move(node));

  // Create the object
  auto field = ManageNew<llcpp::fuchsia::shell::ObjectFieldDefinition>();
  field->object_field_schema.file_id = file_id_;
  field->object_field_schema.node_id = type_id.node_id;
  field->value = expression_node_id;
  llcpp::fuchsia::shell::NodeId value_id;
  auto value_node = llcpp::fuchsia::shell::Node::WithObjectField(fidl::unowned_ptr(field));
  result.value_node = value_id = AddNode(std::move(value_node));

  std::vector<FidlNodeIdPair>& fields = object_stack_.back();
  fields.emplace_back(std::move(type_id), std::move(value_id));

  return result;
}

}  // namespace shell::console
