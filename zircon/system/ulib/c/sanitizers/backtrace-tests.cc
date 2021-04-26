// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/sanitizer.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>

#include <zxtest/zxtest.h>

#include "backtrace.h"

namespace {

#if __has_feature(shadow_call_stack)
constexpr bool kHaveShadowCallStack = true;
#else
constexpr bool kHaveShadowCallStack = false;
#endif

using Backtrace = cpp20::span<uintptr_t>;
using Getter = size_t(Backtrace);

constexpr size_t kFrameCount = 4;  // Foo -> Otter -> Outer -> Find

void Print(Backtrace bt) {
  if (bt.empty()) {
    return;
  }
  constexpr std::string_view kMessage{"Test backtrace:\n"};
  __sanitizer_log_write(kMessage.data(), kMessage.size());
  unsigned int n = 0;
  for (uintptr_t pc : bt) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{{{bt:%u:%#zx}}}\n", n++, pc);
    __sanitizer_log_write(buf, strlen(buf));
  }
}

[[gnu::noinline]] size_t Find(Backtrace backtrace, Getter* getter) {
  // Now actually collect the backtrace.  This and its callers all increment
  // the return value just to prevent the compiler from optimizing these all
  // into tail calls that don't preserve the frames normally.
  return getter(backtrace) + 1;
}

[[gnu::noinline]] size_t Outer(Backtrace backtrace, Getter* getter) {
  return Find(backtrace, getter) + 1;
}

[[gnu::noinline]] size_t Otter(Backtrace backtrace, Getter* getter) {
  return Outer(backtrace, getter) + 1;
}

[[gnu::noinline]] size_t Foo(Backtrace backtrace, Getter* getter) {
  return Otter(backtrace, getter) + 1;
}

TEST(SanitizerFastBacktraceTests, BacktraceByFramePointer) {
  std::array<uintptr_t, 16> buffer;
  Backtrace backtrace(buffer);

  // Count the number of frames from this one back.
  const size_t baseline = __libc_sanitizer::BacktraceByFramePointer(backtrace);

  // Now call down four frames: Foo -> Otter -> Outer -> Find
  size_t count = Foo(backtrace, __libc_sanitizer::BacktraceByFramePointer);

  // Adjust for the increment done in each frame.  Those prevented the compiler
  // from optimizing them into tail calls.
  ASSERT_GE(count, kFrameCount);
  count -= kFrameCount;

  backtrace = backtrace.subspan(0, count);
  Print(backtrace);

  // Count that we got the right number.
  EXPECT_EQ(count, baseline + kFrameCount);
}

TEST(SanitizerFastBacktraceTests, BacktraceByShadowCallStack) {
  std::array<uintptr_t, 16> buffer;
  Backtrace backtrace(buffer);

  // Count the number of frames from this one back.
  const size_t baseline = __libc_sanitizer::BacktraceByShadowCallStack(backtrace);
  if (kHaveShadowCallStack) {
    EXPECT_GT(baseline, 0);
  } else {
    EXPECT_EQ(baseline, 0);
  }

  // Now call down four frames:
  size_t count = Foo(backtrace, __libc_sanitizer::BacktraceByShadowCallStack);

  // Adjust for the increment done in each frame.  Those prevented the compiler
  // from optimizing them into tail calls.
  ASSERT_GE(count, kFrameCount);
  count -= kFrameCount;

  backtrace = backtrace.subspan(0, count);
  Print(backtrace);

  if (kHaveShadowCallStack) {
    // Count that we got the right number.
    EXPECT_EQ(count, baseline + kFrameCount);
  } else {
    EXPECT_EQ(count, 0);
  }
}

}  // namespace
