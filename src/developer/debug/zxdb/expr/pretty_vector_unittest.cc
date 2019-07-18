// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_vector.h"

#include "gtest/gtest.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/format_options.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/template_parameter.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

namespace {

class PrettyVectorTest : public TestWithLoop {};

}  // namespace

TEST_F(PrettyVectorTest, Basic) {
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

  FormatNode node("value", vec_value);

  PrettyStdVector pretty;

  bool called = false;
  pretty.Format(&node, FormatOptions(), context, fit::defer_callback([&called, loop = &loop()]() {
    called = true, loop->QuitNow();
  }));
  ASSERT_FALSE(called);  // Should be async.
  loop().Run();

  EXPECT_EQ("std::vector<int32_t>", node.type());

  ASSERT_EQ(2u, node.children().size());
  EXPECT_EQ(1, node.children()[0]->value().GetAs<int32_t>());
  EXPECT_EQ(99, node.children()[1]->value().GetAs<int32_t>());
}

}  // namespace zxdb
