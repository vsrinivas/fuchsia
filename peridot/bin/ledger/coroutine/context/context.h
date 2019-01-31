// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_COROUTINE_CONTEXT_CONTEXT_H_
#define PERIDOT_BIN_LEDGER_COROUTINE_CONTEXT_CONTEXT_H_

#include <memory>

#include <lib/fxl/build_config.h>

#include "peridot/bin/ledger/coroutine/context/stack.h"

#if defined(ARCH_CPU_X86_64)
#include "peridot/bin/ledger/coroutine/context/x64/context.h"
#elif defined(ARCH_CPU_ARM64)
#include "peridot/bin/ledger/coroutine/context/arm64/context.h"
#else
#error Please add support for your architecture.
#endif

namespace context {

// Context is architecture dependent.
using Context = InternalContext;

// Initializes |context| to the currently active execution context. Return
// |true| on the first return of this function. If this context is later resumed
// using SetContext or SwapContext, the execution will start with this function
// returning false.
extern bool GetContext(Context* context);

// Restores the execution context pointed at by |context|. This function never
// returns. The program execution will continue as if the call to |GetContext|,
// |MakeContext| or |SwapContext| that created |context| just returned.
extern void SetContext(Context* context);

// Initializes |context| to a new context. When this context is later activated,
// |func| is called with |data| as parameter. The stack will be |stack|. |func|
// must never return.
//
// NOLINT suppresses false-positive redundant declaration check due to a
// friend declaration in stack.h.
void MakeContext(Context* context,  // NOLINT
                 Stack* stack, void (*func)(void*), void* data);

// Saves the current execution context in |old_context| and activates the
// execution context pointed by |in_context|.
void SwapContext(Context* out_context, Context* in_context);

}  // namespace context

#endif  // PERIDOT_BIN_LEDGER_COROUTINE_CONTEXT_CONTEXT_H_
