// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/coroutine/context/context.h"

#include "gtest/gtest.h"
#include "lib/ftl/logging.h"

namespace context {

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
  auto runnable = reinterpret_cast<std::function<void()>*>(data);
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
  std::function<void()> runnable = [&]() {
    f = Fact(5);
    va_args_result = UseMMX("Hello %d %d\n", 1, 2);
    SetContext(&old_context);
  };

  MakeContext(&new_context, &stack, &RunInContext, &runnable);

  SwapContext(&old_context, &new_context);

  EXPECT_EQ(120u, f);
  EXPECT_EQ(10, va_args_result);
}

#if __has_feature(safe_stack)
void TrashStack(void* context) {
  char buffer[1024];
  for (size_t i = 0; i < 6; ++i) {
    buffer[Fact(i)] = 1;
  }

  SetContext(reinterpret_cast<Context*>(context));
}

TEST(Context, MakeContextUnsafeStack) {
  Stack stack;
  memset(stack.unsafe_stack(), 0, stack.stack_size());

  Context new_context;
  Context old_context;

  EXPECT_TRUE(GetContext(&new_context));
  MakeContext(&new_context, &stack, &TrashStack, &old_context);

  SwapContext(&old_context, &new_context);

  bool found = false;
  char* ptr = reinterpret_cast<char*>(stack.unsafe_stack());
  for (size_t i = 0; i < stack.stack_size(); ++i) {
    found = found || *(ptr + i);
  }
  EXPECT_TRUE(found);
}
#endif

}  // namespace
}  // namespace context
