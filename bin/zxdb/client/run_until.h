// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

namespace zxdb {

class Err;
struct InputLocation;
class Process;
class Thread;

// Backend for Thread and Process' ContinueUntil(). As with this functions,
// the callback indicates that setup is complete, not that the step completed.
void RunUntil(Process* process, InputLocation location,
              std::function<void(const Err&)> cb);
void RunUntil(Thread* thread, InputLocation location,
              std::function<void(const Err&)> cb);

// Runs the given thread until both its location match and checks the stack.
// It will stop at the location only when the current stack frame is strictly
// greater than the input end_sp (this means the stack has grown up to a
// higher frame than was input).
void RunUntil(Thread* thread, InputLocation location, uint64_t end_sp,
              std::function<void(const Err&)> cb);

}  // namespace zxdb
