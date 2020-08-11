// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_process.h"

#include "src/developer/debug/debug_agent/mock_thread.h"

namespace debug_agent {

MockProcess::MockProcess(DebugAgent* debug_agent, zx_koid_t koid, std::string name)
    : DebuggedProcess(debug_agent,
                      DebuggedProcessCreateInfo(std::make_unique<MockProcessHandle>(koid, name))) {}
MockProcess::~MockProcess() = default;

MockThread* MockProcess::AddThread(zx_koid_t thread_koid) {
  auto mock_thread = std::make_unique<MockThread>(this, thread_koid);
  MockThread* thread_ptr = mock_thread.get();
  InjectThreadForTest(std::move(mock_thread));
  return thread_ptr;
}

}  // namespace debug_agent
