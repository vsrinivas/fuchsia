// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <vector>

#include "src/developer/shell/console/ast_builder.h"
#include "src/developer/shell/interpreter/test/interpreter_test.h"

shell::console::AstBuilder::NodeId NullNode;

TEST_F(InterpreterTest, ContextNotCreated) {
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(1));
  Finish(kError);

  ASSERT_EQ("Execution context 1 not defined.\n", GlobalErrors());
}

TEST_F(InterpreterTest, ContextCreatedTwice) {
  ASSERT_CALL_OK(shell().CreateExecutionContext(1));
  ASSERT_CALL_OK(shell().CreateExecutionContext(1));
  Finish(kError);

  ASSERT_EQ("Execution context 1 is already in use.\n", GlobalErrors());
}

TEST_F(InterpreterTest, NoPendingInstruction) {
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::ANALYSIS_ERROR, context->result);

  std::string error_result = context->error_stream.str();
  ASSERT_EQ("No pending instruction to execute.\n", error_result);
}

TEST_F(InterpreterTest, GlobalExpression) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.SetRoot(builder.AddIntegerLiteral(1));

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));

  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::ANALYSIS_ERROR, context->result);

  std::string error_result = context->error_stream.str();
  ASSERT_EQ("Node 1:1 can't be a root node.\n", error_result);
}

TEST_F(InterpreterTest, BadAst) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddIntegerLiteral(1, true);

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::ANALYSIS_ERROR, context->result);

  std::string error_result = context->error_stream.str();
  ASSERT_EQ("Pending AST nodes for execution context 1.\n", error_result);
}

TEST_F(InterpreterTest, VariableDefinition) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("foo", builder.TypeUint64(), NullNode, false, true);
  builder.AddVariableDeclaration("bar", builder.TypeInt64(), builder.AddIntegerLiteral(1, true),
                                 true, true);
  builder.AddVariableDeclaration("x", builder.TypeUint64(), builder.AddIntegerLiteral(10, false),
                                 true, true);

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().DumpExecutionContext(context->id));
  Finish(kDump);

  ASSERT_FALSE(last_result_partial());
  ASSERT_EQ(results().size(), static_cast<size_t>(3));
  ASSERT_EQ(results()[0], "var foo: uint64\n");
  ASSERT_EQ(results()[1], "const bar: int64 = -1\n");
  ASSERT_EQ(results()[2], "const x: uint64 = 10\n");
}

TEST_F(InterpreterTest, BuiltinTypes) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("b", builder.TypeBool(), NullNode, false, true);
  builder.AddVariableDeclaration("c", builder.TypeChar(), NullNode, false, true);
  builder.AddVariableDeclaration("s", builder.TypeString(), NullNode, false, true);
  builder.AddVariableDeclaration("i8", builder.TypeInt8(), NullNode, false, true);
  builder.AddVariableDeclaration("u8", builder.TypeUint8(), NullNode, false, true);
  builder.AddVariableDeclaration("i16", builder.TypeInt16(), NullNode, false, true);
  builder.AddVariableDeclaration("u16", builder.TypeUint16(), NullNode, false, true);
  builder.AddVariableDeclaration("i32", builder.TypeInt32(), NullNode, false, true);
  builder.AddVariableDeclaration("u32", builder.TypeUint32(), NullNode, false, true);
  builder.AddVariableDeclaration("i64", builder.TypeInt64(), NullNode, false, true);
  builder.AddVariableDeclaration("u64", builder.TypeUint64(), NullNode, false, true);
  builder.AddVariableDeclaration("big_int", builder.TypeInteger(), NullNode, false, true);
  builder.AddVariableDeclaration("f32", builder.TypeFloat32(), NullNode, false, true);
  builder.AddVariableDeclaration("f64", builder.TypeFloat64(), NullNode, false, true);

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().DumpExecutionContext(context->id));
  Finish(kDump);

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
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("foo", builder.TypeUint64(), builder.AddIntegerLiteral(1, false),
                                 false, true);
  builder.AddVariableDeclaration("bar", builder.TypeUint64(), builder.AddIntegerLiteral(10, false),
                                 false, true);
  builder.AddVariableDeclaration("groucho", builder.TypeString(),
                                 builder.AddStringLiteral("A Marx brother"), false, true);

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));
  LoadGlobal("foo");
  LoadGlobal("bar");
  LoadGlobal("groucho");
  LoadGlobal("x");
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context->GetResult());

  llcpp::fuchsia::shell::Node* foo = GetGlobal("foo");
  ASSERT_TRUE(foo->is_integer_literal());
  ASSERT_FALSE(foo->integer_literal().negative);
  ASSERT_EQ(foo->integer_literal().absolute_value.count(), static_cast<size_t>(1));
  ASSERT_EQ(foo->integer_literal().absolute_value[0], 1U);

  llcpp::fuchsia::shell::Node* bar = GetGlobal("bar");
  ASSERT_TRUE(bar->is_integer_literal());
  ASSERT_FALSE(bar->integer_literal().negative);
  ASSERT_EQ(bar->integer_literal().absolute_value.count(), static_cast<size_t>(1));
  ASSERT_EQ(bar->integer_literal().absolute_value[0], 10U);

  llcpp::fuchsia::shell::Node* groucho = GetGlobal("groucho");
  ASSERT_TRUE(groucho->is_string_literal());
  ASSERT_EQ("A Marx brother",
            std::string(groucho->string_literal().data(), groucho->string_literal().size()));

  llcpp::fuchsia::shell::Node* x = GetGlobal("x");
  ASSERT_EQ(x, nullptr);
}

TEST_F(InterpreterTest, VariableNoType) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("bar", builder.TypeUndef(), NullNode, false, true);
  builder.AddVariableDeclaration("foo", builder.TypeUndef(), builder.AddIntegerLiteral(1, false),
                                 false, true);

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));

  Finish(kExecute);
  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::ANALYSIS_ERROR, context->result);

  std::string error_result = context->error_stream.str();
  ASSERT_EQ(
      "node 1:1 Type not defined.\n"
      "node 1:3 Type not defined.\n",
      error_result);
}

TEST_F(InterpreterTest, VariableTypeNotImplemented) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("bar", builder.TypeInteger(), NullNode, false, true);

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));

  Finish(kExecute);
  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::ANALYSIS_ERROR, context->result);

  std::string error_result = context->error_stream.str();
  ASSERT_EQ("node 1:1 Can't create variable 'bar' of type integer (not implemented yet).\n",
            error_result);
}

TEST_F(InterpreterTest, VariableDefinedTwice) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("bar", builder.TypeUint64(), NullNode, false, true);
  builder.AddVariableDeclaration("bar", builder.TypeUint64(), NullNode, false, true);

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));

  Finish(kExecute);
  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::ANALYSIS_ERROR, context->result);

  std::string error_result = context->error_stream.str();
  ASSERT_EQ(
      "node 1:2 Variable 'bar' already defined.\n"
      "node 1:1 First definition.\n",
      error_result);
}

TEST_F(InterpreterTest, BadIntegerLiterals) {
  constexpr uint64_t kFileId = 1;
  InterpreterTestContext* context = CreateContext();
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("i8p", builder.TypeInt8(), builder.AddIntegerLiteral(0x80, false),
                                 true, true);
  builder.AddVariableDeclaration("i8n", builder.TypeInt8(), builder.AddIntegerLiteral(0x81, true),
                                 true, true);
  builder.AddVariableDeclaration("u8p", builder.TypeUint8(),
                                 builder.AddIntegerLiteral(0x100, false), true, true);
  builder.AddVariableDeclaration("u8n", builder.TypeUint8(), builder.AddIntegerLiteral(1, true),
                                 true, true);

  builder.AddVariableDeclaration("i16p", builder.TypeInt16(),
                                 builder.AddIntegerLiteral(0x8000, false), true, true);
  builder.AddVariableDeclaration("i16n", builder.TypeInt16(),
                                 builder.AddIntegerLiteral(0x8001, true), true, true);
  builder.AddVariableDeclaration("u16p", builder.TypeUint16(),
                                 builder.AddIntegerLiteral(0x10000, false), true, true);
  builder.AddVariableDeclaration("u16n", builder.TypeUint16(), builder.AddIntegerLiteral(1, true),
                                 true, true);

  builder.AddVariableDeclaration("i32p", builder.TypeInt32(),
                                 builder.AddIntegerLiteral(0x80000000, false), true, true);
  builder.AddVariableDeclaration("i32n", builder.TypeInt32(),
                                 builder.AddIntegerLiteral(0x80000001, true), true, true);
  builder.AddVariableDeclaration("u32p", builder.TypeUint32(),
                                 builder.AddIntegerLiteral(0x100000000L, false), true, true);
  builder.AddVariableDeclaration("u32n", builder.TypeUint32(), builder.AddIntegerLiteral(1, true),
                                 true, true);

  builder.AddVariableDeclaration("i64p", builder.TypeInt64(),
                                 builder.AddIntegerLiteral(0x8000000000000000UL, false), true,
                                 true);
  builder.AddVariableDeclaration("i64n", builder.TypeInt64(),
                                 builder.AddIntegerLiteral(0x8000000000000001UL, true), true, true);
  builder.AddVariableDeclaration("u64n", builder.TypeUint64(), builder.AddIntegerLiteral(1, true),
                                 true, true);

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));

  Finish(kExecute);
  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::ANALYSIS_ERROR, context->result);

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
  ASSERT_CALL_OK(shell().CreateExecutionContext(context->id));

  shell::console::AstBuilder builder(kFileId);
  builder.AddVariableDeclaration("i8p", builder.TypeInt8(), builder.AddIntegerLiteral(0x7f, false),
                                 true, true);
  builder.AddVariableDeclaration("i8n", builder.TypeInt8(), builder.AddIntegerLiteral(0x80, true),
                                 true, true);
  builder.AddVariableDeclaration("u8p", builder.TypeUint8(), builder.AddIntegerLiteral(0xff, false),
                                 true, true);
  builder.AddVariableDeclaration("u8n", builder.TypeUint8(), builder.AddIntegerLiteral(0, false),
                                 true, true);

  builder.AddVariableDeclaration("i16p", builder.TypeInt16(),
                                 builder.AddIntegerLiteral(0x7fff, false), true, true);
  builder.AddVariableDeclaration("i16n", builder.TypeInt16(),
                                 builder.AddIntegerLiteral(0x8000, true), true, true);
  builder.AddVariableDeclaration("u16p", builder.TypeUint16(),
                                 builder.AddIntegerLiteral(0xffff, false), true, true);
  builder.AddVariableDeclaration("u16n", builder.TypeUint16(), builder.AddIntegerLiteral(0, false),
                                 true, true);

  builder.AddVariableDeclaration("i32p", builder.TypeInt32(),
                                 builder.AddIntegerLiteral(0x7fffffff, false), true, true);
  builder.AddVariableDeclaration("i32n", builder.TypeInt32(),
                                 builder.AddIntegerLiteral(0x80000000, true), true, true);
  builder.AddVariableDeclaration("u32p", builder.TypeUint32(),
                                 builder.AddIntegerLiteral(0xffffffffL, false), true, true);
  builder.AddVariableDeclaration("u32n", builder.TypeUint32(), builder.AddIntegerLiteral(0, false),
                                 true, true);

  builder.AddVariableDeclaration("i64p", builder.TypeInt64(),
                                 builder.AddIntegerLiteral(0x7fffffffffffffffUL, false), true,
                                 true);
  builder.AddVariableDeclaration("i64n", builder.TypeInt64(),
                                 builder.AddIntegerLiteral(0x8000000000000000UL, true), true, true);
  builder.AddVariableDeclaration("u64p", builder.TypeUint64(),
                                 builder.AddIntegerLiteral(0xffffffffffffffffUL, false), true,
                                 true);
  builder.AddVariableDeclaration("u64n", builder.TypeUint64(), builder.AddIntegerLiteral(0, false),
                                 true, true);

  ASSERT_CALL_OK(shell().AddNodes(context->id, builder.DefsAsVectorView()));
  ASSERT_CALL_OK(shell().ExecuteExecutionContext(context->id));
  Finish(kExecute);

  ASSERT_EQ(llcpp::fuchsia::shell::ExecuteResult::OK, context->GetResult());
}
