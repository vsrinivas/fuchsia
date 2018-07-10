// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/mock_frame.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/bin/zxdb/console/format_frame.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(FormatFrame, Unsymbolized) {
  debug_ipc::StackFrame stack_frame;
  stack_frame.ip = 0x12345678;
  stack_frame.sp = 0x567890;

  MockFrame frame(nullptr, nullptr, stack_frame, Location(Location::State::kSymbolized, stack_frame.ip));

  OutputBuffer out;

  // Short format just prints the address.
  FormatFrame(&frame, &out, false);
  EXPECT_EQ("0x12345678", out.AsString());

  // Long version should do the same (not duplicate it).
  out = OutputBuffer();
  FormatFrame(&frame, &out, true);
  EXPECT_EQ("0x12345678", out.AsString());

  // With index.
  out = OutputBuffer();
  FormatFrame(&frame, &out, true, 3);
  EXPECT_EQ("Frame 3 0x12345678", out.AsString());
}

}  // namespace zxdb
