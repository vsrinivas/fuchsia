// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/console/ast_builder.h"

namespace shell::console {

uint64_t AstBuilder::AddNode(llcpp::fuchsia::shell::Node&& node) {
  llcpp::fuchsia::shell::NodeDefinition definition;
  definition.node = std::move(node);
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

}  // namespace shell::console
