// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_rust_tuple.h"

#include "gtest/gtest.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/format_options.h"
#include "src/developer/debug/zxdb/expr/format_test_support.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

namespace {

class PrettyRustTupleTest : public TestWithLoop {
 public:
  PrettyRustTupleTest() { context_ = fxl::MakeRefCounted<MockEvalContext>(); }

  // Evaluates the given member, promotes the result to 64-bit and expects that it's equal to the
  // given value.
  void ExpectMemberValue(const ExprValue& input, const std::string& member_name,
                         int64_t expected) {
    PrettyRustTuple pretty;

    auto getter = pretty.GetMember(member_name);
    ASSERT_TRUE(getter);

    bool should_quit = false;
    bool called = false;
    getter(context(), input, [&called, &should_quit, expected](ErrOrValue v) {
      called = true;

      ASSERT_TRUE(v.ok()) << v.err().msg();

      int64_t actual = 0;
      Err err = v.value().PromoteTo64(&actual);
      ASSERT_TRUE(err.ok());
      EXPECT_EQ(expected, actual);

      if (should_quit)
        debug_ipc::MessageLoop::Current()->QuitNow();
    });
    if (!called) {
      should_quit = true;
      debug_ipc::MessageLoop::Current()->Run();
    }
    EXPECT_TRUE(called);
  }

  fxl::RefPtr<MockEvalContext> context() { return context_; }

 private:
  fxl::RefPtr<MockEvalContext> context_;
};

}  // namespace

TEST_F(PrettyRustTupleTest, Tuple) {
  const char kMyStructName[] = "(i32)";

  // Encodes 13 as a 32-bit integer.
  uint8_t kMem[4] = {
      0x0d,
      0x00,
      0x00,
      0x00,
  };

  auto int32_type = MakeInt32Type();
  auto type = MakeTestRustTuple(kMyStructName, {int32_type});

  ExprValue value(type, std::vector<uint8_t>(std::begin(kMem), std::end(kMem)));
  FormatNode node("value", value);

  PrettyRustTuple pretty;

  // Expect synchronous completion for the inline value case.
  bool completed = false;
  pretty.Format(&node, FormatOptions(), context(),
                fit::defer_callback([&completed]() { completed = true; }));
  EXPECT_TRUE(completed);

  SyncFillAndDescribeFormatTree(context(), &node, FormatOptions());
  EXPECT_EQ("value = (i32), \n  0 = int32_t, 13\n", GetDebugTreeForFormatNode(&node));

  ExpectMemberValue(value, "0", 13);
}

TEST_F(PrettyRustTupleTest, TupleStruct) {
  const char kMyStructName[] = "MyStruct";

  // Encodes 13 as a 32-bit integer.
  uint8_t kMem[4] = {
      0x0d,
      0x00,
      0x00,
      0x00,
  };

  auto int32_type = MakeInt32Type();
  auto type = MakeTestRustTuple(kMyStructName, {int32_type});

  ExprValue value(type, std::vector<uint8_t>(std::begin(kMem), std::end(kMem)));
  FormatNode node("value", value);

  PrettyRustTuple pretty;

  // Expect synchronous completion for the inline value case.
  bool completed = false;
  pretty.Format(&node, FormatOptions(), context(),
                fit::defer_callback([&completed]() { completed = true; }));
  EXPECT_TRUE(completed);

  SyncFillAndDescribeFormatTree(context(), &node, FormatOptions());
  EXPECT_EQ("value = MyStruct, \n  0 = int32_t, 13\n", GetDebugTreeForFormatNode(&node));

  ExpectMemberValue(value, "0", 13);
}

TEST_F(PrettyRustTupleTest, Status) {
  const char kRustStatusName[] = "fuchsia_zircon_status::Status";

  // Encodes -6 as a 32-bit integer.
  uint8_t kMem[4] = {
      0xfa,
      0xff,
      0xff,
      0xff,
  };

  auto int32_type = MakeInt32Type();
  auto type = MakeTestRustTuple(kRustStatusName, {int32_type});

  ExprValue value(type, std::vector<uint8_t>(std::begin(kMem), std::end(kMem)));
  FormatNode node("value", value);

  PrettyRustZirconStatus pretty;

  // Expect synchronous completion for the inline value case.
  bool completed = false;
  pretty.Format(&node, FormatOptions(), context(),
                fit::defer_callback([&completed]() { completed = true; }));
  EXPECT_TRUE(completed);
  ASSERT_EQ(1u, node.children().size());

  EXPECT_EQ("-6 (ZX_ERR_INTERNAL_INTR_RETRY)", node.children()[0]->description());
}

}  // namespace zxdb
