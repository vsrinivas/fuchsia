// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/coroutine/context/context.h"

#include "lib/fxl/logging.h"

static_assert(__has_feature(safe_stack),
              "Context code must be compiled with safe stack");

namespace context {

void SwapContext(Context* out_context, Context* in_context) {
  if (!GetContext(out_context)) {
    // Returning from a SetContext, nothing else to do.
    return;
  }
  SetContext(in_context);

  FXL_NOTREACHED() << "SetContext should not return.";
}

void MakeContext(Context* context,
                 Stack* stack,
                 void (*func)(void*),
                 void* data) {
  memset(context, 0, sizeof(Context));

  uintptr_t sp = stack->safe_stack() + stack->stack_size();
  uintptr_t unsafe_sp = stack->unsafe_stack() + stack->stack_size();
  // Align stacks.
  sp = ((sp + kAdditionalStackAlignment) & (~15)) - kAdditionalStackAlignment;
  unsafe_sp = (unsafe_sp & (~15));

  context->registers[REG_LR] = reinterpret_cast<uintptr_t>(func);
  context->registers[REG_ARG0] = reinterpret_cast<uintptr_t>(data);
  context->registers[REG_SP] = sp;
  context->registers[REG_UNSAFE_SP] = unsafe_sp;
}

}  // namespace context
