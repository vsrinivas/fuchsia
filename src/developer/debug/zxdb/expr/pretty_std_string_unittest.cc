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

namespace zxdb {

namespace {

class PrettyStdStringTest : public TestWithLoop {};

}  // namespace

TEST_F(PrettyStdStringTest, Short) {
  // Encodes 'a'-'m' in the "short" form of a std::string.
  uint8_t kShortMem[24] = {
      0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,  // Inline bytes...
      0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x00, 0x00, 0x00,  // ...
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d,  // ..., last byte is size.
  };

  ExprValue short_value(fxl::RefPtr<Type>(),
                        std::vector<uint8_t>(std::begin(kShortMem), std::end(kShortMem)));
  auto context = fxl::MakeRefCounted<MockEvalContext>();
  FormatNode node("value", short_value);

  PrettyStdString pretty;

  // Expect synchronous completion for the inline value case.
  bool completed = false;
  pretty.Format(&node, FormatOptions(), context,
                fit::defer_callback([&completed]() { completed = true; }));
  EXPECT_TRUE(completed);

  EXPECT_EQ("\"abcdefghijklm\"", node.description());
}

// Tests a string with no bytes but a source location. This string data encodes the "long" format
// which is in turn another pointer.
TEST_F(PrettyStdStringTest, Long) {
  auto context = fxl::MakeRefCounted<MockEvalContext>();

  // The string that's pointed to.
  constexpr uint64_t kStringAddress = 0x245260;
  constexpr size_t kStringLength = 0x45;
  const char kStringData[] =
      "Now is the time for all good men to come to the aid of their country.";
  context->data_provider()->AddMemory(
      kStringAddress, std::vector<uint8_t>(std::begin(kStringData), std::end(kStringData)));

  // The std::string object representation.
  constexpr uint64_t kObjectAddress = 0x12345678;
  uint8_t kLongMem[24] = {
      // clang-format off
      0x60, 0x52, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00,  // Address = kStringAddress.
      kStringLength, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Length = kStringLength
      0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,  // Capacity (last bit is "long" flag).
      // clang-format on
  };
  context->data_provider()->AddMemory(
      kObjectAddress, std::vector<uint8_t>(std::begin(kLongMem), std::end(kLongMem)));

  // The std::string object. This has no data in it because the real type isn't known, but it does
  // have a valid source address.
  ExprValue value(fxl::RefPtr<Type>(), {}, ExprValueSource(kObjectAddress));

  FormatNode node("value", value);

  PrettyStdString pretty;

  bool completed = false;
  pretty.Format(&node, FormatOptions(), context,
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
