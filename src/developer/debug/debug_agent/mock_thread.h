// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_THREAD_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_THREAD_H_

#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/mock_thread_handle.h"

namespace debug_agent {

// Simple setup for a DebuggedThread that sets up a mocked thread handle and provides some
// convenience wrappers for querying the state.
//
// Since DebuggedThread is not a abstract class designed for derivation, there should be no
// overrides on this class. Overrides for behavior should go on the [Mock]ThreadHandle later.
class MockThread : public DebuggedThread {
 public:
  MockThread(DebuggedProcess* process, zx_koid_t thread_koid);
  ~MockThread();

  MockThreadHandle& mock_thread_handle() {
    // We create the thread handle in our constructor so we can assume its underlying type here.
    return static_cast<MockThreadHandle&>(thread_handle());
  }

  bool running() { return !mock_thread_handle().is_suspended() && !in_exception(); }

  // Sets the thread to be in an exception state with the current IP being at the given address.
  // All other registers will have their default (0) value.
  void SendException(uint64_t address, debug_ipc::ExceptionType type);
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_THREAD_H_
