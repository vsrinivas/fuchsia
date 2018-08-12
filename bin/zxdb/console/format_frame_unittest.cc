// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_frame.h"
#include "garnet/bin/zxdb/client/mock_frame.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/bin/zxdb/console/format_value.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/lib/debug_ipc/helper/platform_message_loop.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

// Synchronous wrapper around asynchronous long formatting.
std::string SyncFormatFrameLong(const Frame* frame,
                                const FormatValueOptions& options) {
  debug_ipc::PlatformMessageLoop loop;
  loop.Init();

  auto helper = fxl::MakeRefCounted<ValueFormatHelper>();
  FormatFrameLong(frame, helper.get(), FormatValueOptions());

  std::string out_string;
  bool complete = false;
  helper->Complete([&loop, &out_string, &complete](OutputBuffer out) {
    complete = true;
    out_string = out.AsString();
    loop.QuitNow();
  });

  if (!complete) {
    // Did not complete synchronously, run the loop.
    loop.Run();
  }

  EXPECT_TRUE(complete);

  loop.Cleanup();
  return out_string;
}

}  // namespace

TEST(FormatFrame, Unsymbolized) {
  debug_ipc::StackFrame stack_frame;
  stack_frame.ip = 0x12345678;
  stack_frame.sp = 0x567890;

  MockFrame frame(nullptr, nullptr, stack_frame,
                  Location(Location::State::kSymbolized, stack_frame.ip));

  OutputBuffer out;

  // Short format just prints the address.
  FormatFrame(&frame, &out);
  EXPECT_EQ("0x12345678", out.AsString());

  // Long version should do the same (not duplicate it).
  EXPECT_EQ("0x12345678", SyncFormatFrameLong(&frame, FormatValueOptions()));

  // With index.
  out = OutputBuffer();
  FormatFrame(&frame, &out, 3);
  EXPECT_EQ("Frame 3 0x12345678", out.AsString());
}

}  // namespace zxdb
