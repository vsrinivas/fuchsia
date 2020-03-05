// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_INTERPRETER_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_INTERPRETER_H_

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/developer/shell/interpreter/src/code.h"
#include "src/developer/shell/interpreter/src/isolate.h"
#include "src/developer/shell/interpreter/src/nodes.h"

namespace shell {
namespace interpreter {

class Interpreter;

// Defines an execution context. Each execution context is a standalone entity which executes its
// instructions in parallel with other execution contexts (eventually in separated threads).
// For a batch execution, we have only one execution context for the program.
// For an interactive shell, we usually have one execution context per line.
class ExecutionContext {
 public:
  ExecutionContext(Interpreter* interpreter, uint64_t id) : interpreter_(interpreter), id_(id) {}

  Interpreter* interpreter() const { return interpreter_; }
  uint64_t id() const { return id_; }
  bool has_errors() const { return has_errors_; }
  void set_has_errors() { has_errors_ = true; }

  // Adds an instruction which will be executed by the following Execute.
  void AddPendingInstruction(std::unique_ptr<Instruction> instruction) {
    pending_instructions_.emplace_back(std::move(instruction));
  }

  // Emit an error not associated to a node.
  void EmitError(std::string error_message);

  // Emit an error associated to a node.
  void EmitError(NodeId node_id, std::string error_message);

  // Dumps all the pending instructions.
  void Dump();

  // Compiles all the pending instructions.
  void Compile(code::Code* code);

  // Executes all the pending instructions.
  void Execute();

 private:
  // Interpreter which owns the context.
  Interpreter* const interpreter_;
  // Context id for the interpreter which owns the context.
  const uint64_t id_;
  // Instructions waiting to be executed.
  std::vector<std::unique_ptr<Instruction>> pending_instructions_;
  // True if the context encountered an error.
  bool has_errors_ = false;
};

// Defines an interpreter. This is a sand boxed object. That means that one interpreter can
// only access to the objects it defines. It cannot access to other interpreters' data.
// However, execution contexts from an interpreter share the same data.
class Interpreter {
 public:
  Interpreter() : isolate_(std::make_unique<Isolate>()) {}
  virtual ~Interpreter() = default;

  Isolate* isolate() const { return isolate_.get(); }

  // Called when the interpreter encouter an error.
  virtual void EmitError(ExecutionContext* context, std::string error_message) = 0;

  // Called when the interpreter encouter an error associated to a node.
  virtual void EmitError(ExecutionContext* context, NodeId node_id, std::string error_message) = 0;

  // Called when a context has dumped all its pending instructions.
  virtual void DumpDone(ExecutionContext* context) = 0;

  // Called when a context is ready to terminate. Case where the execution succeeded.
  virtual void ContextDone(ExecutionContext* context) = 0;

  // Called when a context is ready to terminate. Case where the context terminated early because
  // it encountered an analysis/semantic error.
  virtual void ContextDoneWithAnalysisError(ExecutionContext* context) = 0;

  // Called when a context emit a text result.
  virtual void TextResult(ExecutionContext* context, std::string_view text) = 0;

  // Gets the context for the specified id.
  ExecutionContext* GetContext(uint64_t context_id) const {
    auto context = contexts_.find(context_id);
    if (context != contexts_.end()) {
      return context->second.get();
    }
    return nullptr;
  }

  // Adds a new execution context.
  ExecutionContext* AddContext(uint64_t context_id);

  // Erases an execution context.
  void EraseContext(uint64_t context_id) { contexts_.erase(context_id); }

  // Returns the node with the specified id.  The node is still owned by the interpreter.
  Node* GetNode(uint64_t file_id, uint64_t node_id) const {
    auto result = nodes_.find(std::make_pair(file_id, node_id));
    if (result == nodes_.end()) {
      return nullptr;
    }
    return result->second;
  }

  Node* GetNode(NodeId id) const { return GetNode(id.file_id, id.node_id); }

  // Associates a node with an id.  Node is kept alive directly or indirectly by the execution
  // context.
  void AddNode(uint64_t file_id, uint64_t node_id, Node* node);

  // Removes the association between a node and an id.
  void RemoveNode(uint64_t file_id, uint64_t node_id);

  // Inserts an expression into a node.
  void InsertExpression(ExecutionContext* context, uint64_t container_file_id,
                        uint64_t container_node_id, std::unique_ptr<Expression> expression);
  // Inserts an instruction into a node.
  void InsertInstruction(ExecutionContext* context, uint64_t container_file_id,
                         uint64_t container_node_id, std::unique_ptr<Instruction> instruction);

  const Variable* SearchGlobal(const std::string& name) const {
    return isolate_->SearchGlobal(name);
  }

  const Variable* SearchGlobal(const NodeId& node_id) const {
    return isolate_->SearchGlobal(node_id);
  }

  void LoadGlobal(const Variable* variable, Value* value) const {
    return isolate_->LoadGlobal(variable, value);
  }

 private:
  // All the contexts for the interpreter.
  std::map<uint64_t, std::unique_ptr<ExecutionContext>> contexts_;
  // All the nodes handled by the interpreter.
  std::map<std::pair<uint64_t, uint64_t>, Node*> nodes_;
  // The isolate ran by the interpreter.
  std::unique_ptr<Isolate> isolate_;
};

}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_INTERPRETER_H_
