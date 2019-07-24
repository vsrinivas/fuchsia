// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/pretty_std_string.h"

#include "gtest/gtest.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/format_options.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

namespace {

// Memory address/length of the default string. This is relatively long so it's guaranteed to
// exceed the C++ short string optimization length.
constexpr uint64_t kStringAddress = 0x99887766;
constexpr uint64_t kStringLen = 69;  // Not including null.

class PrettyStringTest : public TestWithLoop {
 public:
  PrettyStringTest() {
    context_ = fxl::MakeRefCounted<MockEvalContext>();

    const char kStringData[] =
        "Now is the time for all good men to come to the aid of their country.";
    context_->data_provider()->AddMemory(
        kStringAddress, std::vector<uint8_t>(std::begin(kStringData), std::end(kStringData)));
  }

  fxl::RefPtr<MockEvalContext> context() { return context_; }

 private:
  fxl::RefPtr<MockEvalContext> context_;
};

}  // namespace

TEST_F(PrettyStringTest, StdStringShort) {
  // Encodes 'a'-'m' in the "short" form of a std::string.
  uint8_t kShortMem[24] = {
      0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,  // Inline bytes...
      0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x00, 0x00, 0x00,  // ...
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d,  // ..., last byte is size.
  };

  ExprValue short_value(fxl::RefPtr<Type>(),
                        std::vector<uint8_t>(std::begin(kShortMem), std::end(kShortMem)));
  FormatNode node("value", short_value);

  PrettyStdString pretty;

  // Expect synchronous completion for the inline value case.
  bool completed = false;
  pretty.Format(&node, FormatOptions(), context(),
                fit::defer_callback([&completed]() { completed = true; }));
  EXPECT_TRUE(completed);

  EXPECT_EQ("\"abcdefghijklm\"", node.description());
}

// Tests a string with no bytes but a source location. This string data encodes the "long" format
// which is in turn another pointer.
TEST_F(PrettyStringTest, StdStringLong) {
  // The std::string object representation.
  constexpr uint64_t kObjectAddress = 0x12345678;
  uint8_t kLongMem[24] = {
      // clang-format off
      0x66, 0x77, 0x88, 0x99, 0x00, 0x00, 0x00, 0x00,  // Address = kStringAddress.
      kStringLen, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Length = kStringLen
      0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,  // Capacity (last bit is "long" flag).
      // clang-format on
  };
  context()->data_provider()->AddMemory(
      kObjectAddress, std::vector<uint8_t>(std::begin(kLongMem), std::end(kLongMem)));

  // The std::string object. This has no data in it because the real type isn't known, but it does
  // have a valid source address.
  ExprValue value(fxl::RefPtr<Type>(), {}, ExprValueSource(kObjectAddress));

  FormatNode node("value", value);

  PrettyStdString pretty;

  bool completed = false;
  pretty.Format(&node, FormatOptions(), context(),
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
