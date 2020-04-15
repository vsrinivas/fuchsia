// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/diagnostics/stream/cpp/fidl.h>
#include <lib/zx/clock.h>

#include <iomanip>
#include <iostream>
#include <vector>

#include "gtest/gtest.h"
#include "src/diagnostics/lib/streams/encode.h"

TEST(StreamsRecordEncoder, Writable) {
  // Create Buffer
  std::vector<uint8_t> vec;
  // Create Record
  fuchsia::diagnostics::stream::Value value;
  fuchsia::diagnostics::stream::Argument arg{.name = "arg_name", .value = std::move(value)};
  std::vector<fuchsia::diagnostics::stream::Argument> args;
  args.push_back(std::move(arg));
  fuchsia::diagnostics::stream::Record record{.timestamp = 12, .arguments = std::move(args)};
  // Write Record
  streams::log_record(record, &vec);
  int length = vec.size();
  ASSERT_NE(length, 0);
}

TEST(StreamsRecordEncoder, WriteRecordString) {
  // Create Buffer
  std::vector<uint8_t> vec;
  // Create Record
  fuchsia::diagnostics::stream::Value value;
  value.set_text("hello");
  fuchsia::diagnostics::stream::Argument arg{.name = "world", .value = std::move(value)};
  std::vector<fuchsia::diagnostics::stream::Argument> args;
  args.push_back(std::move(arg));
  fuchsia::diagnostics::stream::Record record{.timestamp = 5, .arguments = std::move(args)};
  streams::log_record(record, &vec);

  std::vector<uint8_t> expected(
      {0x5, 0, 0, 0, 0, 0, 0, 0, 'w', 'o', 'r', 'l', 'd', 'h', 'e', 'l', 'l', 'o'});
  EXPECT_EQ(expected, vec);
}

TEST(StreamsRecordEncoder, WriteRecordSignedIntNegative) {
  // Create Buffer
  std::vector<uint8_t> vec;
  // Create Record
  fuchsia::diagnostics::stream::Value value;
  value.set_signed_int(-7);
  fuchsia::diagnostics::stream::Argument arg{.name = "name", .value = std::move(value)};
  std::vector<fuchsia::diagnostics::stream::Argument> args;
  args.push_back(std::move(arg));

  fuchsia::diagnostics::stream::Record record{.timestamp = 9, .arguments = std::move(args)};
  streams::log_record(record, &vec);
  std::vector<uint8_t> expected({0x9, 0,   0,    0,    0,    0,    0,    0,    'n',  'a',
                                 'm', 'e', 0xF9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
  EXPECT_EQ(vec, expected);
}

TEST(StreamsRecordEncoder, WriteRecordSignedIntPositive) {
  // Create Buffer
  std::vector<uint8_t> vec;
  // Create Record
  fuchsia::diagnostics::stream::Value value;
  value.set_signed_int(4);
  fuchsia::diagnostics::stream::Argument arg{.name = "name", .value = std::move(value)};
  std::vector<fuchsia::diagnostics::stream::Argument> args;
  args.push_back(std::move(arg));

  fuchsia::diagnostics::stream::Record record{.timestamp = 9, .arguments = std::move(args)};
  streams::log_record(record, &vec);
  std::vector<uint8_t> expected(
      {0x9, 0, 0, 0, 0, 0, 0, 0, 'n', 'a', 'm', 'e', 0x4, 0, 0, 0, 0, 0, 0, 0});
  EXPECT_EQ(vec, expected);
}

TEST(StreamsRecordEncoder, WriteRecordUnsignedInt) {
  // Create Buffer
  std::vector<uint8_t> vec;
  // Create Record
  fuchsia::diagnostics::stream::Value value;
  value.set_unsigned_int(3);
  fuchsia::diagnostics::stream::Argument arg{.name = "name", .value = std::move(value)};
  std::vector<fuchsia::diagnostics::stream::Argument> args;
  args.push_back(std::move(arg));

  fuchsia::diagnostics::stream::Record record{.timestamp = 6, .arguments = std::move(args)};
  streams::log_record(record, &vec);
  std::vector<uint8_t> expected(
      {0x6, 0, 0, 0, 0, 0, 0, 0, 'n', 'a', 'm', 'e', 0x3, 0, 0, 0, 0, 0, 0, 0});
  EXPECT_EQ(vec, expected);
}

TEST(StreamsRecordEncoder, WriteRecordFloat) {
  // Create Buffer
  std::vector<uint8_t> vec;
  // Create Record
  fuchsia::diagnostics::stream::Value value;
  value.set_floating(3.1415);
  fuchsia::diagnostics::stream::Argument arg{.name = "name", .value = std::move(value)};
  std::vector<fuchsia::diagnostics::stream::Argument> args;
  args.push_back(std::move(arg));

  fuchsia::diagnostics::stream::Record record{.timestamp = 6, .arguments = std::move(args)};
  streams::log_record(record, &vec);
  std::vector<uint8_t> expected({0x6, 0,   0,    0,    0,    0,    0,    0,    'n',  'a',
                                 'm', 'e', 0x6F, 0x12, 0x83, 0xC0, 0xCA, 0x21, 0x09, 0x40});
  EXPECT_EQ(vec, expected);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
