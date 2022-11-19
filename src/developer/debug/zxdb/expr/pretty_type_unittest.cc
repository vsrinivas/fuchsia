// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_type.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/abi_null.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/format_options.h"
#include "src/developer/debug/zxdb/expr/format_test_support.h"
#include "src/developer/debug/zxdb/expr/found_name.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/expr/test_eval_context_impl.h"
#include "src/developer/debug/zxdb/symbols/index_test_support.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"
#include "src/developer/debug/zxdb/symbols/symbol_test_parent_setter.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

// NOTE: Some of the tests are in pretty_type_manager_unittest.cc. Those test the actual
// instantiation of the PrettyType classes for STL, etc. while this file tests the PrettyType
// classes in the abstract.

namespace zxdb {

namespace {

class PrettyTypeTest : public TestWithLoop {};

}  // namespace

TEST_F(PrettyTypeTest, PrettyGenericContainer) {
  // To test that the pretty-printer program can resolve type names local to the structure
  // being pretty-printed, we need a full symbol index.
  ProcessSymbolsTestSetup setup;
  MockModuleSymbols* module_symbols = setup.InjectMockModule();
  SymbolContext symbol_context(ProcessSymbolsTestSetup::kDefaultLoadAddress);
  auto& index_root = module_symbols->index().root();

  auto data_provider = fxl::MakeRefCounted<MockSymbolDataProvider>();
  auto eval_context = fxl::MakeRefCounted<TestEvalContextImpl>(
      std::make_shared<AbiNull>(), setup.process().GetWeakPtr(), data_provider, ExprLanguage::kC);

  // Test struct.
  const char kStructName[] = "TestStruct";
  auto structure =
      MakeCollectionType(DwarfTag::kStructureType, kStructName, {{"value", MakeInt32Type()}});
  TestIndexedSymbol indexed_struct(module_symbols, &index_root, kStructName, structure);

  // Test struct value.
  ExprValue value(static_cast<int32_t>(4), structure);

  // Make the equivalent of a "typedef int16_t MyInt;" inside the structure.
  const char kMyIntName[] = "MyInt";
  auto my_int = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kTypedef, MakeInt16Type());
  my_int->set_assigned_name(kMyIntName);
  SymbolTestParentSetter my_int_parent(my_int, structure);
  EXPECT_EQ("TestStruct::MyInt", my_int->GetFullName());
  TestIndexedSymbol indexed_int(module_symbols, indexed_struct.index_node, kMyIntName, my_int);

  // Test appending literal string keys as well as value keys and the GetMaxArraySize() function.
  // My doing the cast to "MyInt", we also test that types can be resolved that are members of
  // the struct being pretty-printed.
  PrettyGenericContainer pretty(R"(
    $zxdb::AppendKeyValueRow("[size]", value);
    for (int i = 0; i < value; i = i + 1) {
      $zxdb::AppendKeyValueRow(i, static_cast<MyInt>(1000) + i);
    }
    $zxdb::AppendNameValueRow("[max was]", $zxdb::GetMaxArraySize());
    $zxdb::AppendNameRow("...");
  )");

  FormatNode node("result", value);
  node.set_type("TestStruct");
  FormatOptions options;

  bool complete = false;
  pretty.Format(&node, options, eval_context,
                fit::defer_callback([&complete]() { complete = true; }));
  EXPECT_TRUE(complete);  // Expect synchronous completion.
  node.set_state(FormatNode::kDescribed);

  EXPECT_EQ(7u, node.children().size());

  // Describe each child but not the root (that will overwrite what we just did above with the
  // custom pretty-printer).
  for (auto& child : node.children()) {
    SyncFillAndDescribeFormatNode(eval_context, child.get(), options);
  }
  EXPECT_EQ(
      "result = TestStruct, \n"
      "  \"[size]\" = int32_t, 4\n"
      "  0 = int64_t, 1000\n"
      "  1 = int64_t, 1001\n"
      "  2 = int64_t, 1002\n"
      "  3 = int64_t, 1003\n"
      "  [max was] = uint32_t, 256\n"
      "  ... = , \n",
      GetDebugTreeForFormatNode(&node));
}

TEST_F(PrettyTypeTest, RecursiveVariant) {
  // This declares the following structure to encode a variant of two values:
  //
  //   template<Value, NextNode> union Node {
  //     Value value;
  //     NextNode next;
  //   };
  //
  //   struct Variant<int32_t, double> {
  //     uint32_t index;
  //     Node<int32_t, Node<double, void>> base;
  //   }
  auto node_double_void =
      MakeCollectionType(DwarfTag::kUnionType, "Node<double, void>", {{"value", MakeDoubleType()}});
  auto node_int_double =
      MakeCollectionType(DwarfTag::kUnionType, "Node<int, Node<double, void>>",
                         {{"value", MakeInt32Type()}, {"next", node_double_void}});
  const char kVariantName[] = "Variant<int32_t, double>";
  auto variant_type = MakeCollectionType(DwarfTag::kStructureType, kVariantName,
                                         {{"index", MakeUint32Type()}, {"base", node_int_double}});

  PrettyRecursiveVariant pretty("Variant", "base", "index", "next", "value", "Variant::npos", {});

  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();
  FormatOptions options;

  // NONE VARIANT

  // 4-byte index = -1, 8-byte value is irrelevant.
  FormatNode none_node("none",
                       ExprValue(variant_type, {0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0}));
  none_node.set_type(kVariantName);
  bool complete = false;
  pretty.Format(&none_node, options, eval_context,
                fit::defer_callback([&complete]() { complete = true; }));
  EXPECT_TRUE(complete);  // Expect synchronous completion.
  EXPECT_EQ("none = Variant<int32_t, double>, Variant::npos\n",
            GetDebugTreeForFormatNode(&none_node));

  // INT VARIANT

  // 4-byte index = 0, 4-byte int32 value = 42, 4 bytes padding.
  FormatNode int_node("int_one", ExprValue(variant_type, {0, 0, 0, 0, 42, 0, 0, 0, 0, 0, 0, 0}));
  int_node.set_type(kVariantName);
  complete = false;
  pretty.Format(&int_node, options, eval_context,
                fit::defer_callback([&complete]() { complete = true; }));
  EXPECT_TRUE(complete);  // Expect synchronous completion.

  // It should have made one child which we need to describe now.
  ASSERT_EQ(1u, int_node.children().size());
  SyncFillAndDescribeFormatNode(eval_context, int_node.children()[0].get(), options);
  EXPECT_EQ(
      "int_one = Variant<int32_t, double>, Variant\n"
      "   = int32_t, 42\n",
      GetDebugTreeForFormatNode(&int_node));

  // DOUBLE VARIANT

  // 4-byte index = 1, 8 byte double value.
  std::vector<uint8_t> data{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  const double kSourceDouble = 3.14159;
  memcpy(&data[4], &kSourceDouble, sizeof(double));

  FormatNode double_node("double_one", ExprValue(variant_type, data));
  double_node.set_type(kVariantName);
  complete = false;
  pretty.Format(&double_node, options, eval_context,
                fit::defer_callback([&complete]() { complete = true; }));
  EXPECT_TRUE(complete);  // Expect synchronous completion.

  // It should have made one child which we need to describe now.
  ASSERT_EQ(1u, double_node.children().size());
  SyncFillAndDescribeFormatNode(eval_context, double_node.children()[0].get(), options);
  EXPECT_EQ(
      "double_one = Variant<int32_t, double>, Variant\n"
      "   = double, 3.14159\n",
      GetDebugTreeForFormatNode(&double_node));
}

TEST_F(PrettyTypeTest, PrettyWrappedValue) {
  // Make a structure with one value that will be our pretty-printed value.
  auto structure =
      MakeCollectionType(DwarfTag::kStructureType, "TestStruct", {{"value", MakeInt32Type()}});
  ExprValue value(static_cast<int32_t>(42), structure);

  PrettyWrappedValue pretty("TestStruct", "[<", ">]", "value");

  FormatNode node("result", value);
  node.set_type("TestStruct");
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();
  FormatOptions options;

  bool complete = false;
  pretty.Format(&node, options, eval_context,
                fit::defer_callback([&complete]() { complete = true; }));
  EXPECT_TRUE(complete);  // Expect synchronous completion.
  SyncFillAndDescribeFormatNode(eval_context, node.children()[0].get(), options);
  EXPECT_EQ(
      "result = TestStruct, TestStruct\n"
      "   = int32_t, 42\n",
      GetDebugTreeForFormatNode(&node));
  EXPECT_EQ("[<", node.wrapper_prefix());
  EXPECT_EQ(">]", node.wrapper_suffix());
}

}  // namespace zxdb
