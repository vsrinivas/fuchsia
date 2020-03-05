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

  /// Adds an integer literal to the list of nodes.
  fuchsia::shell::NodeId StringLiteral(std::string value) {
    fuchsia::shell::Node node;
    node.set_string_literal(std::move(value));
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

  // Adds a previously defined variable.
  fuchsia::shell::NodeId Variable(fuchsia::shell::NodeId variable) {
    fuchsia::shell::Node node;
    node.set_variable({variable});
    return AddNode(&node, /*root_node=*/false);
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
  builder.VariableDefinition("bar", TypeInt64(), false, builder.IntegerLiteral(1, true));
  builder.VariableDefinition("x", TypeUint64(), false, builder.IntegerLiteral(10, false));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->DumpExecutionContext(context->id);
  Run();

  ASSERT_FALSE(last_result_partial());
  ASSERT_EQ(results().size(), static_cast<size_t>(3));
  ASSERT_EQ(results()[0], "var foo: uint64\n");
  ASSERT_EQ(results()[1], "const bar: int64 = -1\n");
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
  shell()->DumpExecutionContext(context->id);
  Run();

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

TEST_F(InterpreterTest, VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  builder.VariableDefinition("foo", TypeUint64(), false, builder.IntegerLiteral(1, false));
  builder.VariableDefinition("bar", TypeUint64(), false, builder.IntegerLiteral(10, false));
  builder.VariableDefinition("groucho", TypeString(), false,
                             builder.StringLiteral("A Marx brother"));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("foo");
  LoadGlobal("bar");
  LoadGlobal("groucho");
  LoadGlobal("x");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* foo = GetGlobal("foo");
  ASSERT_TRUE(foo->is_integer_literal());
  ASSERT_FALSE(foo->integer_literal().negative);
  ASSERT_EQ(foo->integer_literal().absolute_value.size(), static_cast<size_t>(1));
  ASSERT_EQ(foo->integer_literal().absolute_value[0], 1U);

  fuchsia::shell::Node* bar = GetGlobal("bar");
  ASSERT_TRUE(bar->is_integer_literal());
  ASSERT_FALSE(bar->integer_literal().negative);
  ASSERT_EQ(bar->integer_literal().absolute_value.size(), static_cast<size_t>(1));
  ASSERT_EQ(bar->integer_literal().absolute_value[0], 10U);

  fuchsia::shell::Node* groucho = GetGlobal("groucho");
  ASSERT_TRUE(groucho->is_string_literal());
  ASSERT_EQ("A Marx brother",
            std::string(groucho->string_literal().data(), groucho->string_literal().size()));

  fuchsia::shell::Node* x = GetGlobal("x");
  ASSERT_EQ(x, nullptr);
}

TEST_F(InterpreterTest, VariableNoType) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  builder.VariableDefinition("bar", TypeUndef(), false, NullNode);
  builder.VariableDefinition("foo", TypeUndef(), false, builder.IntegerLiteral(1, false));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::ANALYSIS_ERROR, context->result);

  std::string error_result = context->error_stream.str();
  ASSERT_EQ(
      "node 1:1 Type not defined.\n"
      "node 1:3 Type not defined.\n",
      error_result);
}

TEST_F(InterpreterTest, VariableTypeNotImplemented) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  builder.VariableDefinition("bar", TypeInteger(), false, NullNode);

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::ANALYSIS_ERROR, context->result);

  std::string error_result = context->error_stream.str();
  ASSERT_EQ("node 1:1 Can't create variable 'bar' of type integer (not implemented yet).\n",
            error_result);
}

TEST_F(InterpreterTest, VariableDefinedTwice) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  builder.VariableDefinition("bar", TypeUint64(), false, NullNode);
  builder.VariableDefinition("bar", TypeUint64(), false, NullNode);

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::ANALYSIS_ERROR, context->result);

  std::string error_result = context->error_stream.str();
  ASSERT_EQ(
      "node 1:2 Variable 'bar' already defined.\n"
      "node 1:1 First definition.\n",
      error_result);
}

TEST_F(InterpreterTest, BadIntegerLiterals) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  builder.VariableDefinition("i8p", TypeInt8(), false, builder.IntegerLiteral(0x80, false));
  builder.VariableDefinition("i8n", TypeInt8(), false, builder.IntegerLiteral(0x81, true));
  builder.VariableDefinition("u8p", TypeUint8(), false, builder.IntegerLiteral(0x100, false));
  builder.VariableDefinition("u8n", TypeUint8(), false, builder.IntegerLiteral(1, true));

  builder.VariableDefinition("i16p", TypeInt16(), false, builder.IntegerLiteral(0x8000, false));
  builder.VariableDefinition("i16n", TypeInt16(), false, builder.IntegerLiteral(0x8001, true));
  builder.VariableDefinition("u16p", TypeUint16(), false, builder.IntegerLiteral(0x10000, false));
  builder.VariableDefinition("u16n", TypeUint16(), false, builder.IntegerLiteral(1, true));

  builder.VariableDefinition("i32p", TypeInt32(), false, builder.IntegerLiteral(0x80000000, false));
  builder.VariableDefinition("i32n", TypeInt32(), false, builder.IntegerLiteral(0x80000001, true));
  builder.VariableDefinition("u32p", TypeUint32(), false,
                             builder.IntegerLiteral(0x100000000L, false));
  builder.VariableDefinition("u32n", TypeUint32(), false, builder.IntegerLiteral(1, true));

  builder.VariableDefinition("i64p", TypeInt64(), false,
                             builder.IntegerLiteral(0x8000000000000000UL, false));
  builder.VariableDefinition("i64n", TypeInt64(), false,
                             builder.IntegerLiteral(0x8000000000000001UL, true));
  builder.VariableDefinition("u64n", TypeUint64(), false, builder.IntegerLiteral(1, true));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::ANALYSIS_ERROR, context->result);

  std::string error_result = context->error_stream.str();
  ASSERT_EQ(
      "node 1:1 Can't create an integer literal of type int8 with 128.\n"
      "node 1:3 Can't create an integer literal of type int8 with -129.\n"
      "node 1:5 Can't create an integer literal of type uint8 with 256.\n"
      "node 1:7 Can't create an integer literal of type uint8 with -1.\n"
      "node 1:9 Can't create an integer literal of type int16 with 32768.\n"
      "node 1:11 Can't create an integer literal of type int16 with -32769.\n"
      "node 1:13 Can't create an integer literal of type uint16 with 65536.\n"
      "node 1:15 Can't create an integer literal of type uint16 with -1.\n"
      "node 1:17 Can't create an integer literal of type int32 with 2147483648.\n"
      "node 1:19 Can't create an integer literal of type int32 with -2147483649.\n"
      "node 1:21 Can't create an integer literal of type uint32 with 4294967296.\n"
      "node 1:23 Can't create an integer literal of type uint32 with -1.\n"
      "node 1:25 Can't create an integer literal of type int64 with 9223372036854775808.\n"
      "node 1:27 Can't create an integer literal of type int64 with -9223372036854775809.\n"
      "node 1:29 Can't create an integer literal of type uint64 with -1.\n",
      error_result);
}

TEST_F(InterpreterTest, GoodIntegerLiterals) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  builder.VariableDefinition("i8p", TypeInt8(), false, builder.IntegerLiteral(0x7f, false));
  builder.VariableDefinition("i8n", TypeInt8(), false, builder.IntegerLiteral(0x80, true));
  builder.VariableDefinition("u8p", TypeUint8(), false, builder.IntegerLiteral(0xff, false));
  builder.VariableDefinition("u8n", TypeUint8(), false, builder.IntegerLiteral(0, false));

  builder.VariableDefinition("i16p", TypeInt16(), false, builder.IntegerLiteral(0x7fff, false));
  builder.VariableDefinition("i16n", TypeInt16(), false, builder.IntegerLiteral(0x8000, true));
  builder.VariableDefinition("u16p", TypeUint16(), false, builder.IntegerLiteral(0xffff, false));
  builder.VariableDefinition("u16n", TypeUint16(), false, builder.IntegerLiteral(0, false));

  builder.VariableDefinition("i32p", TypeInt32(), false, builder.IntegerLiteral(0x7fffffff, false));
  builder.VariableDefinition("i32n", TypeInt32(), false, builder.IntegerLiteral(0x80000000, true));
  builder.VariableDefinition("u32p", TypeUint32(), false,
                             builder.IntegerLiteral(0xffffffffL, false));
  builder.VariableDefinition("u32n", TypeUint32(), false, builder.IntegerLiteral(0, false));

  builder.VariableDefinition("i64p", TypeInt64(), false,
                             builder.IntegerLiteral(0x7fffffffffffffffUL, false));
  builder.VariableDefinition("i64n", TypeInt64(), false,
                             builder.IntegerLiteral(0x8000000000000000UL, true));
  builder.VariableDefinition("u64p", TypeUint64(), false,
                             builder.IntegerLiteral(0xffffffffffffffffUL, false));
  builder.VariableDefinition("u64n", TypeUint64(), false, builder.IntegerLiteral(0, false));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());
}

TEST_F(InterpreterTest, LoadStringVariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  auto a_marx_brother = builder.VariableDefinition("a_marx_brother", TypeString(), false,
                                                   builder.StringLiteral("A Marx brother"));
  builder.VariableDefinition("groucho", TypeString(), false, builder.Variable(a_marx_brother));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("groucho");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* groucho = GetGlobal("groucho");
  ASSERT_TRUE(groucho->is_string_literal());
  ASSERT_EQ("A Marx brother",
            std::string(groucho->string_literal().data(), groucho->string_literal().size()));
}

TEST_F(InterpreterTest, LoadStringVariableFromAnotherContext) {
  constexpr uint64_t kFileId = 1;

  // First context.
  InterpreterTestContext* context_1 = CreateContext();
  shell()->CreateExecutionContext(context_1->id);

  NodeBuilder builder(kFileId);
  auto a_marx_brother = builder.VariableDefinition("a_marx_brother", TypeString(), false,
                                                   builder.StringLiteral("A Marx brother"));

  shell()->AddNodes(context_1->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context_1->id);

  // Second context.
  InterpreterTestContext* context_2 = CreateContext();
  shell()->CreateExecutionContext(context_2->id);

  builder.VariableDefinition("groucho", TypeString(), false, builder.Variable(a_marx_brother));

  shell()->AddNodes(context_2->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context_2->id);

  // Check execution.
  LoadGlobal("groucho");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context_1->GetResult());

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context_2->GetResult());

  fuchsia::shell::Node* groucho = GetGlobal("groucho");
  ASSERT_TRUE(groucho->is_string_literal());
  ASSERT_EQ("A Marx brother",
            std::string(groucho->string_literal().data(), groucho->string_literal().size()));
}

TEST_F(InterpreterTest, LoadInt8VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  auto x = builder.VariableDefinition("x", TypeInt8(), false, builder.IntegerLiteral(1, true));
  builder.VariableDefinition("y", TypeInt8(), false, builder.Variable(x));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("y");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* y = GetGlobal("y");
  ASSERT_TRUE(y->is_integer_literal());
  ASSERT_TRUE(y->integer_literal().negative);
  ASSERT_EQ(y->integer_literal().absolute_value.size(), static_cast<size_t>(1));
  ASSERT_EQ(y->integer_literal().absolute_value[0], 1U);
}

TEST_F(InterpreterTest, LoadUint8VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  auto x = builder.VariableDefinition("x", TypeUint8(), false, builder.IntegerLiteral(1, false));
  builder.VariableDefinition("y", TypeUint8(), false, builder.Variable(x));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("y");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* y = GetGlobal("y");
  ASSERT_TRUE(y->is_integer_literal());
  ASSERT_FALSE(y->integer_literal().negative);
  ASSERT_EQ(y->integer_literal().absolute_value.size(), static_cast<size_t>(1));
  ASSERT_EQ(y->integer_literal().absolute_value[0], 1U);
}

TEST_F(InterpreterTest, LoadInt16VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  auto x = builder.VariableDefinition("x", TypeInt16(), false, builder.IntegerLiteral(1, true));
  builder.VariableDefinition("y", TypeInt16(), false, builder.Variable(x));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("y");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* y = GetGlobal("y");
  ASSERT_TRUE(y->is_integer_literal());
  ASSERT_TRUE(y->integer_literal().negative);
  ASSERT_EQ(y->integer_literal().absolute_value.size(), static_cast<size_t>(1));
  ASSERT_EQ(y->integer_literal().absolute_value[0], 1U);
}

TEST_F(InterpreterTest, LoadUint16VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  auto x = builder.VariableDefinition("x", TypeUint16(), false, builder.IntegerLiteral(1, false));
  builder.VariableDefinition("y", TypeUint16(), false, builder.Variable(x));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("y");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* y = GetGlobal("y");
  ASSERT_TRUE(y->is_integer_literal());
  ASSERT_FALSE(y->integer_literal().negative);
  ASSERT_EQ(y->integer_literal().absolute_value.size(), static_cast<size_t>(1));
  ASSERT_EQ(y->integer_literal().absolute_value[0], 1U);
}

TEST_F(InterpreterTest, LoadInt32VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  auto x = builder.VariableDefinition("x", TypeInt32(), false, builder.IntegerLiteral(1, true));
  builder.VariableDefinition("y", TypeInt32(), false, builder.Variable(x));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("y");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* y = GetGlobal("y");
  ASSERT_TRUE(y->is_integer_literal());
  ASSERT_TRUE(y->integer_literal().negative);
  ASSERT_EQ(y->integer_literal().absolute_value.size(), static_cast<size_t>(1));
  ASSERT_EQ(y->integer_literal().absolute_value[0], 1U);
}

TEST_F(InterpreterTest, LoadUint32VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  auto x = builder.VariableDefinition("x", TypeUint32(), false, builder.IntegerLiteral(1, false));
  builder.VariableDefinition("y", TypeUint32(), false, builder.Variable(x));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("y");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* y = GetGlobal("y");
  ASSERT_TRUE(y->is_integer_literal());
  ASSERT_FALSE(y->integer_literal().negative);
  ASSERT_EQ(y->integer_literal().absolute_value.size(), static_cast<size_t>(1));
  ASSERT_EQ(y->integer_literal().absolute_value[0], 1U);
}

TEST_F(InterpreterTest, LoadInt64VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  auto x = builder.VariableDefinition("x", TypeInt64(), false, builder.IntegerLiteral(1, true));
  builder.VariableDefinition("y", TypeInt64(), false, builder.Variable(x));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("y");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* y = GetGlobal("y");
  ASSERT_TRUE(y->is_integer_literal());
  ASSERT_TRUE(y->integer_literal().negative);
  ASSERT_EQ(y->integer_literal().absolute_value.size(), static_cast<size_t>(1));
  ASSERT_EQ(y->integer_literal().absolute_value[0], 1U);
}

TEST_F(InterpreterTest, LoadUint64VariableOk) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  shell()->CreateExecutionContext(context->id);

  NodeBuilder builder(kFileId);
  auto x = builder.VariableDefinition("x", TypeUint64(), false, builder.IntegerLiteral(1, false));
  builder.VariableDefinition("y", TypeUint64(), false, builder.Variable(x));

  shell()->AddNodes(context->id, std::move(*builder.nodes()));
  shell()->ExecuteExecutionContext(context->id);
  LoadGlobal("y");
  Run();

  ASSERT_EQ(fuchsia::shell::ExecuteResult::OK, context->GetResult());

  fuchsia::shell::Node* y = GetGlobal("y");
  ASSERT_TRUE(y->is_integer_literal());
  ASSERT_FALSE(y->integer_literal().negative);
  ASSERT_EQ(y->integer_literal().absolute_value.size(), static_cast<size_t>(1));
  ASSERT_EQ(y->integer_literal().absolute_value[0], 1U);
}
