// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pthread.h>
#include <threads.h>
#include <unwind.h>
#include <zircon/sanitizer.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <thread>

#include <zxtest/zxtest.h>

#include "backtrace.h"

namespace {

// This is set by the build system to indicate whether or not zxtest and libc
// can be relied on to have frame pointers.
constexpr bool kIncompleteFramePointers = INCOMPLETE_FRAME_POINTERS;

constexpr size_t kMaxTestFrames = 32;

using namespace std::literals;

// Test and compare three kinds of simple backtrace (PC list) collection:
//  * frame pointers (accessible via __sanitizer_fast_backtrace)
//  * shadow call stack (accessible via __sanitizer_fast_backtrace)
//  * metadata-based (DWARF CFI) unwinding (accessible via _Unwind_Backtrace)

using Backtrace = cpp20::span<uintptr_t>;
using Getter = size_t(Backtrace);

size_t BacktraceByUnwind(Backtrace buffer) {
  // The unwinder works by making callbacks for each frame from innermost to
  // outermost.  Each step adds one frame's PC to buffer and increments count.
  size_t count = 0;
  auto unwind_step = [&](_Unwind_Context* ctx) -> _Unwind_Reason_Code {
    // Short-circuit the unwinding when there's no space left for more PCs.
    // Skip the first step that reports our own call to _Unwind_Backtrace.
    if (count <= buffer.size() && count++ > 0) {
      // The count is now the number of steps, which is two past the next
      // buffer slot to write since we skipped the first step's frame.
      buffer[count - 2] = _Unwind_GetIP(ctx);
    }

    // Tell the unwinder to keep going and call again for the next frame unless
    // there's no more space.
    return count <= buffer.size() ? _URC_NO_REASON : _URC_NORMAL_STOP;
  };

  using UnwindStep = decltype(unwind_step);
  constexpr auto unwind_callback = [](_Unwind_Context* ctx, void* arg) {
    return (*static_cast<UnwindStep*>(arg))(ctx);
  };

  _Unwind_Backtrace(unwind_callback, &unwind_step);

  if (count > 0) {
    // We counted the first step but won't report it to the caller.
    --count;
  }

  EXPECT_GT(count, 0);

  auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
  EXPECT_EQ(caller, buffer.front());

  return count;
}

struct BacktraceMethod {
  Getter* getter;
  std::string_view name;
  bool enabled;
};

constexpr BacktraceMethod kByFramePointer = {
    __libc_sanitizer::BacktraceByFramePointer,
    "frame pointers",
    true,
};

constexpr BacktraceMethod kByShadowCallStack = {
    __libc_sanitizer::BacktraceByShadowCallStack,
    "shadow call stack",
    __has_feature(shadow_call_stack),
};

constexpr BacktraceMethod kByUnwind = {
    BacktraceByUnwind,
    "_Unwind_Backtrace",
    true,
};

constexpr size_t kFrameCount = 4;  // Foo -> Otter -> Outer -> Find

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

class Collector {
 public:
  explicit Collector(const BacktraceMethod& method) : method_(method) {}

  void Collect() {
    // Count the number of frames from this one back.
    baseline_ = method_.getter(buffer_);

    if (method_.enabled) {
      EXPECT_GT(baseline_, 0);
    } else {
      EXPECT_EQ(baseline_, 0);
    }

    // Now call down four frames: Foo -> Otter -> Outer -> Find
    count_ = Foo(buffer_, method_.getter);

    // Adjust for the increment done in each frame.  Those prevented the
    // compiler from optimizing them into tail calls.
    ASSERT_GE(count_, kFrameCount);
    count_ -= kFrameCount;
  }

  void CollectC11Thread() {
    context_ = "thrd_create"sv;
    thrd_t t;
    ASSERT_EQ(thrd_create(
                  &t,
                  [](void* arg) -> int {
                    static_cast<Collector*>(arg)->Collect();
                    return 0;
                  },
                  this),
              thrd_success);
    int ret;
    EXPECT_EQ(thrd_join(t, &ret), thrd_success);
    EXPECT_EQ(ret, 0);
  }

  void CollectPThread() {
    context_ = "pthread_create"sv;
    pthread_t t;
    ASSERT_EQ(pthread_create(
                  &t, nullptr,
                  [](void* arg) -> void* {
                    static_cast<Collector*>(arg)->Collect();
                    return nullptr;
                  },
                  this),
              0);
    void* ret;
    EXPECT_EQ(pthread_join(t, &ret), 0);
    EXPECT_NULL(ret);
  }

  void CollectCppThread() {
    context_ = "std::thread"sv;
    std::thread(&Collector::Collect, this).join();
  }

  void Check() {
    Print();

    // Check that we got the right number.
    if (method_.enabled) {
      EXPECT_EQ(count_, baseline_ + kFrameCount);
    } else {
      EXPECT_EQ(count_, 0);
    }
  }

  void Print() {
    if (!backtrace().empty()) {
      std::string message("Test backtrace ("sv);
      message += context_;
      message += ", ";
      message += method_.name;
      message += "):\n";
      __sanitizer_log_write(message.data(), message.size());

      unsigned int n = 0;
      for (uintptr_t pc : backtrace()) {
        char buf[64];
        snprintf(buf, sizeof(buf), "{{{bt:%u:%#zx}}}\n", n++, pc);
        __sanitizer_log_write(buf, strlen(buf));
      }
    }
  }

  Backtrace backtrace() { return Backtrace(buffer_).subspan(0, count_); }
  size_t baseline() const { return baseline_; }
  size_t count() const { return count_; }

 private:
  const BacktraceMethod& method_;
  std::string_view context_ = "initial thread"sv;
  std::array<uintptr_t, kMaxTestFrames> buffer_;
  size_t baseline_ = 0, count_ = 0;
};

TEST(SanitizerFastBacktraceTests, BacktraceByFramePointer) {
  Collector bt(kByFramePointer);
  ASSERT_NO_FATAL_FAILURE(bt.Collect());
  ASSERT_NO_FATAL_FAILURE(bt.Check());
}

TEST(SanitizerFastBacktraceTests, BacktraceByShadowCallStack) {
  Collector bt(kByShadowCallStack);
  ASSERT_NO_FATAL_FAILURE(bt.Collect());
  ASSERT_NO_FATAL_FAILURE(bt.Check());
}

TEST(SanitizerFastBacktraceTests, BacktraceByUnwind) {
  Collector bt(kByUnwind);
  ASSERT_NO_FATAL_FAILURE(bt.Collect());
  ASSERT_NO_FATAL_FAILURE(bt.Check());
}

TEST(SanitizerFastBacktraceTests, C11ThreadBacktraceByFramePointer) {
  Collector bt(kByFramePointer);
  ASSERT_NO_FATAL_FAILURE(bt.CollectC11Thread());
  ASSERT_NO_FATAL_FAILURE(bt.Check());
}

TEST(SanitizerFastBacktraceTests, C11ThreadBacktraceByShadowCallStack) {
  Collector bt(kByShadowCallStack);
  ASSERT_NO_FATAL_FAILURE(bt.CollectC11Thread());
  ASSERT_NO_FATAL_FAILURE(bt.Check());
}

TEST(SanitizerFastBacktraceTests, C11ThreadBacktraceByUnwind) {
  Collector bt(kByUnwind);
  ASSERT_NO_FATAL_FAILURE(bt.CollectC11Thread());
  ASSERT_NO_FATAL_FAILURE(bt.Check());
}

TEST(SanitizerFastBacktraceTests, PThreadBacktraceByFramePointer) {
  Collector bt(kByFramePointer);
  ASSERT_NO_FATAL_FAILURE(bt.CollectPThread());
  ASSERT_NO_FATAL_FAILURE(bt.Check());
}

TEST(SanitizerFastBacktraceTests, PThreadBacktraceByShadowCallStack) {
  Collector bt(kByShadowCallStack);
  ASSERT_NO_FATAL_FAILURE(bt.CollectPThread());
  ASSERT_NO_FATAL_FAILURE(bt.Check());
}

TEST(SanitizerFastBacktraceTests, PThreadBacktraceByUnwind) {
  Collector bt(kByUnwind);
  ASSERT_NO_FATAL_FAILURE(bt.CollectPThread());
  ASSERT_NO_FATAL_FAILURE(bt.Check());
}

TEST(SanitizerFastBacktraceTests, CppThreadBacktraceByFramePointer) {
  Collector bt(kByFramePointer);
  ASSERT_NO_FATAL_FAILURE(bt.CollectCppThread());
  ASSERT_NO_FATAL_FAILURE(bt.Check());
}

TEST(SanitizerFastBacktraceTests, CppThreadBacktraceByShadowCallStack) {
  Collector bt(kByShadowCallStack);
  ASSERT_NO_FATAL_FAILURE(bt.CollectCppThread());
  ASSERT_NO_FATAL_FAILURE(bt.Check());
}

TEST(SanitizerFastBacktraceTests, CppThreadBacktraceByUnwind) {
  Collector bt(kByUnwind);
  ASSERT_NO_FATAL_FAILURE(bt.CollectCppThread());
  ASSERT_NO_FATAL_FAILURE(bt.Check());
}

void ExpectMatch(Collector& fp_collector, Collector& scs_collector, Collector& unw_collector,
                 size_t expected_diffs = 0, bool fp_maybe_incomplete = false) {
  constexpr auto count_differences = [](Backtrace a, Backtrace b) -> size_t {
    if (a.size() > b.size()) {
      return a.size() - b.size();
    }
    if (b.size() > a.size()) {
      return b.size() - a.size();
    }
    size_t differences = 0;
    for (size_t i = 0; i < a.size(); ++i) {
      if (a[i] != b[i]) {
        ++differences;
      }
    }
    return differences;
  };

  Backtrace fp = fp_collector.backtrace();
  Backtrace scs = scs_collector.backtrace();
  Backtrace unw = unw_collector.backtrace();

  EXPECT_GT(fp.size(), kFrameCount);
  EXPECT_GT(unw.size(), kFrameCount);

  // If zxtest doesn't use frame pointers, the FP backtrace may be incomplete
  // but won't necessarily just be truncated.  Since libc always synthesizes
  // frame pointers for the outermost frames of the initial thread, then if
  // zxtest's frames don't use proper frame pointers but also don't happen to
  // clobber the frame pointer register, then the FP backtrace might just skip
  // its frames rather than being truncated at the innermost FP-lacking frame.
  // Hence all we can guarantee is the frames within this file.
  Backtrace unw_vs_fp = unw, fp_vs_unw = fp;
  Backtrace scs_vs_fp = scs, fp_vs_scs = fp;
  if (fp_maybe_incomplete) {
    size_t reliable_frames = std::min(fp.size(), kFrameCount + 1);
    if (fp.size() < unw.size()) {
      unw_vs_fp = unw.subspan(0, reliable_frames);
      fp_vs_unw = fp.subspan(0, reliable_frames);
    }
    if (fp.size() < scs.size()) {
      scs_vs_fp = scs.subspan(0, reliable_frames);
      fp_vs_scs = fp.subspan(0, reliable_frames);
    }
  }

  // The two backtraces should be identical except for one slightly different
  // return address in the frame that invoked the collections.  In the threaded
  // cases, they're completely identical.  This assertion failure won't
  // generate any helpful explanation of the differences, but the two
  // backtraces will have appeared in the sanitizer log output for comparison.
  EXPECT_EQ(fp_vs_unw.size(), unw_vs_fp.size());
  if (fp_vs_unw.size() == fp.size()) {
    EXPECT_EQ(count_differences(unw_vs_fp, fp_vs_unw), expected_diffs);
  } else {
    EXPECT_LE(count_differences(unw_vs_fp, fp_vs_unw), expected_diffs);
  }

  // The differences shouldn't be in the outermost or innermost frames.
  EXPECT_EQ(fp_vs_unw.front(), unw_vs_fp.front());
  EXPECT_EQ(fp_vs_unw.back(), unw_vs_fp.back());

  if (kByShadowCallStack.enabled) {
    EXPECT_GT(fp_vs_scs.size(), kFrameCount);

    EXPECT_EQ(fp_vs_scs.size(), scs_vs_fp.size());
    if (fp_vs_unw.size() == fp.size()) {
      EXPECT_EQ(count_differences(scs_vs_fp, fp_vs_scs), expected_diffs);
    } else {
      EXPECT_LE(count_differences(scs_vs_fp, fp_vs_scs), expected_diffs);
    }
    EXPECT_EQ(fp_vs_scs.front(), scs_vs_fp.front());
    EXPECT_EQ(fp_vs_scs.back(), scs_vs_fp.back());

    EXPECT_EQ(unw.size(), scs.size());
    EXPECT_EQ(expected_diffs, count_differences(scs, unw));
    EXPECT_EQ(unw.front(), scs.front());
    EXPECT_EQ(unw.back(), scs.back());
  } else {
    EXPECT_TRUE(scs.empty());
  }
}

TEST(SanitizerFastBacktraceTests, BacktraceMethodsMatch) {
  Collector fp(kByFramePointer);
  ASSERT_NO_FATAL_FAILURE(fp.Collect());

  Collector scs(kByShadowCallStack);
  ASSERT_NO_FATAL_FAILURE(scs.Collect());

  Collector unw(kByUnwind);
  ASSERT_NO_FATAL_FAILURE(unw.Collect());

  // The sole difference should be the return address for this frame itself,
  // where the different Collect() call sites are.  Additionally, the initial
  // thread's callers outside this file might omit the frame pointers.
  ASSERT_NO_FATAL_FAILURE(ExpectMatch(fp, scs, unw, 1, kIncompleteFramePointers));
}

TEST(SanitizerFastBacktraceTests, C11ThreadBacktraceMethodsMatch) {
  Collector fp(kByFramePointer);
  ASSERT_NO_FATAL_FAILURE(fp.CollectC11Thread());

  Collector scs(kByShadowCallStack);
  ASSERT_NO_FATAL_FAILURE(scs.CollectC11Thread());

  Collector unw(kByUnwind);
  ASSERT_NO_FATAL_FAILURE(unw.CollectC11Thread());

  ASSERT_NO_FATAL_FAILURE(ExpectMatch(fp, scs, unw));
}

TEST(SanitizerFastBacktraceTests, PThreadBacktraceMethodsMatch) {
  Collector fp(kByFramePointer);
  ASSERT_NO_FATAL_FAILURE(fp.CollectPThread());

  Collector scs(kByShadowCallStack);
  ASSERT_NO_FATAL_FAILURE(scs.CollectPThread());

  Collector unw(kByUnwind);
  ASSERT_NO_FATAL_FAILURE(unw.CollectPThread());

  ASSERT_NO_FATAL_FAILURE(ExpectMatch(fp, scs, unw));
}

TEST(SanitizerFastBacktraceTests, CppThreadBacktraceMethodsMatch) {
  Collector fp(kByFramePointer);
  ASSERT_NO_FATAL_FAILURE(fp.CollectCppThread());

  Collector scs(kByShadowCallStack);
  ASSERT_NO_FATAL_FAILURE(scs.CollectCppThread());

  Collector unw(kByUnwind);
  ASSERT_NO_FATAL_FAILURE(unw.CollectCppThread());

  ASSERT_NO_FATAL_FAILURE(ExpectMatch(fp, scs, unw));
}

}  // namespace
