// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_process.h"

#include "src/developer/debug/debug_agent/debugged_thread.h"

namespace debug_agent {

MockProcess::MockProcess(zx_koid_t koid)
    : DebuggedProcess(nullptr, {koid, zx::process()}) {}

MockProcess::~MockProcess() = default;

void MockProcess::AddThread(zx_koid_t koid) {
  threads_[koid] = std::make_unique<DebuggedThread>(
      this, zx::thread(), koid, ThreadCreationOption::kSuspendedKeepSuspended);
}

DebuggedThread* MockProcess::GetThread(zx_koid_t koid) const {
  auto it = threads_.find(koid);
  if (it == threads_.end())
    return nullptr;
  return it->second.get();
}

std::vector<DebuggedThread*> MockProcess::GetThreads() const {
  std::vector<DebuggedThread*> threads;
  threads.reserve(threads_.size());
  for (auto& kv : threads_)
    threads.emplace_back(kv.second.get());
  return threads;
}

}  // namespace debug_agent
