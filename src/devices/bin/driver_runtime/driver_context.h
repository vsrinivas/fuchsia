// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_RUNTIME_DRIVER_CONTEXT_H_
#define SRC_DEVICES_BIN_DRIVER_RUNTIME_DRIVER_CONTEXT_H_

#include <lib/fdf/internal.h>

namespace driver_context {

// Adds |driver| to the thread's current call stack.
void PushDriver(const void* driver);

// Removes the driver at the top of the thread's current call stack.
// The stack must not be empty.
void PopDriver();

// Returns the driver at the top of the thread's current call stack,
// or null if the stack is empty.
const void* GetCurrentDriver();

// Returns whether |driver| is in the thread's current call stack.
bool IsDriverInCallStack(const void* driver);

// Returns whether the thread's current call stack is empty.
bool IsCallStackEmpty();

}  // namespace driver_context

#endif  // SRC_DEVICES_BIN_DRIVER_RUNTIME_DRIVER_CONTEXT_H_
