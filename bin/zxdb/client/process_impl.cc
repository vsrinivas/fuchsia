// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/process_impl.h"

#include <set>

#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/target_impl.h"
#include "garnet/bin/zxdb/client/thread_impl.h"
#include "garnet/public/lib/fxl/logging.h"

namespace zxdb {

ProcessImpl::ProcessImpl(TargetImpl* target, uint64_t koid,
                         const std::string& name)
    : Process(target->session()), target_(target), koid_(koid), name_(name),
      weak_thunk_(std::make_shared<WeakThunk<ProcessImpl>>(this)) {}
ProcessImpl::~ProcessImpl() = default;

ThreadImpl* ProcessImpl::GetThreadImplFromKoid(uint64_t koid) {
  auto found = threads_.find(koid);
  if (found == threads_.end())
    return nullptr;
  return found->second.get();
}

Target* ProcessImpl::GetTarget() const {
  return target_;
}

uint64_t ProcessImpl::GetKoid() const {
  return koid_;
}

const std::string& ProcessImpl::GetName() const {
  return name_;
}

std::vector<Thread*> ProcessImpl::GetThreads() const {
  std::vector<Thread*> result;
  result.reserve(threads_.size());
  for (const auto& pair : threads_)
    result.push_back(pair.second.get());
  return result;
}

Thread* ProcessImpl::GetThreadFromKoid(uint64_t koid) {
  return GetThreadImplFromKoid(koid);
}

void ProcessImpl::SyncThreads(std::function<void()> callback) {
  debug_ipc::ThreadsRequest request;
  request.process_koid = koid_;
  session()->Send<debug_ipc::ThreadsRequest, debug_ipc::ThreadsReply>(
      request,
      [callback = std::move(callback),
       thunk = std::weak_ptr<WeakThunk<ProcessImpl>>(weak_thunk_)](
          Session*, uint32_t, const Err& err, debug_ipc::ThreadsReply reply) {
        if (auto ptr = thunk.lock()) {
          ptr->thunk->UpdateThreads(reply.threads);
          if (callback)
            callback();
        }
      });
}

void ProcessImpl::OnThreadStarting(const debug_ipc::ThreadRecord& record) {
  if (threads_.find(record.koid) != threads_.end()) {
    // Duplicate new thread notification. Some legitimate cases could cause
    // this, like the client requeusting a thread list (which will add missing
    // ones and get here) racing with the notification for just-created thread.
    return;
  }

  auto thread = std::make_unique<ThreadImpl>(this, record);
  Thread* thread_ptr = thread.get();
  threads_[record.koid] = std::move(thread);

  for (auto& observer : observers())
    observer.DidCreateThread(this, thread_ptr);
}

void ProcessImpl::OnThreadExiting(const debug_ipc::ThreadRecord& record) {
  auto found = threads_.find(record.koid);
  if (found == threads_.end()) {
    // Duplicate exit thread notification. Some legitimate cases could cause
    // this as in OnThreadStarting().
    return;
  }

  for (auto& observer : observers())
    observer.WillDestroyThread(this, found->second.get());

  threads_.erase(found);
}

void ProcessImpl::UpdateThreads(
    const std::vector<debug_ipc::ThreadRecord>& new_threads) {
  // Go through all new threads, checking to added ones and updating existing.
  std::set<uint64_t> new_threads_koids;
  for (const auto& record : new_threads) {
    new_threads_koids.insert(record.koid);
    auto found_existing = threads_.find(record.koid);
    if (found_existing == threads_.end()) {
      // New thread added.
      OnThreadStarting(record);
    } else {
      // Existing one, update everything.
      found_existing->second->SetMetadata(record);
    }
  }

  // Do the reverse lookup to check for threads not in the new list. Be careful
  // not to mutate the threads_ list while iterating over it.
  std::vector<uint64_t> existing_koids;
  for (const auto& pair : threads_)
    existing_koids.push_back(pair.first);
  for (uint64_t existing_koid : existing_koids) {
    if (new_threads_koids.find(existing_koid) == new_threads_koids.end()) {
      debug_ipc::ThreadRecord record;
      record.koid = existing_koid;
      OnThreadExiting(record);
    }
  }
}

}  // namespace zxdb
