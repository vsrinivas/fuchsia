// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_COROUTINE_CONTEXT_STACK_H_
#define APPS_LEDGER_SRC_COROUTINE_CONTEXT_STACK_H_

#include <stddef.h>

#include "mx/vmar.h"
#include "mx/vmo.h"

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

 private:
  friend void MakeContext(context::Context*, Stack*, void (*)(void*), void*);
  friend char* GetUnsafeStackForTest(const Stack&);

  uintptr_t safe_stack() const { return safe_stack_; }
  uintptr_t unsafe_stack() const { return unsafe_stack_; };

  const size_t stack_size_;
  mx::vmo vmo_;
  mx::vmar safe_stack_mapping_;
  uintptr_t safe_stack_;
  mx::vmar unsafe_stack_mapping_;
  uintptr_t unsafe_stack_;
};
}  // namespace context

#endif  // APPS_LEDGER_SRC_COROUTINE_CONTEXT_STACK_H_
