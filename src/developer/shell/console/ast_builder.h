// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_CONSOLE_AST_BUILDER_H_
#define SRC_DEVELOPER_SHELL_CONSOLE_AST_BUILDER_H_

#include <climits>
#include <string>
#include <vector>

#include "fuchsia/shell/llcpp/fidl.h"
#include "src/developer/shell/console/err.h"

namespace shell::console {

// Builds the remote AST for consumption by the interpreter service.
class AstBuilder {
 public:
  AstBuilder() : undef_(true), next_id_(0) {}

  AstBuilder& operator=(AstBuilder&&);
  AstBuilder(AstBuilder&&);

  // The undefined type.  Useful when your node isn't typed.
  llcpp::fuchsia::shell::ShellType undef() {
    return llcpp::fuchsia::shell::ShellType::WithUndef(&undef_);
  }

  // Returns the set of nodes managed by thie AstBuilder as a vector view, suitable for sending to
  // the service.
  fidl::VectorView<llcpp::fuchsia::shell::NodeDefinition> AsVectorView() {
    return fidl::VectorView(nodes_);
  }

  bool empty() const { return nodes_.empty(); }

  template <class T>
  T* ManageCopyOf(const T* value, size_t size = sizeof(T)) {
    auto buf = bufs_.emplace_back(new char[size]).get();
    T* def = reinterpret_cast<T*>(buf);
    memcpy(buf, value, size);
    return def;
  }

  template <class T>
  T* ManageNew() {
    auto buf = bufs_.emplace_back(new char[sizeof(T)]).get();
    return reinterpret_cast<T*>(buf);
  }

  // Sets the given node to be the root node for remote computation.
  void SetRoot(uint64_t node_id);

  // Adds a variable declaration.  The variable is named with the given |identifier|, the type is
  // the given |type|, the |node_id| refers to the node that, when evaluated, gives the initial
  // value, and |is_const| tells you whether the variable is const.  Returns the resulting node_id.
  uint64_t AddVariableDeclaration(const std::string& identifier,
                                  llcpp::fuchsia::shell::ShellType type, uint64_t node_id,
                                  bool is_const);

  // Adds an integer literal node with the value |i|.  Returns the resulting node_id.
  uint64_t AddIntegerLiteral(int64_t i);

  AstBuilder& operator=(const AstBuilder&) = delete;
  AstBuilder(const AstBuilder&) = delete;

 private:
  bool undef_;
  uint64_t next_id_;
  // Replace with arena allocation.
  std::vector<std::unique_ptr<char[]>> bufs_;
  std::vector<llcpp::fuchsia::shell::NodeDefinition> nodes_;

  // Returns the added node_id
  uint64_t AddNode(llcpp::fuchsia::shell::Node&& node);
};

}  // namespace shell::console

#endif  // SRC_DEVELOPER_SHELL_CONSOLE_AST_BUILDER_H_
