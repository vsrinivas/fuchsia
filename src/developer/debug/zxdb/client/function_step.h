// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FUNCTION_STEP_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FUNCTION_STEP_H_

namespace zxdb {

class Thread;

enum class FunctionStep {
  // Do nothing special for this call. This will mean "stop" or "continue" depending on the
  // context.
  kDefault,

  // Single-step CPU instructions as long as there is no line information for the given address.
  // This is used to step through PLT stubs to get to the destination function, for example.
  kStepNoLineInfo,

  // Immediately step out of this function. This might be used to ignore libc calls, for example.
  kStepOut,
};

const char* FunctionStepToString(FunctionStep);

// Returns the action that should be applied to the function at the top of the stack for the given
// thread. The thread is expected to be stopped.
//
// This function should generally be called by the thread controllers whenever a new stack frame
// is entered or if ubsymbolized code is entered. It will base its computation on the current
// settings and state of the debugged program.
//
// If there is an error, it will return kDefault.
FunctionStep GetFunctionStepAction(Thread* thread);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FUNCTION_STEP_H_
