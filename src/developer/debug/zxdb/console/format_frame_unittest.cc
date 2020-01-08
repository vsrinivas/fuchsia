// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_frame.h"

#include "gtest/gtest.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/console/async_output_buffer_test_util.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/format_location.h"
#include "src/developer/debug/zxdb/console/format_node_console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"

namespace zxdb {

namespace {

class FormatFrameTest : public TestWithLoop {
 public:
  // Synchronous wrapper around asynchronous long formatting.
  std::string SyncFormatFrame(const Frame* frame, const FormatFrameOptions& opts, int id = -1) {
    return LoopUntilAsyncOutputBufferComplete(FormatFrame(frame, opts, id)).AsString();
  }
};

}  // namespace

TEST_F(FormatFrameTest, Unsymbolized) {
  MockFrame frame(nullptr, nullptr, Location(Location::State::kSymbolized, 0x12345678), 0x567890, 0,
                  std::vector<debug_ipc::Register>(), 0xdeadbeef);

  // Short format just prints the address.
  FormatFrameOptions simple_opts;
  simple_opts.detail = FormatFrameOptions::kSimple;
  EXPECT_EQ("0x12345678", SyncFormatFrame(&frame, simple_opts));

  // Long version should do the same.
  FormatFrameOptions verbose_opts;
  verbose_opts.detail = FormatFrameOptions::kVerbose;
  EXPECT_EQ("0x12345678\n      IP = 0x12345678, SP = 0x567890, base = 0xdeadbeef",
            SyncFormatFrame(&frame, verbose_opts));

  // Simple, with index.
  EXPECT_EQ("Frame 3 0x12345678", SyncFormatFrame(&frame, simple_opts, 3));
}

TEST_F(FormatFrameTest, Inline) {
  // This is to have some place for the inline frame to refer to as the underlying physical frame.
  // The values are ignored.
  MockFrame physical_frame(nullptr, nullptr, Location(Location::State::kSymbolized, 0x12345678),
                           0x567890);

  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kInlinedSubroutine);
  function->set_assigned_name("Function");

  MockFrame inline_frame(
      nullptr, nullptr, Location(0x12345678, FileLine("file.cc", 22), 0, symbol_context, function),
      0x567890, 0, std::vector<debug_ipc::Register>(), 0xdeadbeef, &physical_frame);

  FormatFrameOptions simple_opts;
  simple_opts.detail = FormatFrameOptions::kSimple;
  EXPECT_EQ("Function() • file.cc:22 (inline)", SyncFormatFrame(&inline_frame, simple_opts));

  FormatFrameOptions param_opts;
  param_opts.detail = FormatFrameOptions::kParameters;
  EXPECT_EQ("Function() • file.cc:22 (inline)", SyncFormatFrame(&inline_frame, param_opts));

  FormatFrameOptions verbose_opts;
  verbose_opts.detail = FormatFrameOptions::kVerbose;
  EXPECT_EQ(
      "Function() • file.cc:22 (inline)\n"
      "      IP = 0x12345678, SP = 0x567890, base = 0xdeadbeef",
      SyncFormatFrame(&inline_frame, verbose_opts));
}

}  // namespace zxdb
