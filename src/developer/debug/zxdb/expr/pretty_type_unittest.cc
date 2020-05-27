// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_type.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/format_options.h"
#include "src/developer/debug/zxdb/expr/format_test_support.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

// NOTE: Some of the tests are in pretty_type_manager_unittest.cc. Those test the actual
// instantiation of the PrettyType classes for STL, etc. while this file tests the PrettyType
// classes in the abstract.

namespace zxdb {

namespace {

class PrettyTypeTest : public TestWithLoop {};

}  // namespace

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
