// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_SCOPE_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_SCOPE_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/developer/shell/interpreter/src/nodes.h"

namespace shell {
namespace interpreter {

// Base class for all scope variables.
class Variable {
 public:
  Variable(NodeId id, const std::string& name) : id_(id), name_(name) {}

  NodeId id() const { return id_; }
  const std::string& name() const { return name_; }

 private:
  // Id of the node which defines the variable.
  const NodeId id_;
  // Name of the variable.
  const std::string name_;
};

// A variable of type int64.
class Int64Variable : public Variable {
 public:
  Int64Variable(NodeId id, const std::string& name, size_t index)
      : Variable(id, name), index_(index) {}

  size_t index() const { return index_; }

 private:
  // Index in the execution scope's int64_variables_ vector.
  const size_t index_;
};

// A variable of type uint64.
class Uint64Variable : public Variable {
 public:
  Uint64Variable(NodeId id, const std::string& name, size_t index)
      : Variable(id, name), index_(index) {}

  size_t index() const { return index_; }

 private:
  // Index in the execution scope's int64_variables_ vector.
  const size_t index_;
};

// Defines a scope. This can be a global scope (at the isolate level) or a scope associated to
// a thread, a function, a block, ...
// This scope is used during compilation and is referenced by the execution scope during the
// execution.
class Scope {
 public:
  Scope() = default;

  // Returns the variable with the given name.
  Variable* GetVariable(const std::string& name) const {
    auto result = variables_.find(name);
    if (result == variables_.end()) {
      return nullptr;
    }
    return result->second.get();
  }

  // Create a variable of type int64.
  void CreateInt64Variable(NodeId id, const std::string& name) {
    variables_.emplace(
        std::make_pair(name, std::make_unique<Int64Variable>(id, name, next_int64_index_++)));
  }

  // Create a variable of type uint64.
  void CreateUint64Variable(NodeId id, const std::string& name) {
    variables_.emplace(
        std::make_pair(name, std::make_unique<Uint64Variable>(id, name, next_int64_index_++)));
  }

 private:
  // All the variables for this scope.
  std::map<std::string, std::unique_ptr<Variable>> variables_;
  // Next index in the int64_variables_ field of an execution scope. This index is shared by int64
  // and uint64 variables.
  size_t next_int64_index_;
};

}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_SCOPE_H_
