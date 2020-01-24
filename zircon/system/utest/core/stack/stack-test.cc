// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <runtime/tls.h>
#include <zxtest/zxtest.h>

namespace {

// We request one-page stacks, so collisions are easy to catch.
uintptr_t PageOf(const void* ptr) { return reinterpret_cast<uintptr_t>(ptr) & -PAGE_SIZE; }

struct StackTestInfo {
  bool is_pthread;
  char** environ;
  const void* safe_stack;
  const char* unsafe_stack;
  const char* tls_buf;
  const void* tp;
  const void* unsafe_start;
  const void* unsafe_ptr;
  const void* unsafe_end;
  const void* scs_ptr;
};

void* DoStackTest(void* arg) {
  StackTestInfo* info = reinterpret_cast<StackTestInfo*>(arg);

  info->safe_stack = __builtin_frame_address(0);

  // The compiler sees this pointer escape, so it should know
  // that this belongs on the unsafe stack.
  char unsafe_stack[64];
  zx_system_get_version(unsafe_stack, sizeof(unsafe_stack));

  // Likewise, the tls_buf is used.
  static thread_local char tls_buf[64];
  zx_system_get_version(tls_buf, sizeof(tls_buf));

  info->tp = zxr_tp_get();

  info->environ = environ;
  info->unsafe_stack = unsafe_stack;
  info->tls_buf = tls_buf;

#if __has_feature(safe_stack)
  info->unsafe_start = __builtin___get_unsafe_stack_start();
  info->unsafe_ptr = __builtin___get_unsafe_stack_ptr();
  info->unsafe_end = __builtin___get_unsafe_stack_top();
#endif

#if __has_feature(shadow_call_stack)
#ifdef __aarch64__
  __asm__("mov %0, x18" : "=r"(info->scs_ptr));
#else
#error "what shadow-call-stack ABI??"
#endif
#endif

  return nullptr;
}

void CheckThreadStackInfo(StackTestInfo* info) {
  EXPECT_NOT_NULL(info->environ, "environ unset");
  EXPECT_NOT_NULL(info->safe_stack, "CFA is null");
  EXPECT_NOT_NULL(info->unsafe_stack, "local's taken address is null");
  EXPECT_NOT_NULL(info->tls_buf, "thread_local's taken address is null");

  if (__has_feature(safe_stack) || info->is_pthread) {
    EXPECT_NE(PageOf(info->safe_stack), PageOf(info->environ), "safe stack collides with environ");
  }

  // The environ array sits on the main thread's unsafe stack.  But we can't
  // verify that it does since it might not be on the same page. So just check
  // on the pThread
  if (info->is_pthread) {
    EXPECT_NE(PageOf(info->unsafe_stack), PageOf(info->environ),
              "unsafe stack collides with environ");
  }

  EXPECT_NE(PageOf(info->tls_buf), PageOf(info->environ), "TLS collides with environ");

  EXPECT_NE(PageOf(info->tls_buf), PageOf(info->safe_stack), "TLS collides with safe stack");

  EXPECT_NE(PageOf(info->tls_buf), PageOf(info->unsafe_stack), "TLS collides with unsafe stack");

  EXPECT_NE(PageOf(info->tp), PageOf(info->environ), "thread pointer collides with environ");

  EXPECT_NE(PageOf(info->tp), PageOf(info->safe_stack), "thread pointer collides with safe stack");

  EXPECT_NE(PageOf(info->tp), PageOf(info->unsafe_stack),
            "thread pointer collides with unsafe stack");

#if __has_feature(safe_stack)
  if (info->is_pthread) {
    EXPECT_EQ(PageOf(info->unsafe_start), PageOf(info->unsafe_ptr),
              "reported unsafe start and ptr not nearby");
  }

  EXPECT_LE(info->unsafe_start, info->unsafe_ptr,
            "unsafe ptr is out of bounds");

  EXPECT_LE(info->unsafe_ptr, info->unsafe_end,
            "unsafe ptr is out of bounds");

  EXPECT_EQ(PageOf(info->unsafe_stack), PageOf(info->unsafe_ptr),
            "unsafe stack and reported ptr not nearby");

  EXPECT_NE(PageOf(info->unsafe_stack), PageOf(info->safe_stack),
            "unsafe stack collides with safe stack");
#endif

#if __has_feature(shadow_call_stack)
  EXPECT_NOT_NULL(info->scs_ptr, "shadow call stack pointer not set");

  EXPECT_NE(PageOf(info->scs_ptr), PageOf(info->environ),
            "shadow call stack collides with environ");

  EXPECT_NE(PageOf(info->scs_ptr), PageOf(info->tls_buf),
            "shadow call stack collides with TLS");

  EXPECT_NE(PageOf(info->scs_ptr), PageOf(info->safe_stack),
            "shadow call stack collides with safe stack");

  EXPECT_NE(PageOf(info->scs_ptr), PageOf(info->unsafe_stack),
            "shadow call stack collides with unsafe stack");

  EXPECT_NE(PageOf(info->scs_ptr), PageOf(info->tp),
      "shadow call stack collides with thread pointer");
#elif defined(__clang__) && defined(__aarch64__)
#error "This test should always be built with -fsanitize=shadow-call-stack"
#endif
}

// This instance of the test is lossy, because it's possible
// one of our single stacks spans multiple pages.  We can't
// get the main thread's stack down to a single page because
// the unittest machinery needs more than that.
TEST(StackTest, MainThreadStack) {
  StackTestInfo info;
  info.is_pthread = false;

  DoStackTest(&info);

  CheckThreadStackInfo(&info);
}

// Spawn a thread with a one-page stack.
TEST(StackTest, ThreadStack) {
  EXPECT_LE(PTHREAD_STACK_MIN, PAGE_SIZE);

  pthread_attr_t attr;
  ASSERT_EQ(0, pthread_attr_init(&attr));
  ASSERT_EQ(0, pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN));
  pthread_t thread;

  StackTestInfo info;
  info.is_pthread = true;
  ASSERT_EQ(0, pthread_create(&thread, &attr, &DoStackTest, &info));
  ASSERT_EQ(0, pthread_join(thread, nullptr));
  CheckThreadStackInfo(&info);
}

}  // namespace
