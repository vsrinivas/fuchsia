// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_PROCESS_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_PROCESS_H_

#include "src/developer/debug/debug_agent/debugged_process.h"

namespace debug_agent {

// Meant to be used by tests for having light-weight processes that don't talk
// to zircon in order to spin up threads.
class MockProcess : public DebuggedProcess {
 public:
  MockProcess(zx_koid_t koid);
  ~MockProcess();

  DebuggedThread* AddThread(zx_koid_t koid);

  DebuggedThread* GetThread(zx_koid_t koid) const override;

  std::vector<DebuggedThread*> GetThreads() const override;

 private:
  std::map<zx_koid_t, std::unique_ptr<DebuggedThread>> threads_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_PROCESS_H_
