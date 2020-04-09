// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/diagnostics/stream/cpp/fidl.h>
#include <lib/zx/clock.h>

#include <vector>

#include "gtest/gtest.h"
#include "src/diagnostics/lib/streams/encode.h"

TEST(StreamsRecordEncoder, Writable) {
  // Create Buffer
  std::vector<uint8_t> vec;
  // Create Record
  fuchsia::diagnostics::stream::Value value_;
  fuchsia::diagnostics::stream::Argument arg{.name = "arg_name", .value = std::move(value_)};
  std::vector<fuchsia::diagnostics::stream::Argument> args;
  args.push_back(std::move(arg));
  fuchsia::diagnostics::stream::Record record{.timestamp = 12, .arguments = std::move(args)};
  // Write Record
  streams::log_record(record, &vec);
  int length = vec.size();
  ASSERT_NE(length, 0);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
