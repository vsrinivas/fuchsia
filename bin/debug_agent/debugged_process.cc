// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/debugged_process.h"

#include <inttypes.h>
#include <utility>
#include <zircon/syscalls/exception.h>

#include "garnet/bin/debug_agent/debugged_thread.h"
#include "garnet/bin/debug_agent/object_util.h"
#include "garnet/bin/debug_agent/debugged_thread.h"
#include "garnet/bin/debug_agent/process_breakpoint.h"
#include "garnet/public/lib/fxl/logging.h"

namespace debug_agent {

DebuggedProcess::DebuggedProcess(DebugAgent* debug_agent,
                                 zx_koid_t koid,
                                 zx::process proc)
    : debug_agent_(debug_agent), koid_(koid), process_(std::move(proc)) {}
DebuggedProcess::~DebuggedProcess() = default;

void DebuggedProcess::OnPause(const debug_ipc::PauseRequest& request) {
  if (request.thread_koid) {
    DebuggedThread* thread = GetThread(request.thread_koid);
    if (thread)
      thread->Pause();
    // Could be not found if there is a race between the thread exiting and
    // the client sending the request.
  } else {
    // 0 thread ID means resume all in process.
    for (const auto& pair : threads_)
      pair.second->Pause();
  }
}

void DebuggedProcess::OnResume(const debug_ipc::ResumeRequest& request) {
  if (request.thread_koid) {
    DebuggedThread* thread = GetThread(request.thread_koid);
    if (thread)
      thread->Resume(request.how);
    // Could be not found if there is a race between the thread exiting and
    // the client sending the request.
  } else {
    // 0 thread ID means resume all in process.
    for (const auto& pair : threads_)
      pair.second->Resume(request.how);
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
                           &bytes_read) == ZX_OK &&
      bytes_read == block.size) {
    block.valid = true;
  } else {
    block.valid = false;
    block.data.resize(0);
  }

  reply->blocks.emplace_back(std::move(block));
}

void DebuggedProcess::OnAddOrChangeBreakpoint(
    const debug_ipc::AddOrChangeBreakpointRequest& request,
    debug_ipc::AddOrChangeBreakpointReply* reply) {
  // Need to make sure there aren't two breakpoints at the same address that
  // will step on each other. This does not check for partial overlaps which
  // implies that the client set the breakpoint at something other than an
  // instruction boundary and is corrupt anyway.
  FXL_DCHECK(address_to_breakpoint_id_.size() == breakpoints_.size());
  const auto found_addr =
      address_to_breakpoint_id_.find(request.breakpoint.address);

  auto found_id = breakpoints_.find(request.breakpoint.breakpoint_id);
  if (found_id == breakpoints_.end()) {
    // New breakpoint. Shouldn't have any existing breakpoint at this address.
    if (found_addr != address_to_breakpoint_id_.end()) {
      reply->status = ZX_ERR_ALREADY_EXISTS;
      reply->error_message = "There is already a breakpoint at this address.";
      return;
    }
    found_id = breakpoints_
                   .emplace(request.breakpoint.breakpoint_id,
                            std::make_unique<ProcessBreakpoint>(this))
                   .first;
  } else {
    // Modifying an existing breakpoint. If there's an existing breakpoint
    // at this address, it should be the same one.
    if (found_addr != address_to_breakpoint_id_.end()) {
      if (found_addr->first != request.breakpoint.breakpoint_id) {
        reply->status = ZX_ERR_ALREADY_EXISTS;
        reply->error_message = "There is already a breakpoint at this address.";
        return;
      }

      if (request.breakpoint.address != found_id->second->address()) {
        // Existing breakpoint moving. Remove the old address mapping. The new
        // one will be created at the bottom.
        address_to_breakpoint_id_.erase(found_addr);
      }
    }
  }

  found_id->second->SetSettings(request.breakpoint);
  address_to_breakpoint_id_[request.breakpoint.address] =
      request.breakpoint.breakpoint_id;
}

void DebuggedProcess::OnRemoveBreakpoint(
    const debug_ipc::RemoveBreakpointRequest& request,
    debug_ipc::RemoveBreakpointReply* reply) {
  auto found_id = breakpoints_.find(request.breakpoint_id);
  if (found_id == breakpoints_.end()) {
    FXL_NOTREACHED();
    return;
  }

  address_to_breakpoint_id_.erase(found_id->second->address());
  breakpoints_.erase(found_id);
  FXL_DCHECK(address_to_breakpoint_id_.size() == breakpoints_.size());
}

void DebuggedProcess::OnKill(const debug_ipc::KillRequest& request,
                             debug_ipc::KillReply* reply) {
  reply->status = process_.kill();
}

DebuggedThread* DebuggedProcess::GetThread(zx_koid_t thread_koid) {
  auto found_thread = threads_.find(thread_koid);
  if (found_thread == threads_.end())
    return nullptr;
  return found_thread->second.get();
}

void DebuggedProcess::OnThreadStarting(zx::thread thread,
                                       zx_koid_t thread_koid) {
  FXL_DCHECK(threads_.find(thread_koid) == threads_.end());
  threads_.emplace(thread_koid,
                   std::make_unique<DebuggedThread>(this, std::move(thread),
                                                    thread_koid, true));
}

void DebuggedProcess::OnThreadExiting(zx_koid_t thread_koid) {
  FXL_DCHECK(threads_.find(thread_koid) != threads_.end());
  threads_.erase(thread_koid);
}

void DebuggedProcess::PopulateCurrentThreads() {
  for (zx_koid_t koid :
       GetChildKoids(process_.get(), ZX_INFO_PROCESS_THREADS)) {
    FXL_DCHECK(threads_.find(koid) == threads_.end());

    zx_handle_t handle;
    if (zx_object_get_child(process_.get(), koid, ZX_RIGHT_SAME_RIGHTS,
                            &handle) == ZX_OK) {
      threads_.emplace(koid, std::make_unique<DebuggedThread>(
                                 this, zx::thread(handle), koid, true));
    }
  }
}

void DebuggedProcess::OnException(zx_koid_t thread_koid, uint32_t type) {
  DebuggedThread* thread = GetThread(thread_koid);
  if (thread) {
    thread->OnException(type);
  } else {
    fprintf(stderr,
            "Exception for thread %" PRIu64 " which we don't know about.\n",
            thread_koid);
  }
}

ProcessBreakpoint* DebuggedProcess::FindBreakpointForAddr(uint64_t address) {
  FXL_DCHECK(breakpoints_.size() == address_to_breakpoint_id_.size());

  auto found = address_to_breakpoint_id_.find(address);
  if (found == address_to_breakpoint_id_.end())
    return nullptr;

  uint32_t id = found->second;
  auto found_id = breakpoints_.find(id);
  if (found_id == breakpoints_.end()) {
    FXL_NOTREACHED();
    return nullptr;
  }
  return found_id->second.get();
}

}  // namespace debug_agent
