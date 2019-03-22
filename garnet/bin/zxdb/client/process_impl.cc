// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/process_impl.h"

#include <inttypes.h>

#include <set>

#include "garnet/bin/zxdb/client/memory_dump.h"
#include "garnet/bin/zxdb/client/remote_api.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/target_impl.h"
#include "garnet/bin/zxdb/client/thread_impl.h"
#include "garnet/bin/zxdb/symbols/input_location.h"
#include "garnet/bin/zxdb/symbols/loaded_module_symbols.h"
#include "garnet/public/lib/fxl/logging.h"
#include "src/developer/debug/ipc/debug/block_timer.h"

namespace zxdb {

ProcessImpl::ProcessImpl(TargetImpl* target, uint64_t koid,
                         const std::string& name, Process::StartType start_type)
    : Process(target->session(), start_type),
      target_(target),
      koid_(koid),
      name_(name),
      symbols_(this, target->symbols()),
      weak_factory_(this) {}

ProcessImpl::~ProcessImpl() {
  // Send notifications for all destroyed threads.
  for (const auto& thread : threads_) {
    for (auto& observer : observers())
      observer.WillDestroyThread(this, thread.second.get());
  }
}

ThreadImpl* ProcessImpl::GetThreadImplFromKoid(uint64_t koid) {
  auto found = threads_.find(koid);
  if (found == threads_.end())
    return nullptr;
  return found->second.get();
}

Target* ProcessImpl::GetTarget() const { return target_; }

uint64_t ProcessImpl::GetKoid() const { return koid_; }

const std::string& ProcessImpl::GetName() const { return name_; }

ProcessSymbols* ProcessImpl::GetSymbols() { return &symbols_; }

void ProcessImpl::GetModules(
    std::function<void(const Err&, std::vector<debug_ipc::Module>)> callback) {
  debug_ipc::ModulesRequest request;
  request.process_koid = koid_;
  session()->remote_api()->Modules(
      request, [process = weak_factory_.GetWeakPtr(), callback](
                   const Err& err, debug_ipc::ModulesReply reply) {
        if (process)
          process->symbols_.SetModules(reply.modules);
        if (callback)
          callback(err, std::move(reply.modules));
      });
}

void ProcessImpl::GetAspace(
    uint64_t address,
    std::function<void(const Err&, std::vector<debug_ipc::AddressRegion>)>
        callback) const {
  debug_ipc::AddressSpaceRequest request;
  request.process_koid = koid_;
  request.address = address;
  session()->remote_api()->AddressSpace(
      request, [callback](const Err& err, debug_ipc::AddressSpaceReply reply) {
        if (callback)
          callback(err, std::move(reply.map));
      });
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
  session()->remote_api()->Threads(
      request, [callback, process = weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::ThreadsReply reply) {
        if (process) {
          process->UpdateThreads(reply.threads);
          if (callback)
            callback();
        }
      });
}

void ProcessImpl::Pause() {
  debug_ipc::PauseRequest request;
  request.process_koid = koid_;
  session()->remote_api()->Pause(request,
                                 [](const Err& err, debug_ipc::PauseReply) {});
}

void ProcessImpl::Continue() {
  debug_ipc::ResumeRequest request;
  request.process_koid = koid_;
  request.how = debug_ipc::ResumeRequest::How::kContinue;
  session()->remote_api()->Resume(
      request, [](const Err& err, debug_ipc::ResumeReply) {});
}

void ProcessImpl::ContinueUntil(const InputLocation& location,
                                std::function<void(const Err&)> cb) {
  cb(
      Err("Process-wide 'Until' is temporarily closed for construction. "
          "Please try again in a few days."));
}

void ProcessImpl::ReadMemory(
    uint64_t address, uint32_t size,
    std::function<void(const Err&, MemoryDump)> callback) {
  debug_ipc::ReadMemoryRequest request;
  request.process_koid = koid_;
  request.address = address;
  request.size = size;
  session()->remote_api()->ReadMemory(
      request, [callback](const Err& err, debug_ipc::ReadMemoryReply reply) {
        callback(err, MemoryDump(std::move(reply.blocks)));
      });
}

void ProcessImpl::WriteMemory(uint64_t address, std::vector<uint8_t> data,
                              std::function<void(const Err&)> callback) {
  debug_ipc::WriteMemoryRequest request;
  request.process_koid = koid_;
  request.address = address;
  request.data = std::move(data);
  session()->remote_api()->WriteMemory(
      request,
      [address, callback](const Err& err, debug_ipc::WriteMemoryReply reply) {
        if (err.has_error()) {
          callback(err);
        } else if (reply.status != 0) {
          // Convert bad reply to error.
          callback(Err("Unable to write memory to 0x%" PRIx64 ", error %d.",
                       address, reply.status));
        } else {
          // Success.
          callback(Err());
        }
      });
}

void ProcessImpl::OnThreadStarting(const debug_ipc::ThreadRecord& record) {
  TIME_BLOCK();
  if (threads_.find(record.koid) != threads_.end()) {
    // Duplicate new thread notification. Some legitimate cases could cause
    // this, like the client requesting a thread list (which will add missing
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
  TIME_BLOCK();
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

void ProcessImpl::OnModules(const std::vector<debug_ipc::Module>& modules,
                            const std::vector<uint64_t>& stopped_thread_koids) {
  TIME_BLOCK();
  symbols_.SetModules(modules);

  // The threads loading the library will be stopped so we have time to load
  // symbols and enable any pending breakpoints. Now that the notification is
  // complete, the thread(s) can continue.
  if (!stopped_thread_koids.empty()) {
    debug_ipc::ResumeRequest request;
    request.process_koid = koid_;
    request.how = debug_ipc::ResumeRequest::How::kContinue;
    request.thread_koids = stopped_thread_koids;
    session()->remote_api()->Resume(
        request, [](const Err& err, debug_ipc::ResumeReply) {});
  }
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
      // Existing one, update everything. Thread list updates don't include
      // full stack frames for performance reasons.
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

void ProcessImpl::DidLoadModuleSymbols(LoadedModuleSymbols* module) {
  for (auto& observer : observers())
    observer.DidLoadModuleSymbols(this, module);
}

void ProcessImpl::WillUnloadModuleSymbols(LoadedModuleSymbols* module) {
  for (auto& observer : observers())
    observer.WillUnloadModuleSymbols(this, module);
}

void ProcessImpl::OnSymbolLoadFailure(const Err& err) {
  TIME_BLOCK();
  for (auto& observer : observers())
    observer.OnSymbolLoadFailure(this, err);
}

}  // namespace zxdb
