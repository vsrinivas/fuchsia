// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/unwinder/unwind_local.h"

#include <cstdint>
#include <cstdio>

#include <gtest/gtest.h>

namespace unwinder {

namespace {

// Unfortunately we won't know the size of the function but we can approximate that.
// This should be large enough to hold each function.
constexpr size_t kFunctionSize = 0x80;

[[clang::optnone]] std::vector<Frame> f1() { return UnwindLocal(); }
[[clang::optnone]] std::vector<Frame> f2() { return f1(); }
[[clang::optnone]] std::vector<Frame> f3() { return f2(); }

}  // namespace

TEST(Unwinder, UnwindLocal) {
  auto frames = f3();

  for (auto& frame : frames) {
    ASSERT_TRUE(frame.trust == Frame::Trust::kCFI);
  }

  ASSERT_GT(frames.size(), 3UL);

  uint64_t pc;

  ASSERT_TRUE(frames[0].regs.GetPC(pc).ok());
  ASSERT_GT(pc, reinterpret_cast<uint64_t>(f1));
  ASSERT_LT(pc, reinterpret_cast<uint64_t>(f1) + kFunctionSize);

  ASSERT_TRUE(frames[1].regs.GetPC(pc).ok());
  ASSERT_GT(pc, reinterpret_cast<uint64_t>(f2));
  ASSERT_LT(pc, reinterpret_cast<uint64_t>(f2) + kFunctionSize);

  ASSERT_TRUE(frames[2].regs.GetPC(pc).ok());
  ASSERT_GT(pc, reinterpret_cast<uint64_t>(f3));
  ASSERT_LT(pc, reinterpret_cast<uint64_t>(f3) + kFunctionSize);
}

}  // namespace unwinder
