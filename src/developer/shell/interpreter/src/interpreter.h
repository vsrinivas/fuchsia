// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_INTERPRETER_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_INTERPRETER_H_

#include <map>
#include <string>

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

 private:
  // Interpreter which owns the context.
  Interpreter* interpreter_;
  // Context id for the interpreter which owns the context.
  uint64_t id_;
};

// Defines an interpreter. This is a sand boxed object. That means that one interpreter can
// only access to the objects it defines. It cannot access to other interpreters' data.
// However, execution contexts from an interpreter share the same data.
class Interpreter {
 public:
  Interpreter() = default;
  virtual ~Interpreter() = default;

  // Called when the interpreter encouter an error.
  virtual void EmitError(ExecutionContext* context, std::string error_message) = 0;
  // Called when a context is ready to terminate. Case where the context terminated early because
  // it encountered an analysis/semantic error.
  virtual void ContextDoneWithAnalysisError(ExecutionContext* context) = 0;

  ExecutionContext* GetContext(uint64_t context_id) const {
    auto context = contexts_.find(context_id);
    if (context != contexts_.end()) {
      return context->second.get();
    }
    return nullptr;
  }

  // Adds a new execution context.
  void AddContext(uint64_t context_id);

  // Executes a previously created context.
  void ExecuteContext(uint64_t context_id);

 private:
  // All the contexts for the interpreter.
  std::map<uint64_t, std::unique_ptr<ExecutionContext>> contexts_;
};

}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_INTERPRETER_H_
