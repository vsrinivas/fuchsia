// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_node_console.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/console/async_output_buffer_test_util.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/compile_unit.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/namespace.h"
#include "src/developer/debug/zxdb/symbols/symbol_test_parent_setter.h"
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
  std::string SyncFormatValue(const ExprValue& value, const ConsoleFormatOptions& opts,
                              const std::string& name = std::string()) {
    return LoopUntilAsyncOutputBufferComplete(
               FormatValueForConsole(value, opts, eval_context_, name))
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

  // Force types on when there is no type shouldn't show anything.
  FormatNode err_node("foo");
  err_node.SetDescribedError(Err("Error."));
  out = FormatNodeForConsole(err_node, type_options);
  EXPECT_EQ(R"(kVariable "foo", kNormal " = ", kComment "<Error.>")", out.GetDebugString());
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
  auto base_node = std::make_unique<FormatNode>(
      "This_is::a::VeryLongBaseClass<which, should, be, elided, abcdefghijklmnopqrstuvwxyz>");
  base_node->set_child_kind(FormatNode::kBaseClass);
  base_node->set_description_kind(FormatNode::kCollection);
  base_node->set_state(FormatNode::kDescribed);
  node.children().insert(node.children().begin(), std::move(base_node));

  // Test with no eliding.
  out = FormatNodeForConsole(node, options);
  EXPECT_EQ(
      "{This_is::a::VeryLongBaseClass<which, should, be, elided, abcdefghijklmnopqrstuvwxyz> = {}, "
      "a = 42, b = 3.14159}",
      out.AsString());

  // With eliding.
  ConsoleFormatOptions elide_options;
  elide_options.verbosity = ConsoleFormatOptions::Verbosity::kMinimal;
  out = FormatNodeForConsole(node, elide_options);
  EXPECT_EQ("{This_is::a::VeryLongBaseC… = {}, a = 42, b = 3.14159}", out.AsString());

  // Expanded mode eliding shows more.
  elide_options.wrapping = ConsoleFormatOptions::Wrapping::kExpanded;
  out = FormatNodeForConsole(node, elide_options);
  EXPECT_EQ(
      "{\n"
      "  This_is::a::VeryLongBaseClass<which, should, be, e… = {}\n"
      "  a = 42\n"
      "  b = 3.14159\n"
      "}",
      out.AsString());
}

TEST(FormatValueConsole, Wrapper) {
  FormatNode node;
  node.set_state(FormatNode::kDescribed);
  node.set_type("std::optional<int>");
  node.set_description_kind(FormatNode::kWrapper);
  node.set_description("std::optional");
  node.set_wrapper_prefix("(");
  node.set_wrapper_suffix(")");

  // Simple integer child.
  auto int_node = std::make_unique<FormatNode>();
  FillBaseTypeNode("int", "54", int_node.get());
  node.children().push_back(std::move(int_node));

  ConsoleFormatOptions options;
  EXPECT_EQ("std::optional(54)", FormatNodeForConsole(node, options).AsString());

  // With type info. This is a bit weird, it would be nice if the description expanded to the full
  // type name to avoid duplication.
  ConsoleFormatOptions type_options;
  type_options.verbosity = ConsoleFormatOptions::Verbosity::kAllTypes;
  EXPECT_EQ("(std::optional<int>) std::optional((int) 54)",
            FormatNodeForConsole(node, type_options).AsString());

  // Test with a child that's a collection. This is the child of the collection.
  auto collection_child = std::make_unique<FormatNode>("child");
  collection_child->set_description_kind(FormatNode::kString);
  collection_child->set_state(FormatNode::kDescribed);
  collection_child->set_description("\"string value\"");

  // The collection itself.
  auto collection_node = std::make_unique<FormatNode>();
  collection_node->set_description_kind(FormatNode::kCollection);
  collection_node->set_state(FormatNode::kDescribed);
  collection_node->children().push_back(std::move(collection_child));
  node.children()[0] = std::move(collection_node);

  ConsoleFormatOptions expanded_options;
  expanded_options.wrapping = ConsoleFormatOptions::Wrapping::kExpanded;
  EXPECT_EQ(
      "std::optional({\n"
      "  child = \"string value\"\n"
      "})",
      FormatNodeForConsole(node, expanded_options).AsString());
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
  child->set_child_kind(FormatNode::kPointerExpansion);
  node.children().push_back(std::move(child));

  // Print the bare pointer.
  ConsoleFormatOptions options;
  OutputBuffer out = FormatNodeForConsole(node, options);
  EXPECT_EQ("(*)0x12345678", out.AsString());

  // Fill the pointed-to value.
  FillBaseTypeNode("int", "42", node.children()[0].get());

  // Print with the pointed-to value.
  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("(*)0x12345678 " + GetRightArrow() + " 42", out.AsString());

  // Print with type information. Should only show on the pointer and not be duplicated on the
  // pointed-to value.
  ConsoleFormatOptions type_options;
  type_options.verbosity = ConsoleFormatOptions::Verbosity::kAllTypes;
  out = FormatNodeForConsole(node, type_options);
  EXPECT_EQ("(int*) 0x12345678 " + GetRightArrow() + " 42", out.AsString());

  // Add a name.
  node.set_name("a");
  out = FormatNodeForConsole(node, options);
  EXPECT_EQ("a = (*)0x12345678 " + GetRightArrow() + " 42", out.AsString());
  out = FormatNodeForConsole(node, type_options);
  EXPECT_EQ("(int*) a = 0x12345678 " + GetRightArrow() + " 42", out.AsString());

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
  child->set_child_kind(FormatNode::kPointerExpansion);
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

  // Make an int reference. Reference type printing combined with struct type printing can get
  // complicated.
  auto int_ref = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kReferenceType, int32_type);

  // The references point to this data.
  constexpr uint64_t kAddress = 0x1100;
  provider()->AddMemory(kAddress, {0x12, 0, 0, 0});

  // Struct with two values, an int and a int&, and a pair of two of those structs.
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
  // This creates the following structure:
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

  auto int_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, int32_type);

  // Structure contains one member which is a pointer to the integer.
  auto a_type = MakeCollectionType(DwarfTag::kStructureType, "A", {{"a", int_ptr_type}});
  constexpr uint64_t kIntPtrAddress = 0x2200;
  provider()->AddMemory(kIntPtrAddress, {0, 0x11, 0, 0, 0, 0, 0, 0});  // kIntAddress.

  // Declare A* type.
  auto a_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, a_type);

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
  EXPECT_EQ("{c = {b = (*)0x2200 " + GetRightArrow() + " {a = (*)0x1100}}}",
            SyncFormatValue(c_value, opts));
  opts.pointer_expand_depth = 2;
  EXPECT_EQ(
      "{c = {b = (*)0x2200 " + GetRightArrow() + " {a = (*)0x1100 " + GetRightArrow() + " 12}}}",
      SyncFormatValue(c_value, opts));

  // Now test max recursion levels (independent of pointers).
  opts.max_depth = 0;
  EXPECT_EQ("…", SyncFormatValue(c_value, opts));
  opts.max_depth = 1;
  EXPECT_EQ("{…}", SyncFormatValue(c_value, opts));
  opts.max_depth = 2;
  EXPECT_EQ("{c = {…}}", SyncFormatValue(c_value, opts));
  opts.max_depth = 3;
  EXPECT_EQ("{c = {b = (*)0x2200}}", SyncFormatValue(c_value, opts));
  opts.max_depth = 4;
  EXPECT_EQ("{c = {b = (*)0x2200 " + GetRightArrow() + " {…}}}", SyncFormatValue(c_value, opts));
  opts.max_depth = 5;
  EXPECT_EQ("{c = {b = (*)0x2200 " + GetRightArrow() + " {a = (*)0x1100}}}",
            SyncFormatValue(c_value, opts));
  opts.max_depth = 6;
  EXPECT_EQ(
      "{c = {b = (*)0x2200 " + GetRightArrow() + " {a = (*)0x1100 " + GetRightArrow() + " 12}}}",
      SyncFormatValue(c_value, opts));

  // Tests max recursion for the expanded case. The elided structs should not be expanded.
  opts.max_depth = 2;
  opts.wrapping = ConsoleFormatOptions::Wrapping::kExpanded;
  EXPECT_EQ(
      "{\n"
      "  c = {…}\n"
      "}",
      SyncFormatValue(c_value, opts));
}

TEST_F(FormatValueConsoleTest, Wrapping) {
  // struct Nested {
  //   int32 variable1;
  //   int32 variable2;
  // };
  auto int32_type = MakeInt32Type();
  auto nested_collection = MakeCollectionType(
      DwarfTag::kStructureType, "Nested", {{"variable1", int32_type}, {"variable2", int32_type}});

  // nested_ptr = &Nested{ variable1 = 12, variable2 = 34 };
  auto nested_ptr = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, nested_collection);
  constexpr uint64_t kNestedAddress = 0x1100;
  provider()->AddMemory(kNestedAddress, {12, 0, 0, 0, 34, 0, 0, 0});

  // struct Empty {};
  auto empty_collection = MakeCollectionType(DwarfTag::kStructureType, "Empty", {});

  // struct Outer {
  //   Nested* nested;
  //   Empty empty;
  // };
  auto outer_collection = MakeCollectionType(DwarfTag::kStructureType, "Outer",
                                             {{"nested", nested_ptr}, {"empty", empty_collection}});

  // outer_value = Outer{ nested = nested_ptr, empty = Empty{} }
  ExprValue outer_value(outer_collection, {0, 0x11, 0, 0, 0, 0, 0, 0});

  // First do non-expanded mode.
  ConsoleFormatOptions opts;
  opts.pointer_expand_depth = 1000;
  opts.max_depth = 1000;
  opts.wrapping = ConsoleFormatOptions::Wrapping::kNone;

  EXPECT_EQ(
      "{nested = (*)0x1100 " + GetRightArrow() + " {variable1 = 12, variable2 = 34}, empty = {}}",
      SyncFormatValue(outer_value, opts));

  // Expanded mode.
  opts.wrapping = ConsoleFormatOptions::Wrapping::kExpanded;
  EXPECT_EQ(
      "{\n"
      "  nested = (*)0x1100 " +
          GetRightArrow() +
          " {\n"
          "    variable1 = 12\n"
          "    variable2 = 34\n"
          "  }\n"
          "  empty = {}\n"
          "}",
      SyncFormatValue(outer_value, opts));

  // Smart mode. First give it a really wide limit, everything should be one line.
  opts.wrapping = ConsoleFormatOptions::Wrapping::kSmart;
  opts.smart_indent_cols = 1000;
  EXPECT_EQ(
      "{nested = (*)0x1100 " + GetRightArrow() + " {variable1 = 12, variable2 = 34}, empty = {}}",
      SyncFormatValue(outer_value, opts));

  // Super narrow limit should force an expansion of everything.
  opts.smart_indent_cols = 2;
  EXPECT_EQ(
      "{\n"
      "  nested = (*)0x1100 " +
          GetRightArrow() +
          " {\n"
          "    variable1 = 12\n"
          "    variable2 = 34\n"
          "  }\n"
          "  empty = {}\n"
          "}",
      SyncFormatValue(outer_value, opts));

  // Intermediate state where "nested" fits in one line but the outer part doesn't.
  opts.smart_indent_cols = 55;
  EXPECT_EQ(
      "{\n"
      "  nested = (*)0x1100 " +
          GetRightArrow() +
          " {variable1 = 12, variable2 = 34}\n"  // 55-char line.
          "  empty = {}\n"
          "}",
      SyncFormatValue(outer_value, opts));

  // Test the boundary condition one below the previous.
  opts.smart_indent_cols = 54;
  EXPECT_EQ(
      "{\n"
      "  nested = (*)0x1100 " +
          GetRightArrow() +
          " {\n"
          "    variable1 = 12\n"
          "    variable2 = 34\n"
          "  }\n"
          "  empty = {}\n"
          "}",
      SyncFormatValue(outer_value, opts));
}

// Tests the naming of Rust collections since we have complicated rules depending on verbosity.
TEST_F(FormatValueConsoleTest, RustCollectionName) {
  auto int32_type = MakeInt32Type();  // Won't be named like Rust's ints but doesn't matter.
  SymbolTestParentSetter int32_parent(int32_type, MakeRustUnit());  // Mark as Rust.

  // Namespace for the structs. Making the root compilation unit a Rust one will mark all types in
  // this unit as Rust.
  auto ns = fxl::MakeRefCounted<Namespace>("my_ns");
  SymbolTestParentSetter ns_parent(ns, MakeRustUnit());

  // Make a simple type in the namespace with no templates.
  auto simple_type = MakeCollectionType(DwarfTag::kStructureType, "MyStruct", {});
  SymbolTestParentSetter simple_type_parent(simple_type, ns);

  ExprValue simple_value(simple_type, {});

  // Minimal verbosity shows only the last name. Note that empty Rust structs don't use {}.
  ConsoleFormatOptions minimal;
  minimal.verbosity = ConsoleFormatOptions::Verbosity::kMinimal;
  EXPECT_EQ("MyStruct", SyncFormatValue(simple_value, minimal));

  // Higher verbosity shows the namespace.
  ConsoleFormatOptions medium;
  medium.verbosity = ConsoleFormatOptions::Verbosity::kMedium;
  EXPECT_EQ("my_ns::MyStruct", SyncFormatValue(simple_value, medium));

  // Full type information should be the same, it shouldn't duplicate the type.
  ConsoleFormatOptions all_types;
  all_types.verbosity = ConsoleFormatOptions::Verbosity::kAllTypes;
  EXPECT_EQ("my_ns::MyStruct", SyncFormatValue(simple_value, all_types));

  // Make a long template name for a collection with a member.
  auto template_type = MakeCollectionType(
      DwarfTag::kStructureType,
      "HashMap<alloc::string::String, &str, std::collections::hash::map::RandomState>",
      {{"a", int32_type}});
  SymbolTestParentSetter template_type_parent(template_type, ns);
  ExprValue template_value(template_type, {123, 0, 0, 0});

  // Minimal verbosity elides the template and omits the namespace.
  EXPECT_EQ("HashMap<alloc::string::String, &s…>{a: 123}",
            SyncFormatValue(template_value, minimal));

  // Minimal verbosity in expanded mode shows more but still elides.
  ConsoleFormatOptions minimal_expanded = minimal;
  minimal_expanded.wrapping = ConsoleFormatOptions::Wrapping::kExpanded;
  minimal.verbosity = ConsoleFormatOptions::Verbosity::kMinimal;
  EXPECT_EQ(
      "HashMap<alloc::string::String, &str, std::collections::has…>{\n"
      "  a: 123\n"
      "}",
      SyncFormatValue(template_value, minimal_expanded));

  // Medium verbosity shows everything.
  EXPECT_EQ(
      "my_ns::HashMap<alloc::string::String, &str, std::collections::hash::map::RandomState>{a: "
      "123}",
      SyncFormatValue(template_value, medium));

  // Assign a name to a toplevel thing that's printed. This will print using " = " even in Rust
  // to match how local variables are printed.
  EXPECT_EQ("some_value = MyStruct", SyncFormatValue(simple_value, minimal, "some_value"));
}

// Tests Rust tuples and tuple structs.
TEST_F(FormatValueConsoleTest, RustTuple) {
  auto int32_type = MakeInt32Type();  // Won't be named like Rust's ints but doesn't matter.
  SymbolTestParentSetter int32_type_parent(int32_type, MakeRustUnit());
  auto tuple_type = MakeTestRustTuple("(int32_t, int32_t)", {int32_type, int32_type});
  auto tuple_struct_type = MakeTestRustTuple("MyTupleStruct", {int32_type, int32_type});

  // Data encoding 2 32-bit ints: 1 & 2.
  std::vector<uint8_t> data{1, 0, 0, 0, 2, 0, 0, 0};

  ExprValue tuple_value(tuple_type, data);
  ExprValue tuple_struct_value(tuple_struct_type, data);

  // Minimal one-line mode.
  ConsoleFormatOptions minimal;
  minimal.verbosity = ConsoleFormatOptions::Verbosity::kMinimal;
  EXPECT_EQ("(1, 2)", SyncFormatValue(tuple_value, minimal));
  EXPECT_EQ("MyTupleStruct(1, 2)", SyncFormatValue(tuple_struct_value, minimal));

  // Expanded one-line mode.
  ConsoleFormatOptions minimal_expanded = minimal;
  minimal_expanded.wrapping = ConsoleFormatOptions::Wrapping::kExpanded;
  EXPECT_EQ(
      "(\n"
      "  0: 1\n"
      "  1: 2\n"
      ")",
      SyncFormatValue(tuple_value, minimal_expanded));
  EXPECT_EQ(
      "MyTupleStruct(\n"
      "  0: 1\n"
      "  1: 2\n"
      ")",
      SyncFormatValue(tuple_struct_value, minimal_expanded));

  // With full type information. We should the type information for the members but not the tuple
  // itself. These are redundant and look confusing. Currently our full type information looks like
  // C and we may want to revisit this in the future.
  ConsoleFormatOptions full_types = minimal_expanded;
  full_types.verbosity = ConsoleFormatOptions::Verbosity::kAllTypes;
  EXPECT_EQ(
      "(\n"
      "  (int32_t) 0: 1\n"
      "  (int32_t) 1: 2\n"
      ")",
      SyncFormatValue(tuple_value, full_types));
  EXPECT_EQ(
      "MyTupleStruct(\n"
      "  (int32_t) 0: 1\n"
      "  (int32_t) 1: 2\n"
      ")",
      SyncFormatValue(tuple_struct_value, full_types));
}

TEST_F(FormatValueConsoleTest, RustEnum) {
  auto int32_type = MakeInt32Type();  // Won't be named like Rust's ints but doesn't matter.

  auto enum_type = MakeTestRustEnum();

  // "None" is the default so any discriminant value (first 4 bytes) other than 0 or 1 should match.
  // Pad out to 12 bytes, the rest are unused. Note that Rust doesn't actually use the default
  // discriminant feature of DWARF.
  ExprValue none_value(enum_type, {0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});

  // Scalar(123): a single 32-bit value following the 32-bit discriminant of 0.
  ExprValue scalar_value(enum_type, {0, 0, 0, 0, 123, 0, 0, 0, 0, 0, 0, 0});

  // Point{x:12, y:13}: two 32-bit values following the 32-bit discriminant of 1.
  ExprValue point_value(enum_type, {1, 0, 0, 0, 12, 0, 0, 0, 13, 0, 0, 0});

  // Minimal single-line formatting.
  ConsoleFormatOptions minimal;
  minimal.verbosity = ConsoleFormatOptions::Verbosity::kMinimal;
  EXPECT_EQ("None", SyncFormatValue(none_value, minimal));
  EXPECT_EQ("Scalar(123)", SyncFormatValue(scalar_value, minimal));
  EXPECT_EQ("Point{x: 12, y: 13}", SyncFormatValue(point_value, minimal));

  // Expanded with type info.
  ConsoleFormatOptions expanded_all_types;
  expanded_all_types.verbosity = ConsoleFormatOptions::Verbosity::kAllTypes;
  expanded_all_types.wrapping = ConsoleFormatOptions::Wrapping::kExpanded;
  EXPECT_EQ("(RustEnum) None", SyncFormatValue(none_value, expanded_all_types));
  EXPECT_EQ(
      "(RustEnum) Scalar(\n"
      "  (int32_t) 123\n"  // Note: no index for single-element tuples.
      ")",
      SyncFormatValue(scalar_value, expanded_all_types));
  EXPECT_EQ(
      "(RustEnum) Point{\n"
      "  (int32_t) x: 12\n"
      "  (int32_t) y: 13\n"
      "}",
      SyncFormatValue(point_value, expanded_all_types));
}

// This only tests the presentational part of Rust vectors. Vec->Node conversion is tested in
// format_unittests.cc
TEST_F(FormatValueConsoleTest, RustVector) {
  // Give real values with a type from a Rust unit to trigger Rust-specific formatting. Don't need
  // them to have actual data.
  auto vec_type = MakeCollectionType(DwarfTag::kStructureType, "alloc::vec::Vec<i32>", {});
  SymbolTestParentSetter vec_type_parent(vec_type, MakeRustUnit());
  ExprValue vec_value(vec_type, {});

  auto int_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 2, "u32");
  SymbolTestParentSetter int_type_parent(int_type, MakeRustUnit());
  ExprValue int_value(int_type, {});

  // A Vec of int.
  FormatNode vec("", vec_value);
  vec.set_type("alloc::vec::Vec<i32>");
  vec.set_description_kind(FormatNode::kArray);
  vec.set_state(FormatNode::kDescribed);

  vec.children().push_back(std::make_unique<FormatNode>("[0]", int_value));
  vec.children()[0]->set_type("i32");
  vec.children()[0]->set_state(FormatNode::kDescribed);
  vec.children()[0]->set_description("42");
  vec.children().push_back(std::make_unique<FormatNode>("[1]", int_value));
  vec.children()[1]->set_type("i32");
  vec.children()[1]->set_state(FormatNode::kDescribed);
  vec.children()[1]->set_description("19");

  ConsoleFormatOptions options;

  // Minimal verbosity gets tyep type abbreviated "vec!" which is how Rust users would typically
  // instantiate an array.
  options.verbosity = ConsoleFormatOptions::Verbosity::kMinimal;
  EXPECT_EQ("vec![42, 19]", FormatNodeForConsole(vec, options).AsString());

  // Expanded mode.
  options.wrapping = ConsoleFormatOptions::Wrapping::kExpanded;
  EXPECT_EQ(
      "vec![\n"
      "  [0]: 42\n"
      "  [1]: 19\n"
      "]",
      FormatNodeForConsole(vec, options).AsString());

  // Medium verbosity shows the real type.
  options.wrapping = ConsoleFormatOptions::Wrapping::kNone;
  options.verbosity = ConsoleFormatOptions::Verbosity::kMedium;
  EXPECT_EQ("alloc::vec::Vec<i32>[42, 19]", FormatNodeForConsole(vec, options).AsString());

  // Full type info is the same.
  options.verbosity = ConsoleFormatOptions::Verbosity::kAllTypes;
  EXPECT_EQ("alloc::vec::Vec<i32>[42, 19]", FormatNodeForConsole(vec, options).AsString());

  // Use another type name in minimal mode. It shouldn't get abbreviated with the "vec!"
  auto fast_vec_type = MakeCollectionType(DwarfTag::kStructureType, "FastVector<i32>", {});
  SymbolTestParentSetter fast_vec_type_parent(fast_vec_type, MakeRustUnit());
  ExprValue fast_vec_value(fast_vec_type, {});

  vec.SetValue(fast_vec_value);
  vec.set_type("FastVector<i32>");
  options.verbosity = ConsoleFormatOptions::Verbosity::kMinimal;
  EXPECT_EQ("FastVector<i32>[42, 19]", FormatNodeForConsole(vec, options).AsString());
}

}  // namespace zxdb
