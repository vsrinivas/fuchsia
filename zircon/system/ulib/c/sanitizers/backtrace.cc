// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backtrace.h"

#include "threads_impl.h"

namespace __libc_sanitizer {

size_t BacktraceByFramePointer(cpp20::span<uintptr_t> pcs) {
  struct FramePointer {
    const FramePointer* fp;
    uintptr_t pc;
  };

  auto on_stack = [&stack = __pthread_self()->safe_stack](const FramePointer* fp) -> bool {
    uintptr_t address = reinterpret_cast<uintptr_t>(fp);
    return address >= reinterpret_cast<uintptr_t>(stack.iov_base) &&
           address < reinterpret_cast<uintptr_t>(stack.iov_base) + stack.iov_len;
  };

  uintptr_t ra = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
  auto fp = reinterpret_cast<const FramePointer*>(__builtin_frame_address(0));
  size_t i = 0;
  while (i < pcs.size() && on_stack(fp) && fp->pc != 0) {
    if (i == 0 && fp->pc != ra) {
      pcs[i++] = ra;
    } else {
      pcs[i++] = fp->pc;
      fp = fp->fp;
    }
  }
  if (i == 0 && i < pcs.size()) {
    pcs[i++] = ra;
  }

  return i;
}

#if __has_feature(shadow_call_stack)

namespace {

// This is actually defined with internal linkage in asm, below.
// It's defined inside the function so it can travel in its section group.
extern "C" uintptr_t GetShadowCallStackPointer();

}  // namespace

size_t BacktraceByShadowCallStack(cpp20::span<uintptr_t> pcs) {
  // Fetch the current shadow call stack pointer.  This isn't done
  // with direct inline asm so that we can be sure that the compiler
  // has pushed our own frame's return address before we collect it.
  uintptr_t sp = GetShadowCallStackPointer();
#ifdef __aarch64__
  __asm__(
      R"""(
      .pushsection .text.GetShadowCallStackPointer, "ax?", %progbits
      .type GetShadowCallStackPointer, %function
      GetShadowCallStackPointer:
        mov x0, x18
        ret
      .size GetShadowCallStackPointer, . - GetShadowCallStackPointer
      .popsection
      )""");
#else
#error "what machine?"
#endif

  const iovec& stack_block = __pthread_self()->shadow_call_stack;
  uintptr_t stack_base = reinterpret_cast<uintptr_t>(stack_block.iov_base);
  uintptr_t stack_limit = stack_base + stack_block.iov_len;

  if (sp < stack_base || sp > stack_limit || sp % sizeof(uintptr_t) != 0) {
    return 0;
  }

  const uintptr_t* next_pc = reinterpret_cast<const uintptr_t*>(sp);
  const uintptr_t* last_pc = reinterpret_cast<const uintptr_t*>(stack_base);
  size_t i = 0;
  while (i < pcs.size() && next_pc > last_pc && *--next_pc != 0) {
    pcs[i++] = *next_pc;
  }
  return i;
}

#endif  // __has_feature(shadow_call_stack)

}  // namespace __libc_sanitizer
