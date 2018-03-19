// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/debugged_process.h"

#include <utility>
#include <zircon/syscalls/exception.h>

#include "garnet/bin/debug_agent/object_util.h"

struct DebuggedProcess::SuspendedThread {
  enum class Reason {
    kException,
    kOther  // Anything but exception.
  };

  zx::thread thread;

  // This is the original reason for the thread suspend. This controls how the
  // thread will be resumed.
  Reason original_reason = Reason::kOther;
};

DebuggedProcess::DebuggedProcess(zx_koid_t koid, zx::process proc)
    : koid_(koid), process_(std::move(proc)) {}
DebuggedProcess::~DebuggedProcess() = default;

void DebuggedProcess::OnContinue(const debug_ipc::ContinueRequest& request) {
  if (request.thread_koid) {
    ContinueThread(request.thread_koid);
  } else {
    // 0 thread ID means resume all in process. Note: ContinueThread will
    // mutate the list, so we need to make a copy.
    std::vector<zx_koid_t> koids;
    for (const auto& pair : suspended_threads_)
      koids.push_back(pair.first);
    for (zx_koid_t koid : koids)
      ContinueThread(koid);
  }
}

void DebuggedProcess::OnReadMemory(const debug_ipc::ReadMemoryRequest& request,
                                   debug_ipc::ReadMemoryReply* reply) {
  // TODO(brettw) break into blocks if a portion of the memory range is mapped
  // but a portion isn't. Currently this assumes the entire range is in one
  // block.
  debug_ipc::MemoryBlock block;
  block.address = request.address;
  block.size = request.size;
  block.data.resize(request.size);

  size_t bytes_read = 0;
  if (process_.read_memory(request.address, &block.data[0], block.size,
                           &bytes_read) == ZX_OK && bytes_read == block.size) {
    block.valid = true;
  } else {
    block.valid = false;
    block.data.resize(0);
  }

  reply->blocks.emplace_back(std::move(block));
}

void DebuggedProcess::OnException(const zx::thread& thread) {
  // TODO(brettw) add a policy for whether all threads are suspended during
  // execution, or just the current one.
  zx_koid_t exception_thread_koid = KoidForObject(thread);

  // Suspend all threads.
  for (auto& child_thread : GetChildThreads(process_.get())) {
    zx_koid_t thread_koid = KoidForObject(child_thread);
    auto found_child = suspended_threads_.find(thread_koid);
    if (found_child == suspended_threads_.end()) {
      found_child = suspended_threads_.insert(std::make_pair(
          thread_koid, SuspendedThread())).first;
      found_child->second.thread = std::move(child_thread);
    }

    if (thread_koid == exception_thread_koid) {
      // The excepting thread is already suspended.
      found_child->second.original_reason = SuspendedThread::Reason::kException;
    } else {
      found_child->second.thread.suspend();
    }
  }
}

void DebuggedProcess::ContinueThread(zx_koid_t thread_koid) {
  auto found = suspended_threads_.find(thread_koid);
  if (found == suspended_threads_.end()) {
    // It's possible to get here from a benign race condition. If the thread
    // has just been terminated and the client continues it before getting the
    // notification, we may see this.
    return;
  }

  SuspendedThread& thread = found->second;
  if (thread.original_reason == SuspendedThread::Reason::kException)
    thread.thread.resume(ZX_RESUME_EXCEPTION);
  else
    thread.thread.resume(0);

  suspended_threads_.erase(found);
}
