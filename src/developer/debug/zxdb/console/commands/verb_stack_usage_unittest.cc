// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_stack_usage.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/console/console_test.h"

namespace zxdb {

namespace {

class VerbStackUsage : public ConsoleTest {};

}  // namespace

TEST_F(VerbStackUsage, GetThreadStackUsage) {
  constexpr uint64_t kPageSize = 4096;

  constexpr uint64_t kStackBase = 0x100000;  // HIGH address of stack region.
  constexpr uint64_t kStackSizeInPages = 20;
  constexpr uint64_t kStackSize = kStackSizeInPages * kPageSize;

  constexpr uint64_t kUsedBytes = 0x5e;
  constexpr uint64_t kStackPointer = kStackBase - kUsedBytes;

  // Set up the thread to be stopped with the given stack pointer.
  std::vector<std::unique_ptr<Frame>> frames;
  Location location(Location::State::kSymbolized, 0x1000);
  frames.push_back(std::make_unique<MockFrame>(&session(), thread(), location, kStackPointer));
  InjectExceptionWithStack(ConsoleTest::kProcessKoid, ConsoleTest::kThreadKoid,
                           debug_ipc::ExceptionType::kSingleStep, std::move(frames), true);

  std::vector<debug_ipc::AddressRegion> aspace;
  debug_ipc::AddressRegion& containing_region = aspace.emplace_back();
  containing_region.base = 0;
  containing_region.size = 0x100000000000ull;
  containing_region.depth = 0;

  debug_ipc::AddressRegion& other_region = aspace.emplace_back();
  other_region.base = 0x1000;
  other_region.size = 0x1000;
  other_region.depth = 1;
  other_region.vmo_koid = 1234;

  debug_ipc::AddressRegion& stack_region = aspace.emplace_back();
  stack_region.base = kStackBase - kStackSize;
  stack_region.size = kStackSize;
  stack_region.depth = 1;
  stack_region.vmo_koid = 45678;
  constexpr uint64_t kCommittedPages = 3;
  stack_region.committed_pages = kCommittedPages;

  // Good query.
  ThreadStackUsage usage = GetThreadStackUsage(&console().context(), aspace, thread());
  EXPECT_EQ(usage.total, kStackSize);
  EXPECT_EQ(usage.used, kUsedBytes);
  EXPECT_EQ(usage.committed, kCommittedPages * kPageSize);
  EXPECT_EQ(usage.wasted, (kCommittedPages - 1) * kPageSize);  // Actually used one page.
}

}  // namespace zxdb
