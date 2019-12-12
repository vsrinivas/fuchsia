// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_COROUTINE_CONTEXT_CONTEXT_H_
#define SRC_LEDGER_LIB_COROUTINE_CONTEXT_CONTEXT_H_

#include <memory>

#include "src/ledger/lib/coroutine/context/stack.h"

// Processor architecture detection.  For more info on what's defined, see:
//   http://msdn.microsoft.com/en-us/library/b0084kay.aspx
//   http://www.agner.org/optimize/calling_conventions.pdf
//   or with gcc, run: "echo | gcc -E -dM -"
// X64
#if defined(_M_X64) || defined(__x86_64__)
#include "src/ledger/lib/coroutine/context/x64/context.h"
// Arm64
#elif defined(__aarch64__)
#include "src/ledger/lib/coroutine/context/arm64/context.h"
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

#endif  // SRC_LEDGER_LIB_COROUTINE_CONTEXT_CONTEXT_H_
