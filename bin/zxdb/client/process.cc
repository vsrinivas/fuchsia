// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/process.h"

#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/public/lib/fxl/logging.h"

namespace zxdb {

// statkc
std::map<int, Process::ThreadChangeCallback> Process::thread_change_callbacks_;
int Process::next_thread_change_callback_id_ = 1;

Process::Process(Target* target, uint64_t koid)
    : ClientObject(target->session()), target_(target), koid_(koid) {}

Process::~Process() = default;

void Process::OnThreadStarting(uint64_t thread_koid) {
  auto& thread_ptr = threads_[next_thread_id_];
  thread_ptr.reset(
      new Thread(this, next_thread_id_, thread_koid));
  next_thread_id_++;

  for (const auto& cb : thread_change_callbacks_)
    cb.second(thread_ptr.get(), ThreadChange::kStarted);
}

void Process::OnThreadExiting(uint64_t thread_koid) {
  for (auto it = threads_.begin(); it != threads_.end(); ++it) {
    if (it->second->koid() == thread_koid) {
      for (const auto& cb : thread_change_callbacks_)
        cb.second(it->second.get(), ThreadChange::kExiting);
      threads_.erase(it);
      break;
    }
  }
}

// static
int Process::StartWatchingGlobalThreadChanges(ThreadChangeCallback callback) {
  int id = next_thread_change_callback_id_;
  next_thread_change_callback_id_++;
  thread_change_callbacks_[id] = callback;
  return id;
}

// static
void Process::StopWatchingGlobalThreadChanges(int callback_id) {
  auto found = thread_change_callbacks_.find(callback_id);
  if (found == thread_change_callbacks_.end())
    FXL_NOTREACHED();
  else
    thread_change_callbacks_.erase(found);
}

}  // namespace zxdb
