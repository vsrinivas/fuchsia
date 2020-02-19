// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <vector>

#include "src/developer/shell/interpreter/test/interpreter_test.h"

fuchsia::shell::NodeId NullNode{0, 0};

fuchsia::shell::ShellType TypeUndef() {
  fuchsia::shell::ShellType type;
  type.set_undef(true);
  return type;
}

fuchsia::shell::ShellType TypeBool() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::BOOL);
  return type;
}

fuchsia::shell::ShellType TypeChar() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::CHAR);
  return type;
}

fuchsia::shell::ShellType TypeString() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::STRING);
  return type;
}

fuchsia::shell::ShellType TypeInt8() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::INT8);
  return type;
}

fuchsia::shell::ShellType TypeUint8() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::UINT8);
  return type;
}

fuchsia::shell::ShellType TypeInt16() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::INT16);
  return type;
}

fuchsia::shell::ShellType TypeUint16() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::UINT16);
  return type;
}

fuchsia::shell::ShellType TypeInt32() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::INT32);
  return type;
}

fuchsia::shell::ShellType TypeUint32() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::UINT32);
  return type;
}

fuchsia::shell::ShellType TypeInt64() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::INT64);
  return type;
}

fuchsia::shell::ShellType TypeUint64() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::UINT64);
  return type;
}

fuchsia::shell::ShellType TypeInteger() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::INTEGER);
  return type;
}

fuchsia::shell::ShellType TypeFloat32() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::FLOAT32);
  return type;
}

fuchsia::shell::ShellType TypeFloat64() {
  fuchsia::shell::ShellType type;
  type.set_builtin_type(fuchsia::shell::BuiltinType::FLOAT64);
  return type;
}

// Helper class to create nodes.
class NodeBuilder {
 public:
  explicit NodeBuilder(uint64_t file_id) : file_id_(file_id) {}

  std::vector<fuchsia::shell::NodeDefinition>* nodes() { return &nodes_; }

  // Adds a node definition to the list of nodes.
  fuchsia::shell::NodeId AddNode(fuchsia::shell::Node* node, bool root_node) {
    fuchsia::shell::NodeDefinition node_definition;
    fuchsia::shell::NodeId node_id{file_id_, ++last_node_id_};
    node_definition.node_id = node_id;
    node_definition.node = std::move(*node);
    node_definition.root_node = root_node;
    nodes_.emplace_back(std::move(node_definition));
    return node_id;
  }

  /// Adds an integer literal to the list of nodes.
  fuchsia::shell::NodeId IntegerLiteral(uint64_t absolute_value, bool negative) {
    fuchsia::shell::Node node;
    std::vector<uint64_t> values;
    values.push_back(absolute_value);
    node.set_integer_literal({std::move(values), negative});
    return AddNode(&node, /*root_node=*/false);
  }

  // Adds a variable definition to the list of nodes.
  fuchsia::shell::NodeId VariableDefinition(const char* name, fuchsia::shell::ShellType type,
                                            bool mutable_value,
                                            fuchsia::shell::NodeId initial_value,
                                            bool root_node = true) {
    fuchsia::shell::Node node;
    node.set_variable_definition({name, std::move(type), mutable_value, initial_value});
    return AddNode(&node, root_node);
  }

 private:
  // The file id for all the nodes built by this builder.
  const uint64_t file_id_;
  // Last value used for a node id.
  uint64_t last_node_id_ = 0;
  // All the nodes which will be sent to the server.
  std::vector<fuchsia::shell::NodeDefinition> nodes_;
};

TEST_F(InterpreterTest, ContextNotCreated) {
  shell()->ExecuteExecutionContext(1);
  Run();

  ASSERT_EQ("Execution context 1 not defined.\n", GlobalErrors());
}

TEST_F(InterpreterTest, ContextCreatedTwice) {
  shell()->CreateExecutionContext(1);
  shell()->CreateExecutionContext(1);
  Run();

  ASSERT_EQ("Execution context 1 is already in use.\n", GlobalErrors());
}

TEST_F(InterpreterTest, NoPendingInstruction) {
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);
  shell()->ExecuteExecutionContext(context->id);
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::ANALYSIS_ERROR, context->result);

  std::string error_result = context->error_stream.str();
  ASSERT_EQ("No pending instruction to execute.\n", error_result);
}

TEST_F(InterpreterTest, GlobalExpression) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  fuchsia::shell::Node node;
  std::vector<uint64_t> values;
  node.set_integer_literal({std::move(values), false});
  builder.AddNode(&node, /*root_node=*/true);

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::ANALYSIS_ERROR, context->result);

  std::string error_result = context->error_stream.str();
  ASSERT_EQ("Node 1:1 can't be a root node.\n", error_result);
}

TEST_F(InterpreterTest, BadAst) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  builder.IntegerLiteral(1, true);

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::ANALYSIS_ERROR, context->result);

  std::string error_result = context->error_stream.str();
  ASSERT_EQ("Pending AST nodes for execution context 1.\n", error_result);
}

TEST_F(InterpreterTest, VariableDefinition) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  builder.VariableDefinition("foo", TypeUint64(), true, NullNode);
  builder.VariableDefinition("bar", TypeUndef(), false, builder.IntegerLiteral(1, true));
  builder.VariableDefinition("x", TypeUint64(), false, builder.IntegerLiteral(10, false));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->result);
  ASSERT_FALSE(last_result_partial());
  ASSERT_EQ(results().size(), static_cast<size_t>(3));
  ASSERT_EQ(results()[0], "var foo: uint64\n");
  ASSERT_EQ(results()[1], "const bar = -1\n");
  ASSERT_EQ(results()[2], "const x: uint64 = 10\n");
}

TEST_F(InterpreterTest, BuiltinTypes) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  builder.VariableDefinition("b", TypeBool(), true, NullNode);
  builder.VariableDefinition("c", TypeChar(), true, NullNode);
  builder.VariableDefinition("s", TypeString(), true, NullNode);
  builder.VariableDefinition("i8", TypeInt8(), true, NullNode);
  builder.VariableDefinition("u8", TypeUint8(), true, NullNode);
  builder.VariableDefinition("i16", TypeInt16(), true, NullNode);
  builder.VariableDefinition("u16", TypeUint16(), true, NullNode);
  builder.VariableDefinition("i32", TypeInt32(), true, NullNode);
  builder.VariableDefinition("u32", TypeUint32(), true, NullNode);
  builder.VariableDefinition("i64", TypeInt64(), true, NullNode);
  builder.VariableDefinition("u64", TypeUint64(), true, NullNode);
  builder.VariableDefinition("big_int", TypeInteger(), true, NullNode);
  builder.VariableDefinition("f32", TypeFloat32(), true, NullNode);
  builder.VariableDefinition("f64", TypeFloat64(), true, NullNode);

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->result);
  ASSERT_FALSE(last_result_partial());
  ASSERT_EQ(results().size(), static_cast<size_t>(14));
  ASSERT_EQ(results()[0], "var b: bool\n");
  ASSERT_EQ(results()[1], "var c: char\n");
  ASSERT_EQ(results()[2], "var s: string\n");
  ASSERT_EQ(results()[3], "var i8: int8\n");
  ASSERT_EQ(results()[4], "var u8: uint8\n");
  ASSERT_EQ(results()[5], "var i16: int16\n");
  ASSERT_EQ(results()[6], "var u16: uint16\n");
  ASSERT_EQ(results()[7], "var i32: int32\n");
  ASSERT_EQ(results()[8], "var u32: uint32\n");
  ASSERT_EQ(results()[9], "var i64: int64\n");
  ASSERT_EQ(results()[10], "var u64: uint64\n");
  ASSERT_EQ(results()[11], "var big_int: integer\n");
  ASSERT_EQ(results()[12], "var f32: float32\n");
  ASSERT_EQ(results()[13], "var f64: float64\n");
}
