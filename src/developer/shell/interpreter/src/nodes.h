// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_NODES_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_NODES_H_

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>

#include "src/developer/shell/interpreter/src/code.h"
#include "src/developer/shell/interpreter/src/value.h"

namespace shell {
namespace interpreter {

class ExecutionContext;
class ExecutionScope;
class Expression;
class Instruction;
class IntegerLiteral;
class Interpreter;
class Scope;
class StringLiteral;
class Variable;

struct NodeId {
  NodeId(uint64_t file_id, uint64_t node_id) : file_id(file_id), node_id(node_id) {}

  // The id of the file which defines the node.
  uint64_t file_id;
  // The node id.
  uint64_t node_id;

  bool operator<(const NodeId& ref) const {
    return (node_id < ref.node_id) || (file_id < ref.file_id);
  }

  // Returns a text representation.
  std::string StringId() const { return std::to_string(file_id) + ":" + std::to_string(node_id); }
};

// Base class for a type.
class Type {
 public:
  Type() = default;
  virtual ~Type() = default;

  // The size for the type in bytes.
  virtual size_t Size() const = 0;

  // Returns true if the type is the undefined type.
  virtual bool IsUndefined() const { return false; }

  // Creates an exact copy of the type.
  virtual std::unique_ptr<Type> Duplicate() const = 0;

  // Prints the type.
  virtual void Dump(std::ostream& os) const = 0;

  // Creates a variable of this type in the scope.
  virtual Variable* CreateVariable(ExecutionContext* context, Scope* scope, NodeId id,
                                   const std::string& name) const;

  // Generates a default value for this type. When the generated code is executed, it pushes the
  // value to the thread's stack values.
  virtual void GenerateDefaultValue(ExecutionContext* context, code::Code* code) const;

  // Generates an integer literal for this type. When the generated code is executed, it pushes the
  // value to the thread's stack value. The generation can generate an error if the literal is not
  // compatible with the type.
  virtual void GenerateIntegerLiteral(ExecutionContext* context, code::Code* code,
                                      const IntegerLiteral* literal) const;

  // Generates a string literal for this type. When the generated code is executed, it pushes the
  // value to the thread's stack value. The generation can generate an error if the literal is not
  // compatible with the type.
  virtual void GenerateStringLiteral(ExecutionContext* context, code::Code* code,
                                     const StringLiteral* literal) const;

  // Loads the current value of the variable stored at |index| in |scope| into |value|.
  virtual void LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const;
};

inline std::ostream& operator<<(std::ostream& os, const Type& type) {
  type.Dump(os);
  return os;
}

// Base class for all the AST nodes.
class Node {
 public:
  Node(Interpreter* interpreter, uint64_t file_id, uint64_t node_id);
  virtual ~Node();

  Interpreter* interpreter() const { return interpreter_; }
  const NodeId& id() const { return id_; }
  uint64_t file_id() const { return id_.file_id; }
  uint64_t node_id() const { return id_.node_id; }

  // Returns a text representation of the node id.
  std::string StringId() const { return id_.StringId(); }

 private:
  // The interpreter which owns the node.
  Interpreter* interpreter_;
  // The node id.
  NodeId id_;
};

// Base class for all the expressions. Expressions generate a result which can be used by another
// expression or by an instruction.
class Expression : public Node {
 public:
  Expression(Interpreter* interpreter, uint64_t file_id, uint64_t node_id)
      : Node(interpreter, file_id, node_id) {}

  // Returns the type of the expression. The pointer is always valid (but it can be TypeUndefined).
  virtual std::unique_ptr<Type> GetType() const;

  // Prints the expression.
  virtual void Dump(std::ostream& os) const = 0;

  // Compiles the expression (perform the semantic checks and generates code).
  virtual void Compile(ExecutionContext* context, code::Code* code, const Type* for_type) const = 0;
};

inline std::ostream& operator<<(std::ostream& os, const Expression& expression) {
  expression.Dump(os);
  return os;
}

// Base class for all the instructions.
class Instruction : public Node {
 public:
  Instruction(Interpreter* interpreter, uint64_t file_id, uint64_t node_id)
      : Node(interpreter, file_id, node_id) {}

  // Prints the instruction.
  virtual void Dump(std::ostream& os) const = 0;

  // Compiles the instruction (performs the semantic checks and generates code).
  virtual void Compile(ExecutionContext* context, code::Code* code) const = 0;
};

inline std::ostream& operator<<(std::ostream& os, const Instruction& instruction) {
  instruction.Dump(os);
  return os;
}

}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_NODES_H_
