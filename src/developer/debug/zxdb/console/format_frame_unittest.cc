// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_frame.h"

#include "gtest/gtest.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/client/mock_stack_delegate.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/stack.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/async_output_buffer_test_util.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console_test.h"
#include "src/developer/debug/zxdb/console/format_location.h"
#include "src/developer/debug/zxdb/console/format_node_console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"

namespace zxdb {

namespace {

class FormatFrameTest : public ConsoleTest {
 public:
  // Synchronous wrappers around asynchronous formatting.
  std::string SyncFormatStack(Thread* thread, const FormatStackOptions& opts) {
    return LoopUntilAsyncOutputBufferComplete(FormatStack(thread, false, opts)).AsString();
  }
  std::string SyncFormatFrame(const Frame* frame, const FormatFrameOptions& opts, int id = -1) {
    return LoopUntilAsyncOutputBufferComplete(FormatFrame(frame, opts, id)).AsString();
  }
};

}  // namespace

TEST_F(FormatFrameTest, FormatStack) {
  Stack& stack = thread()->GetStack();

  std::vector<std::unique_ptr<Frame>> frames;
  frames.push_back(std::make_unique<MockFrame>(nullptr, nullptr, 0x1001, 0x2001, "Function0",
                                               FileLine("file0.cc", 20)));
  frames.push_back(std::make_unique<MockFrame>(nullptr, nullptr, 0x1002, 0x2002, "Function1",
                                               FileLine("file1.cc", 21)));
  frames.push_back(std::make_unique<MockFrame>(nullptr, nullptr, 0x1003, 0x2003, "Function2",
                                               FileLine("file2.cc", 22)));
  frames.push_back(std::make_unique<MockFrame>(nullptr, nullptr, 0x1004, 0x2004, "Function3",
                                               FileLine("file3.cc", 23)));

  stack.SetFramesForTest(std::move(frames), true);

  FormatStackOptions opts;

  // No pretty formatting.
  EXPECT_EQ(
      "▶ 0 Function0() • file0.cc:20\n"
      "  1 Function1() • file1.cc:21\n"
      "  2 Function2() • file2.cc:22\n"
      "  3 Function3() • file3.cc:23\n",
      SyncFormatStack(thread(), opts));

  // Set up a PrettyStackManager to collapse the first two frames.
  auto manager = fxl::MakeRefCounted<PrettyStackManager>();
  std::vector<PrettyStackManager::StackGlob> matchers;
  matchers.push_back(PrettyStackManager::StackGlob(
      "Function0/1 Matcher",
      {PrettyFrameGlob::Func("Function0"), PrettyFrameGlob::Func("Function1")}));
  manager->SetMatchers(matchers);
  opts.pretty_stack = manager;

  // With pretty eliding of the first two frames.
  EXPECT_EQ(
      "▶ 0…1 «Function0/1 Matcher» (-r expands)\n"
      "  2 Function2() • file2.cc:22\n"
      "  3 Function3() • file3.cc:23\n",
      SyncFormatStack(thread(), opts));

  // Set the active frame to be frame 1. The matcher should no longer apply so the frame can be
  // shown.
  console().context().SetActiveFrameIdForThread(thread(), 1);
  EXPECT_EQ(
      "  0 Function0() • file0.cc:20\n"
      "▶ 1 Function1() • file1.cc:21\n"
      "  2 Function2() • file2.cc:22\n"
      "  3 Function3() • file3.cc:23\n",
      SyncFormatStack(thread(), opts));
}

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
