// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_CONSOLE_AST_BUILDER_H_
#define SRC_DEVELOPER_SHELL_CONSOLE_AST_BUILDER_H_

#include <climits>
#include <string>
#include <vector>

#include "fuchsia/shell/llcpp/fidl.h"
#include "src/developer/shell/common/err.h"

namespace shell::console {

// Builds the remote AST for consumption by the interpreter service.
//
// Node ids start at 1, because node id 0 is reserved for null.
class AstBuilder {
 public:
  using NodeId = llcpp::fuchsia::shell::NodeId;

  // Constructs an AstBuilder. |file_id| is the id of the file (1 by default, because 0 means
  // "builtin")
  explicit AstBuilder(uint64_t file_id = 1) : file_id_(file_id), undef_(true), next_id_(0) {}

  AstBuilder& operator=(AstBuilder&&);
  AstBuilder(AstBuilder&&);

  // The undefined type.  Useful when your node isn't typed.
  llcpp::fuchsia::shell::ShellType undef() {
    return llcpp::fuchsia::shell::ShellType::WithUndef(fidl::unowned_ptr(&undef_));
  }

  // Returns the set of nodes managed by this AstBuilder as a vector view, suitable for sending to
  // Shell::AddNodes.
  fidl::VectorView<llcpp::fuchsia::shell::NodeDefinition> DefsAsVectorView() {
    return fidl::unowned_vec(nodes_);
  }

  // Returns the set of nodes managed by this AstBuilder as a vector view.
  // The node id is assumed to be the individual index.
  // Caution: DefsAsVectorView will not return anything after this method is called.
  fidl::VectorView<llcpp::fuchsia::shell::Node> NodesAsVectorView();

  bool empty() const { return nodes_.empty(); }

  template <class T>
  T* ManageCopyOf(const T* value, size_t size = sizeof(T)) {
    auto buf = bufs_.emplace_back(new char[size]).get();
    memcpy(buf, value, size);
    return new (buf) T(*value);
  }

  template <class T>
  T* ManageNew() {
    auto buf = bufs_.emplace_back(new char[sizeof(T)]).get();
    return new (buf) T();
  }

  template <class T>
  T* ManageNewArray(size_t size) {
    auto buf = bufs_.emplace_back(new char[sizeof(T) * size]).get();
    return new (buf) T[size]();
  }

  // Sets the given node to be the root node for remote computation.
  void SetRoot(NodeId node_id);

  // Adds a variable declaration.  The variable is named with the given |identifier|, the type is
  // the given |type|, the |node_id| refers to the node that, when evaluated, gives the initial
  // value, and |is_const| tells you whether the variable is const.  Returns the resulting node_id.
  NodeId AddVariableDeclaration(const std::string& identifier,
                                llcpp::fuchsia::shell::ShellType&& type, NodeId node_id,
                                bool is_const, bool is_root = false);

  // Adds a variable reference, where |node_id| was the variable declaration.
  NodeId AddVariable(const std::string& identifier);

  // Adds an integer literal node with the value |i|.  Returns the resulting node_id.
  NodeId AddIntegerLiteral(uint64_t i, bool is_negative);

  // Adds an integer literal node with the value |i|.  Returns the resulting node_id.
  NodeId AddIntegerLiteral(int64_t i);

  // Adds a string literal node with the value |s|.  Returns the resulting node_id.
  NodeId AddStringLiteral(const std::string& s);

  // Adds the emition of an expression.
  void AddEmitResult(NodeId expression);

  // Adds an assignment of two values.
  NodeId AddAssignment(NodeId destination, NodeId source);

  // Adds an addition of two values.
  NodeId AddAddition(bool with_exceptions, NodeId left, NodeId right);

  struct NodePair {
    NodeId value_node;
    NodeId schema_node;
  };

  // Call OpenObject when you start parsing an object, and CloseObject when you finish.
  // The resulting NodePair will contain nodes with its schema and value.
  void OpenObject();
  NodePair CloseObject();

  // Adds a field node
  NodePair AddField(const std::string& key, NodeId expression_node_id,
                    llcpp::fuchsia::shell::ShellType&& type);

  AstBuilder& operator=(const AstBuilder&) = delete;
  AstBuilder(const AstBuilder&) = delete;

  // Returns the added node_id
  NodeId AddNode(llcpp::fuchsia::shell::Node&& node, bool is_root = false);

  // Returns a pointer to a node that has previously been added.  For testing.
  llcpp::fuchsia::shell::Node* at(llcpp::fuchsia::shell::NodeId& id) {
    for (auto& def : nodes_) {
      if (def.node_id.node_id == id.node_id && def.node_id.file_id == id.file_id) {
        return &def.node;
      }
    }
    return nullptr;
  }

  // The following methods generate a ShellType object for the given type.
  llcpp::fuchsia::shell::ShellType TypeUndef() {
    fidl::aligned<bool> undef = false;
    fidl::aligned<bool>* undef_ptr = ManageCopyOf(&undef);
    return llcpp::fuchsia::shell::ShellType::WithUndef(fidl::unowned_ptr(undef_ptr));
  }

  llcpp::fuchsia::shell::ShellType TypeBool() {
    return TypeBuiltin(llcpp::fuchsia::shell::BuiltinType::BOOL);
  }

  llcpp::fuchsia::shell::ShellType TypeChar() {
    return TypeBuiltin(llcpp::fuchsia::shell::BuiltinType::CHAR);
  }

  llcpp::fuchsia::shell::ShellType TypeString() {
    return TypeBuiltin(llcpp::fuchsia::shell::BuiltinType::STRING);
  }

  llcpp::fuchsia::shell::ShellType TypeInt8() {
    return TypeBuiltin(llcpp::fuchsia::shell::BuiltinType::INT8);
  }

  llcpp::fuchsia::shell::ShellType TypeUint8() {
    return TypeBuiltin(llcpp::fuchsia::shell::BuiltinType::UINT8);
  }

  llcpp::fuchsia::shell::ShellType TypeInt16() {
    return TypeBuiltin(llcpp::fuchsia::shell::BuiltinType::INT16);
  }

  llcpp::fuchsia::shell::ShellType TypeUint16() {
    return TypeBuiltin(llcpp::fuchsia::shell::BuiltinType::UINT16);
  }

  llcpp::fuchsia::shell::ShellType TypeInt32() {
    return TypeBuiltin(llcpp::fuchsia::shell::BuiltinType::INT32);
  }

  llcpp::fuchsia::shell::ShellType TypeUint32() {
    return TypeBuiltin(llcpp::fuchsia::shell::BuiltinType::UINT32);
  }

  llcpp::fuchsia::shell::ShellType TypeInt64() {
    return TypeBuiltin(llcpp::fuchsia::shell::BuiltinType::INT64);
  }

  llcpp::fuchsia::shell::ShellType TypeUint64() {
    return TypeBuiltin(llcpp::fuchsia::shell::BuiltinType::UINT64);
  }

  llcpp::fuchsia::shell::ShellType TypeInteger() {
    return TypeBuiltin(llcpp::fuchsia::shell::BuiltinType::INTEGER);
  }

  llcpp::fuchsia::shell::ShellType TypeFloat32() {
    return TypeBuiltin(llcpp::fuchsia::shell::BuiltinType::FLOAT32);
  }

  llcpp::fuchsia::shell::ShellType TypeFloat64() {
    return TypeBuiltin(llcpp::fuchsia::shell::BuiltinType::FLOAT64);
  }

  llcpp::fuchsia::shell::ShellType TypeObject(AstBuilder::NodeId schema_node) {
    llcpp::fuchsia::shell::NodeId* schema_ptr = ManagedNodeId(schema_node);
    return llcpp::fuchsia::shell::ShellType::WithObjectSchema(fidl::unowned_ptr(schema_ptr));
  }

 private:
  uint64_t file_id_;
  fidl::aligned<bool> undef_;
  uint64_t next_id_;
  // Replace with arena allocation.
  std::vector<std::unique_ptr<char[]>> bufs_;
  std::vector<llcpp::fuchsia::shell::NodeDefinition> nodes_;
  std::vector<llcpp::fuchsia::shell::Node> raw_nodes_;

  struct FidlNodeIdPair {
    FidlNodeIdPair(llcpp::fuchsia::shell::NodeId&& schema, llcpp::fuchsia::shell::NodeId&& value)
        : schema_id(std::move(schema)), value_id(std::move(value)) {}
    llcpp::fuchsia::shell::NodeId schema_id;
    llcpp::fuchsia::shell::NodeId value_id;
  };

  llcpp::fuchsia::shell::ShellType TypeBuiltin(llcpp::fuchsia::shell::BuiltinType type) {
    llcpp::fuchsia::shell::BuiltinType* type_ptr = ManageCopyOf(&type);
    return llcpp::fuchsia::shell::ShellType::WithBuiltinType(fidl::unowned_ptr(type_ptr));
  }

  llcpp::fuchsia::shell::NodeId* ManagedNodeId(NodeId node_id) { return ManageCopyOf(&node_id); }

  std::vector<std::vector<FidlNodeIdPair>> object_stack_;
};

}  // namespace shell::console

#endif  // SRC_DEVELOPER_SHELL_CONSOLE_AST_BUILDER_H_
