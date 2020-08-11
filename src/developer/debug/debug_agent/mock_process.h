// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_PROCESS_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_PROCESS_H_

#include "src/developer/debug/debug_agent/debugged_process.h"
#include "src/developer/debug/debug_agent/mock_process_handle.h"

namespace debug_agent {

class MockThread;

// Meant to be used by tests for having light-weight processes that don't talk
// to zircon in order to spin up threads.
class MockProcess : public DebuggedProcess {
 public:
  // |debug_agent| is optional and can be null. Be sure that your test doesn't use those resouces
  // though.
  MockProcess(DebugAgent* debug_agent, zx_koid_t koid, std::string name = std::string());
  ~MockProcess();

  MockProcessHandle& mock_process_handle() {
    // We create the process handle in our constructor so we can assume its underlying type here.
    return static_cast<MockProcessHandle&>(process_handle());
  }

  MockThread* AddThread(zx_koid_t koid);
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_PROCESS_H_
