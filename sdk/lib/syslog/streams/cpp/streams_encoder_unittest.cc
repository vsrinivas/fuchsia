// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/diagnostics/stream/cpp/fidl.h>
#include <lib/zx/clock.h>

#include <iomanip>
#include <iostream>
#include <vector>

#include <gtest/gtest.h>

#include "sdk/lib/syslog/streams/cpp/encode.h"

TEST(StreamsRecordEncoder, Writable) {
  // Create Buffer
  std::vector<uint8_t> vec;
  // Create Record
  fuchsia::diagnostics::stream::Value value;
  fuchsia::diagnostics::stream::Argument arg{.name = "arg_name", .value = std::move(value)};
  std::vector<fuchsia::diagnostics::stream::Argument> args;
  args.push_back(std::move(arg));
  fuchsia::diagnostics::stream::Record record{.timestamp = 12,
                                              .severity = fuchsia::diagnostics::Severity::INFO,
                                              .arguments = std::move(args)};
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
  fuchsia::diagnostics::stream::Record record{.timestamp = 5,
                                              .severity = fuchsia::diagnostics::Severity::INFO,
                                              .arguments = std::move(args)};
  streams::log_record(record, &vec);

  // Expected Results
  // 5: represents the size of the record
  // 9: represents the type of Record (Log record)
  // 0x30: represents the severity
  std::vector<uint8_t> expected_record_header({0x59, 0, 0, 0, 0, 0, 0, 0x30});
  // 5: represents the timestamp
  std::vector<uint8_t> expected_time_stamp({0x5, 0, 0, 0, 0, 0, 0, 0});
  // 3: represents the size of argument
  // 6: represents the value type
  // 5, 0x80: represents the key stringref
  // second 5, 0x80: represents the value stringref
  std::vector<uint8_t> expected_arg_header({0x36, 0, 0x5, 0x80, 0x5, 0x80, 0, 0});
  std::vector<uint8_t> expected_arg_name({'w', 'o', 'r', 'l', 'd', 0, 0, 0});
  std::vector<uint8_t> expected_text_value({'h', 'e', 'l', 'l', 'o', 0, 0, 0});

  std::vector<uint8_t> expected_result;
  expected_result.insert(expected_result.end(), expected_record_header.begin(),
                         expected_record_header.end());

  expected_result.insert(expected_result.end(), expected_time_stamp.begin(),
                         expected_time_stamp.end());
  expected_result.insert(expected_result.end(), expected_arg_header.begin(),
                         expected_arg_header.end());
  expected_result.insert(expected_result.end(), expected_arg_name.begin(), expected_arg_name.end());
  expected_result.insert(expected_result.end(), expected_text_value.begin(),
                         expected_text_value.end());

  EXPECT_EQ(expected_result, vec);
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

  fuchsia::diagnostics::stream::Record record{.timestamp = 9,
                                              .severity = fuchsia::diagnostics::Severity::INFO,
                                              .arguments = std::move(args)};
  streams::log_record(record, &vec);
  std::vector<uint8_t> expected({0x59, 0, 0,    0,    0,    0,    0,    0x30, 0x9,  0,
                                 0,    0, 0,    0,    0,    0,    0x33, 0,    0x4,  0x80,
                                 0,    0, 0,    0,    'n',  'a',  'm',  'e',  0,    0,
                                 0,    0, 0xF9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
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

  fuchsia::diagnostics::stream::Record record{.timestamp = 9,
                                              .severity = fuchsia::diagnostics::Severity::INFO,
                                              .arguments = std::move(args)};
  streams::log_record(record, &vec);
  std::vector<uint8_t> expected({0x59, 0, 0,    0, 0,   0,    0, 0x30, 0x9, 0, 0,   0,   0,   0,
                                 0,    0, 0x33, 0, 0x4, 0x80, 0, 0,    0,   0, 'n', 'a', 'm', 'e',
                                 0,    0, 0,    0, 0x4, 0,    0, 0,    0,   0, 0,   0});
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

  fuchsia::diagnostics::stream::Record record{.timestamp = 6,
                                              .severity = fuchsia::diagnostics::Severity::INFO,
                                              .arguments = std::move(args)};
  streams::log_record(record, &vec);
  std::vector<uint8_t> expected({0x59, 0, 0,    0, 0,   0,    0, 0x30, 0x6, 0, 0,   0,   0,   0,
                                 0,    0, 0x34, 0, 0x4, 0x80, 0, 0,    0,   0, 'n', 'a', 'm', 'e',
                                 0,    0, 0,    0, 0x3, 0,    0, 0,    0,   0, 0,   0});
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

  fuchsia::diagnostics::stream::Record record{.timestamp = 6,
                                              .severity = fuchsia::diagnostics::Severity::INFO,
                                              .arguments = std::move(args)};
  streams::log_record(record, &vec);
  std::vector<uint8_t> expected({0x59, 0, 0,    0,    0,    0,    0,    0x30, 0x6,  0,
                                 0,    0, 0,    0,    0,    0,    0x35, 0,    0x4,  0x80,
                                 0,    0, 0,    0,    'n',  'a',  'm',  'e',  0,    0,
                                 0,    0, 0x6F, 0x12, 0x83, 0xC0, 0xCA, 0x21, 0x09, 0x40});
  EXPECT_EQ(vec, expected);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
