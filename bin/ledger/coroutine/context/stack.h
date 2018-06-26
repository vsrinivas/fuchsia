// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_COROUTINE_CONTEXT_STACK_H_
#define PERIDOT_BIN_LEDGER_COROUTINE_CONTEXT_STACK_H_

#include <stddef.h>

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

namespace context {

struct InternalContext;
using Context = InternalContext;

// A stack to be used with |MakeContext|.
class Stack {
 public:
  // Creates a new stack. |stack_size| is the minimal size of the new stack.
  explicit Stack(size_t stack_size = 64 * 1024);
  ~Stack();

  // Releases the memory associated with this stack. After this call, the stack
  // is ready to be used again, but its content is not specified.
  void Release();

  size_t stack_size() const { return stack_size_; }

  uintptr_t safe_stack() const { return safe_stack_; }
#if __has_feature(safe_stack)
  uintptr_t unsafe_stack() const { return unsafe_stack_; };
#endif

 private:
  const size_t stack_size_;
  zx::vmo vmo_;
  zx::vmar safe_stack_mapping_;
  uintptr_t safe_stack_;
#if __has_feature(safe_stack)
  zx::vmar unsafe_stack_mapping_;
  uintptr_t unsafe_stack_;
#endif
};
}  // namespace context

#endif  // PERIDOT_BIN_LEDGER_COROUTINE_CONTEXT_STACK_H_
