// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_node_console.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/console/async_output_buffer_test_util.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

namespace {

// Test harness for tests for FormatValueForConsole that may be async.
class FormatValueConsoleTest : public TestWithLoop {
 public:
  FormatValueConsoleTest() : eval_context_(fxl::MakeRefCounted<MockEvalContext>()) {}

  fxl::RefPtr<MockEvalContext>& eval_context() { return eval_context_; }
  MockSymbolDataProvider* provider() { return eval_context_->data_provider(); }

  // Synchronously calls FormatExprValue, returning the result.
  std::string SyncFormatValue(const ExprValue& value, const ConsoleFormatOptions& opts) {
    return LoopUntilAsyncOutputBufferComplete(FormatValueForConsole(value, opts, eval_context_))
        .AsString();
  }

 private:
  fxl::RefPtr<MockEvalContext> eval_context_;
};

void FillBaseTypeNode(const std::string& type_name, const std::string& description,
                      FormatNode* node) {
  node->set_state(FormatNode::kDescribed);
  node->set_type(type_name);
  node->set_description_kind(FormatNode::kBaseType);
  node->set_description(description);
}

}  // namespace

TEST(FormatNodeConsole, SimpleValue) {
  FormatNode node;
  FillBaseTypeNode("int", "54", &node);
  ConsoleFormatOptions options;

  // Bare value.
  OutputBuffer out = FormatNodeForConsole(node, options);
  EXPECT_EQ("kNormal \"54\"", out.GetDebugString());

  // Bare value with types forced on.
  ConsoleFormatOptions type_options;
  type_options.verbosity = ConsoleFormatOptions::Verbosity::kAllTypes;
  out = FormatNodeForConsole(node, type_options);
  EXPECT_EQ(R"(kComment "(int) ", kNormal "54")", out.GetDebugString());

  // Named value.
  node.set_name("foo");
  out = FormatNodeForConsole(node, options);
  EXPECT_EQ(R"(kVariable "foo", kNormal " = 54")", out.GetDebugString());

  // Named value with types forced on.
  out = FormatNodeForConsole(node, type_options);
  EXPECT_EQ(R"(kComment "(int) ", kVariable "foo", kNormal " = 54")", out.GetDebugString());
}

TEST(FormatNodeConsole, Collection) {
  FormatNode node;
  node.set_state(FormatNode::kDescribed);
  node.set_type("MyClass");
  node.set_description_kind(FormatNode::kCollection);
  node.set_description("This description is not displayed for a collection.");

  ConsoleFormatOptions options;

  // Empty collection.
  OutputBuffer out = FormatNodeForConsole(node, options);
  EXPECT_EQ("{}", out.AsString());

  // Add some children.
  auto child = std::make_unique<FormatNode>("a");
  FillBaseTypeNode("int", "42", child.get());
  node.children().push_back(std::move(child));

  child = std::make_unique<FormatNode>("b");
  FillBaseTypeNode("double", "3.14159", child.get());
  node.children().push_back(std::move(child));

  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("{a = 42, b = 3.14159}", out.AsString());

  // With types forced.
  ConsoleFormatOptions type_options;
  type_options.verbosity = ConsoleFormatOptions::Verbosity::kAllTypes;
  out = FormatNodeForConsole(node, type_options);
  EXPECT_EQ("(MyClass) {(int) a = 42, (double) b = 3.14159}", out.AsString());

  // Add a very long base class name.
  auto base_node =
      std::make_unique<FormatNode>("This_is::a::VeryLongBaseClass<which, should, be, elided>");
  base_node->set_child_kind(FormatNode::kBaseClass);
  base_node->set_description_kind(FormatNode::kCollection);
  base_node->set_state(FormatNode::kDescribed);
  node.children().insert(node.children().begin(), std::move(base_node));

  // Test with no eliding.
  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("{This_is::a::VeryLongBaseClass<which, should, be, elided> = {}, a = 42, b = 3.14159}",
            out.AsString());

  // With eliding.
  ConsoleFormatOptions elide_options;
  elide_options.verbosity = ConsoleFormatOptions::Verbosity::kMinimal;
  out = FormatNodeForConsole(node, elide_options);
  EXPECT_EQ("{This_is::a::VeryLong… = {}, a = 42, b = 3.14159}", out.AsString());
}

TEST(FormatNodeConsole, Array) {
  FormatNode node;
  node.set_state(FormatNode::kDescribed);
  node.set_type("int[2]");
  node.set_description_kind(FormatNode::kArray);
  node.set_description("This description is not displayed for arrays.");

  // Empty array.
  ConsoleFormatOptions options;
  OutputBuffer out = FormatNodeForConsole(node, options);
  EXPECT_EQ("{}", out.AsString());

  // Add some children.
  auto child = std::make_unique<FormatNode>("[0]");
  FillBaseTypeNode("int", "42", child.get());
  node.children().push_back(std::move(child));

  child = std::make_unique<FormatNode>("[1]");
  FillBaseTypeNode("int", "137", child.get());
  node.children().push_back(std::move(child));

  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("{42, 137}", out.AsString());

  // Truncated array.
  node.children().push_back(std::make_unique<FormatNode>("..."));
  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("{42, 137, ...}", out.AsString());

  // With types forced on.
  ConsoleFormatOptions type_options;
  type_options.verbosity = ConsoleFormatOptions::Verbosity::kAllTypes;
  out = FormatNodeForConsole(node, type_options);
  EXPECT_EQ("(int[2]) {42, 137, ...}", out.AsString());
}

TEST(FormatNodeConsole, Pointer) {
  FormatNode node;
  node.set_state(FormatNode::kDescribed);
  node.set_type("int*");
  node.set_description_kind(FormatNode::kPointer);
  node.set_description("0x12345678");

  // Pointed to value. Don't fill it in yet.
  auto child = std::make_unique<FormatNode>("*");
  child->set_state(FormatNode::kUnevaluated);
  node.children().push_back(std::move(child));

  // Print the bare pointer.
  ConsoleFormatOptions options;
  OutputBuffer out = FormatNodeForConsole(node, options);
  EXPECT_EQ("(*)0x12345678", out.AsString());

  // Fill the pointed-to value.
  FillBaseTypeNode("int", "42", node.children()[0].get());

  // Print with the pointed-to value.
  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("(*)0x12345678 🡺 42", out.AsString());

  // Print with type information. Should only show on the pointer and not be duplicated on the
  // pointed-to value.
  ConsoleFormatOptions type_options;
  type_options.verbosity = ConsoleFormatOptions::Verbosity::kAllTypes;
  out = FormatNodeForConsole(node, type_options);
  EXPECT_EQ("(int*) 0x12345678 🡺 42", out.AsString());

  // Add a name.
  node.set_name("a");
  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("a = (*)0x12345678 🡺 42", out.AsString());
  out = FormatNodeForConsole(node, type_options);
  EXPECT_EQ("(int*) a = 0x12345678 🡺 42", out.AsString());

  // Report an error for the pointed-to value, it should now be omitted.
  node.children()[0]->set_err(Err("Bad pointer"));
  node.children()[0]->set_description(std::string());
  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("a = (*)0x12345678", out.AsString());
}

TEST(FormatNodeConsole, Reference) {
  FormatNode node;
  node.set_state(FormatNode::kDescribed);
  node.set_type("int&");
  node.set_description_kind(FormatNode::kReference);
  node.set_description("0x12345678");

  // Pointed to value. Don't fill it in yet.
  auto child = std::make_unique<FormatNode>();
  child->set_state(FormatNode::kUnevaluated);
  node.children().push_back(std::move(child));

  // Print the bare reference.
  ConsoleFormatOptions options;
  OutputBuffer out = FormatNodeForConsole(node, options);
  EXPECT_EQ("(&)0x12345678", out.AsString());

  // Fill the pointed-to value.
  FillBaseTypeNode("int", "42", node.children()[0].get());

  // Print with the pointed-to value.
  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("42", out.AsString());

  // Print with type information. Should only show on the pointer and not be duplicated on the
  // pointed-to value.
  ConsoleFormatOptions type_options;
  type_options.verbosity = ConsoleFormatOptions::Verbosity::kAllTypes;
  out = FormatNodeForConsole(node, type_options);
  EXPECT_EQ("(int&) 42", out.AsString());

  // Add a name.
  node.set_name("a");
  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("a = 42", out.AsString());
  out = FormatNodeForConsole(node, type_options);
  EXPECT_EQ("(int&) a = 42", out.AsString());
}

TEST_F(FormatValueConsoleTest, SimpleSync) {
  ConsoleFormatOptions opts;

  // Basic synchronous number.
  ExprValue val_int16(fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 2, "short"),
                      {0xe0, 0xf0});
  EXPECT_EQ("-3872", SyncFormatValue(val_int16, opts));
}

// Tests collections and nested references.
TEST_F(FormatValueConsoleTest, Collection) {
  ConsoleFormatOptions opts;
  opts.num_format = ConsoleFormatOptions::NumFormat::kHex;

  auto int32_type = MakeInt32Type();

  // Make an int reference. Reference type printing combined with struct type
  // printing can get complicated.
  auto int_ref =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kReferenceType, LazySymbol(int32_type));

  // The references point to this data.
  constexpr uint64_t kAddress = 0x1100;
  provider()->AddMemory(kAddress, {0x12, 0, 0, 0});

  // Struct with two values, an int and a int&, and a pair of two of those
  // structs.
  auto foo =
      MakeCollectionType(DwarfTag::kStructureType, "Foo", {{"a", int32_type}, {"b", int_ref}});
  auto pair =
      MakeCollectionType(DwarfTag::kStructureType, "Pair", {{"first", foo}, {"second", foo}});

  ExprValue pair_value(pair, {0x11, 0x00, 0x11, 0x00,                            // (int32) a
                              0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,    // (int32&) b
                              0x33, 0x00, 0x33, 0x00,                            // (int32) a
                              0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});  // (int32&) b
  EXPECT_EQ("{first = {a = 0x110011, b = 0x12}, second = {a = 0x330033, b = 0x12}}",
            SyncFormatValue(pair_value, opts));
}

// Tests that maximum recursion depth as well as the maximum pointer dereference depth.
TEST_F(FormatValueConsoleTest, NestingLimits) {
  // This creates the followint structure:
  //
  //   int final = 12;  // @ address kIntAddress.
  //
  //   struct A {  // @ address kIntPtrAddress.
  //     int* a = &final;
  //   } int_ptr;
  //
  //   struct B {
  //     A* b = &int_ptr;
  //   };
  //
  //   struct C {
  //     IntPtrPtr c;
  //   };
  auto int32_type = MakeInt32Type();

  // An integer at this location points to "12".
  constexpr uint64_t kIntAddress = 0x1100;
  provider()->AddMemory(kIntAddress, {12, 0, 0, 0});

  auto int_ptr_type =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, LazySymbol(int32_type));

  // Structure contains one member which is a pointer to the integer.
  auto a_type = MakeCollectionType(DwarfTag::kStructureType, "A", {{"a", int_ptr_type}});
  constexpr uint64_t kIntPtrAddress = 0x2200;
  provider()->AddMemory(kIntPtrAddress, {0, 0x11, 0, 0, 0, 0, 0, 0});  // kIntAddress.

  // Declare A* type.
  auto a_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, LazySymbol(a_type));

  // This structure contains one member that's a pointer to the previous structure.
  auto b_type = MakeCollectionType(DwarfTag::kStructureType, "B", {{"b", a_ptr_type}});

  // This structure contains one member that's the previous structure.
  auto c_type = MakeCollectionType(DwarfTag::kStructureType, "C", {{"c", b_type}});

  // The contents of C (value is the pointer to A).
  ExprValue c_value(c_type, {0, 0x22, 0, 0, 0, 0, 0, 0});

  // Expand different levels of pointers but allow everything else.
  ConsoleFormatOptions opts;
  opts.pointer_expand_depth = 0;
  opts.max_depth = 1000;
  EXPECT_EQ("{c = {b = (*)0x2200}}", SyncFormatValue(c_value, opts));
  opts.pointer_expand_depth = 1;
  EXPECT_EQ("{c = {b = (*)0x2200 🡺 {a = (*)0x1100}}}", SyncFormatValue(c_value, opts));
  opts.pointer_expand_depth = 2;
  EXPECT_EQ("{c = {b = (*)0x2200 🡺 {a = (*)0x1100 🡺 12}}}", SyncFormatValue(c_value, opts));

  // Now test max recursion levels (independent of pointers).
  opts.max_depth = 0;
  EXPECT_EQ("…", SyncFormatValue(c_value, opts));
  opts.max_depth = 1;
  EXPECT_EQ("{c = …}", SyncFormatValue(c_value, opts));
  opts.max_depth = 2;
  EXPECT_EQ("{c = {b = …}}", SyncFormatValue(c_value, opts));
  opts.max_depth = 3;
  EXPECT_EQ("{c = {b = (*)0x2200}}", SyncFormatValue(c_value, opts));
  opts.max_depth = 4;
  EXPECT_EQ("{c = {b = (*)0x2200 🡺 {a = …}}}", SyncFormatValue(c_value, opts));
  opts.max_depth = 5;
  EXPECT_EQ("{c = {b = (*)0x2200 🡺 {a = (*)0x1100}}}", SyncFormatValue(c_value, opts));
  opts.max_depth = 6;
  EXPECT_EQ("{c = {b = (*)0x2200 🡺 {a = (*)0x1100 🡺 12}}}", SyncFormatValue(c_value, opts));
}

}  // namespace zxdb
