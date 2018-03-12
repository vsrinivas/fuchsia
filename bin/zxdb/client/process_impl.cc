// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/process_impl.h"

#include "garnet/bin/zxdb/client/target_impl.h"
#include "garnet/bin/zxdb/client/thread_impl.h"
#include "garnet/public/lib/fxl/logging.h"

namespace zxdb {

ProcessImpl::ProcessImpl(TargetImpl* target, uint64_t koid)
    : Process(target->session()), target_(target), koid_(koid) {}
ProcessImpl::~ProcessImpl() = default;

Target* ProcessImpl::GetTarget() const {
  return target_;
}

uint64_t ProcessImpl::GetKoid() const {
  return koid_;
}

std::vector<Thread*> ProcessImpl::GetThreads() const {
  std::vector<Thread*> result;
  result.reserve(threads_.size());
  for (const auto& pair : threads_)
    result.push_back(pair.second.get());
  return result;
}

void ProcessImpl::OnThreadStarting(uint64_t thread_koid) {
  auto thread = std::make_unique<ThreadImpl>(this, thread_koid);
  Thread* thread_ptr = thread.get();
  threads_[thread_koid] = std::move(thread);

  for (auto& observer : observers())
    observer.DidCreateThread(this, thread_ptr);
}

void ProcessImpl::OnThreadExiting(uint64_t thread_koid) {
  auto found = threads_.find(thread_koid);
  if (found == threads_.end()) {
    FXL_NOTREACHED();
    return;
  }

  for (auto& observer : observers())
    observer.WillDestroyThread(this, found->second.get());

  threads_.erase(found);
}

}  // namespace zxdb
