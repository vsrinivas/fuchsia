// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_process.h"

#include "src/developer/debug/debug_agent/mock_thread.h"

namespace debug_agent {

MockProcess::MockProcess(zx_koid_t koid) : DebuggedProcess(nullptr, {koid, zx::process()}) {}

MockProcess::~MockProcess() = default;

DebuggedThread* MockProcess::AddThread(zx_koid_t thread_koid) {
  auto mock_thread = std::make_unique<MockThread>(this, thread_koid);
  DebuggedThread* thread_ptr = mock_thread.get();
  threads_[thread_koid] = std::move(mock_thread);
  return thread_ptr;
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
