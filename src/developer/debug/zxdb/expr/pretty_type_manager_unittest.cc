// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_type_manager.h"

#include "gtest/gtest.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/format_options.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/expr/pretty_type.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/template_parameter.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

namespace {

class PrettyTypeManagerTest : public TestWithLoop {};

}  // namespace

TEST_F(PrettyTypeManagerTest, StdVector) {
  auto context = fxl::MakeRefCounted<MockEvalContext>();

  // Array data.
  constexpr uint64_t kAddress = 0x221100;
  context->data_provider()->AddMemory(kAddress, {
                                                    1, 0, 0, 0,  // [0] = 1
                                                    99, 0, 0, 0  // [1] = 99
                                                });

  auto int32_type = MakeInt32Type();
  auto int32_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, int32_type);
  auto allocator_type =
      MakeCollectionType(DwarfTag::kClassType, "std::__2::allocator<int32_t>", {});

  auto vector_type = MakeCollectionType(
      DwarfTag::kClassType, "std::__2::vector<int32_t, std::__2::allocator<int32_t> >",
      {{"__begin_", int32_ptr_type}, {"__end_", int32_ptr_type}, {"__end_cap_", int32_ptr_type}});

  auto int32_param = fxl::MakeRefCounted<TemplateParameter>("T", int32_type, false);
  auto allocator_param = fxl::MakeRefCounted<TemplateParameter>("allocator", allocator_type, false);
  vector_type->set_template_params({LazySymbol(int32_param), LazySymbol(allocator_param)});

  ExprValue vec_value(vector_type, {
                                       0x00, 0x11, 0x22, 0, 0, 0, 0, 0,  // __begin_
                                       0x08, 0x11, 0x22, 0, 0, 0, 0, 0,  // __end_ = __begin_ + 8
                                       0x08, 0x11, 0x22, 0, 0, 0, 0, 0,  // __end_cap_ = __end_
                                   });

  PrettyTypeManager manager;
  PrettyType* pretty_vector = manager.GetForType(vector_type.get());
  ASSERT_TRUE(pretty_vector);

  FormatNode node("value", vec_value);

  bool called = false;
  pretty_vector->Format(
      &node, FormatOptions(), context,
      fit::defer_callback([&called, loop = &loop()]() { called = true, loop->QuitNow(); }));
  ASSERT_FALSE(called);  // Should be async.
  loop().Run();

  ASSERT_EQ(2u, node.children().size());
  EXPECT_EQ(1, node.children()[0]->value().GetAs<int32_t>());
  EXPECT_EQ(99, node.children()[1]->value().GetAs<int32_t>());
}

TEST_F(PrettyTypeManagerTest, RustStrings) {
  constexpr uint64_t kStringAddress = 0x99887766;
  constexpr uint64_t kStringLen = 69;  // Not including null.

  const char kStringData[] =
      "Now is the time for all good men to come to the aid of their country.";
  auto context = fxl::MakeRefCounted<MockEvalContext>();
  context->data_provider()->AddMemory(
      kStringAddress, std::vector<uint8_t>(std::begin(kStringData), std::end(kStringData)));

  // The str object representation is just a pointer and a length.
  uint8_t kRustObject[16] = {
      0x66,       0x77, 0x88, 0x99, 0x00, 0x00, 0x00, 0x00,  // Address = kStringAddress.
      kStringLen, 0,    0,    0,    0,    0,    0,    0      // Length = kStringLen.
  };

  auto str_type =
      MakeCollectionType(DwarfTag::kStructureType, "&str",
                         {{"data_ptr", MakeRustCharPointerType()}, {"length", MakeUint64Type()}});
  str_type->set_parent(MakeRustUnit());

  ExprValue value(str_type, std::vector<uint8_t>(std::begin(kRustObject), std::end(kRustObject)));
  FormatNode node("value", value);

  PrettyTypeManager manager;
  PrettyType* pretty = manager.GetForType(str_type.get());
  ASSERT_TRUE(pretty);

  bool completed = false;
  pretty->Format(&node, FormatOptions(), context,
                 fit::defer_callback([&completed, loop = &loop()]() {
                   completed = true;
                   loop->QuitNow();
                 }));
  EXPECT_FALSE(completed);  // Should be async.
  loop().Run();
  EXPECT_TRUE(completed);

  EXPECT_EQ("\"Now is the time for all good men to come to the aid of their country.\"",
            node.description());
}

}  // namespace zxdb
