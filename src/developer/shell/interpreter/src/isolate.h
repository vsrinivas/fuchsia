// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_ISOLATE_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_ISOLATE_H_

#include <memory>
#include <vector>

#include "src/developer/shell/interpreter/src/code.h"
#include "src/developer/shell/interpreter/src/scope.h"
#include "src/developer/shell/interpreter/src/thread.h"

namespace shell {
namespace interpreter {

// Defines an isolate. Each isolate is completely independent from the others.
class Isolate {
 public:
  Isolate() : thread_(std::make_unique<Thread>(this)) {}

  Scope* global_scope() { return &global_scope_; }
  ExecutionScope* global_execution_scope() { return &global_execution_scope_; }

  const Variable* SearchGlobal(const std::string& name) const {
    return global_scope_.GetVariable(name);
  }

  const Variable* SearchGlobal(const NodeId& node_id) const {
    return global_scope_.SearchVariable(node_id);
  }

  void LoadGlobal(const Variable* variable, Value* value) const {
    return global_execution_scope_.Load(variable, value);
  }

  // Allocate more space for the global variables (if needed). This is called after we potentially
  // added some global variables.
  void AllocateGlobals() { global_execution_scope_.Resize(global_scope_.size()); }

  // Executes some code for the isolate.
  void Execute(ExecutionContext* context, std::unique_ptr<code::Code> code) {
    thread_->Execute(context, std::move(code));
  }

 private:
  // Global scope for the isolate. Holds the global variable definitions.
  Scope global_scope_;
  // Global execution scope for the isolate. Holds the storage (the values) for the variables.
  ExecutionScope global_execution_scope_;
  // The thread for this isolate (currently we are mono threaded).
  std::unique_ptr<Thread> thread_;
};

}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_ISOLATE_H_
