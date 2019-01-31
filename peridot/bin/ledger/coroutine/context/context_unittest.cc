// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/coroutine/context/context.h"

#include <string.h>

#include <lib/fit/function.h>
#include <lib/fxl/compiler_specific.h>
#include <lib/fxl/logging.h>

#include "gtest/gtest.h"

namespace context {

#if __has_feature(safe_stack)
char* GetUnsafeStackForTest(const Stack& stack) {
  return reinterpret_cast<char*>(stack.unsafe_stack());
}
#endif

namespace {

// Function using variable args to generate mmx code on x86_64. Running this
// without crashing ensures that the stack is correctly aligned.
int UseMMX(const char* format, ...) {
  va_list va;
  va_start(va, format);
  int result = vsnprintf(nullptr, 0, format, va);
  va_end(va);
  return result;
}

size_t Fact(size_t n) {
  if (n == 0) {
    return 1;
  }
  return n * Fact(n - 1);
}

void RunInContext(void* data) {
  auto runnable = reinterpret_cast<fit::function<void()>*>(data);
  (*runnable)();
}

TEST(Context, GetContext) {
  Context context;
  EXPECT_TRUE(GetContext(&context));
}

TEST(Context, SetContext) {
  Context context;
  volatile size_t nb_calls = 0;

  volatile bool result = GetContext(&context);
  ++nb_calls;
  if (result) {
    SetContext(&context);
  }

  EXPECT_EQ(2u, nb_calls);
}

TEST(Context, MakeContext) {
  Stack stack;

  Context new_context;
  Context old_context;

  size_t f = 0u;
  int va_args_result = 0;
  fit::function<void()> runnable = [&]() {
    f = Fact(5);
    va_args_result = UseMMX("Hello %d %d\n", 1, 2);
    SetContext(&old_context);
  };

  MakeContext(&new_context, &stack, &RunInContext, &runnable);

  SwapContext(&old_context, &new_context);

  EXPECT_EQ(120u, f);
  EXPECT_EQ(10, va_args_result);
}

struct ThreadLocalContext {
  static thread_local char* thread_local_ptr;

  char* ptr = nullptr;
  Context old_context;
};

thread_local char* ThreadLocalContext::thread_local_ptr = nullptr;

void GetThreadLocalPointer(void* context) {
  auto thread_local_context = reinterpret_cast<ThreadLocalContext*>(context);
  thread_local_context->ptr = ThreadLocalContext::thread_local_ptr;
  SetContext(&thread_local_context->old_context);
}

TEST(Context, ThreadLocal) {
  Stack stack;
  ThreadLocalContext context;

  char c = 'a';
  ThreadLocalContext::thread_local_ptr = &c;

  Context new_context;

  EXPECT_TRUE(GetContext(&new_context));
  MakeContext(&new_context, &stack, &GetThreadLocalPointer, &context);

  SwapContext(&context.old_context, &new_context);

  EXPECT_EQ(&c, context.ptr);
}

#if __has_feature(safe_stack)
// Force to set the pointed address to 1. This must be no-inline to prevent the
// compiler to optimize away the set.
FXL_NOINLINE void ForceSet(volatile char* addr) { *addr = 1; }

// Write some data to the unsafe stack.
void TrashStack(void* context) {
  volatile char buffer[1024];
  for (size_t i = 0; i < 6; ++i) {
    ForceSet(buffer + Fact(i));
  }

  SetContext(reinterpret_cast<Context*>(context));
}

TEST(Context, MakeContextUnsafeStack) {
  Stack stack;
  memset(GetUnsafeStackForTest(stack), 0, stack.stack_size());

  Context new_context;
  Context old_context;

  EXPECT_TRUE(GetContext(&new_context));
  MakeContext(&new_context, &stack, &TrashStack, &old_context);

  SwapContext(&old_context, &new_context);

  bool found = false;
  char* ptr = GetUnsafeStackForTest(stack);
  for (size_t i = 0; i < stack.stack_size(); ++i) {
    found = found || *(ptr + i);
  }
  EXPECT_TRUE(found);
}

__NO_SAFESTACK intptr_t GetSafeStackPointer() {
  char a = 0;
  // Suppress check about returning a stack memory address.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-stack-address"
  return reinterpret_cast<intptr_t>(&a);  // NOLINT
#pragma clang diagnostic pop
}

void CheckDistinctStack(void* context) {
  char buff[1];
  memset(buff, 0, sizeof(buff));
  // buff is on the unsafe stack, GetSafeStackPointer() returns a value on the
  // safe stack. This checks that the address of the 2 stacks are separated at
  // least by 2 PAGE_SIZE, given that each stack has a guard.
  EXPECT_GE(std::abs(reinterpret_cast<intptr_t>(buff) - GetSafeStackPointer()),
            2 * PAGE_SIZE);

  SetContext(reinterpret_cast<Context*>(context));
}

TEST(Context, CheckStacksAreDifferent) {
  Stack stack;

  Context new_context;
  Context old_context;

  EXPECT_TRUE(GetContext(&new_context));
  MakeContext(&new_context, &stack, &CheckDistinctStack, &old_context);

  SwapContext(&old_context, &new_context);
}
#endif

}  // namespace
}  // namespace context
