// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/process_impl.h"

#include <inttypes.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <set>

#include "lib/fit/defer.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/client/process_symbol_data_provider.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/target_impl.h"
#include "src/developer/debug/zxdb/client/thread_impl.h"
#include "src/developer/debug/zxdb/symbols/elf_symbol.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/module_symbol_status.h"

namespace zxdb {

ProcessImpl::ProcessImpl(TargetImpl* target, uint64_t koid, const std::string& name,
                         Process::StartType start_type)
    : Process(target->session(), start_type),
      target_(target),
      koid_(koid),
      name_(name),
      symbols_(this, target->symbols()),
      weak_factory_(this) {}

ProcessImpl::~ProcessImpl() {
  if (symbol_data_provider_)
    symbol_data_provider_->Disown();

  // Send notifications for all destroyed threads.
  for (const auto& thread : threads_) {
    for (auto& observer : session()->thread_observers())
      observer.WillDestroyThread(thread.second.get());
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
    fit::callback<void(const Err&, std::vector<debug_ipc::Module>)> callback) {
  debug_ipc::ModulesRequest request;
  request.process_koid = koid_;
  session()->remote_api()->Modules(
      request, [process = weak_factory_.GetWeakPtr(), callback = std::move(callback)](
                   const Err& err, debug_ipc::ModulesReply reply) mutable {
        if (process) {
          process->FixupEmptyModuleNames(reply.modules);
          process->symbols_.SetModules(reply.modules);
        }
        if (callback)
          callback(err, std::move(reply.modules));
      });
}

void ProcessImpl::GetAspace(
    uint64_t address,
    fit::callback<void(const Err&, std::vector<debug_ipc::AddressRegion>)> callback) const {
  debug_ipc::AddressSpaceRequest request;
  request.process_koid = koid_;
  request.address = address;
  session()->remote_api()->AddressSpace(
      request,
      [callback = std::move(callback)](const Err& err, debug_ipc::AddressSpaceReply reply) mutable {
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

Thread* ProcessImpl::GetThreadFromKoid(uint64_t koid) { return GetThreadImplFromKoid(koid); }

void ProcessImpl::SyncThreads(fit::callback<void()> callback) {
  debug_ipc::ThreadsRequest request;
  request.process_koid = koid_;
  session()->remote_api()->Threads(
      request, [callback = std::move(callback), process = weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::ThreadsReply reply) mutable {
        if (process) {
          process->UpdateThreads(reply.threads);
          if (callback)
            callback();
        }
      });
}

void ProcessImpl::Pause(fit::callback<void()> on_paused) {
  debug_ipc::PauseRequest request;
  request.process_koid = koid_;
  session()->remote_api()->Pause(
      request, [weak_process = weak_factory_.GetWeakPtr(), on_paused = std::move(on_paused)](
                   const Err& err, debug_ipc::PauseReply reply) mutable {
        if (weak_process) {
          // Save any new thread metadata (will be empty for errors so don't need to check
          // explicitly for errors).
          for (const auto& record : reply.threads) {
            FX_DCHECK(record.process_koid == weak_process->koid_);
            if (ThreadImpl* thread = weak_process->GetThreadImplFromKoid(record.thread_koid))
              thread->SetMetadata(record);
          }
        }
        on_paused();
      });
}

void ProcessImpl::Continue(bool forward_exceptions) {
  // Tell each thread to continue as it desires.
  //
  // It would be more efficient to tell the backend to resume all threads in the process but the
  // Thread client objects have state which needs to be updated (like the current stack) and the
  // thread could have a controller that wants to continue in a specific way (like single-step or
  // step in a range).
  for (const auto& [koid, thread] : threads_)
    thread->Continue(forward_exceptions);
}

void ProcessImpl::ContinueUntil(std::vector<InputLocation> locations,
                                fit::callback<void(const Err&)> cb) {
  cb(
      Err("Process-wide 'Until' is temporarily closed for construction. "
          "Please try again in a few days."));
}

fxl::RefPtr<SymbolDataProvider> ProcessImpl::GetSymbolDataProvider() const {
  if (!symbol_data_provider_) {
    symbol_data_provider_ =
        fxl::MakeRefCounted<ProcessSymbolDataProvider>(const_cast<ProcessImpl*>(this));
  }
  return symbol_data_provider_;
}

void ProcessImpl::GetTLSHelpers(GetTLSHelpersCallback cb) {
  DoWithHelpers([cb = std::move(cb), this, weak_this = GetWeakPtr()](bool have_helpers) mutable {
    if (!weak_this) {
      cb(Err("Process died while getting TLS helper."));
    } else if (have_helpers) {
      cb(&tls_helpers_);
    } else {
      cb(Err("Could not find Zxdb support code."));
    }
  });
}

void ProcessImpl::ReadMemory(uint64_t address, uint32_t size,
                             fit::callback<void(const Err&, MemoryDump)> callback) {
  debug_ipc::ReadMemoryRequest request;
  request.process_koid = koid_;
  request.address = address;
  request.size = size;
  session()->remote_api()->ReadMemory(
      request,
      [callback = std::move(callback)](const Err& err, debug_ipc::ReadMemoryReply reply) mutable {
        callback(err, MemoryDump(std::move(reply.blocks)));
      });
}

void ProcessImpl::WriteMemory(uint64_t address, std::vector<uint8_t> data,
                              fit::callback<void(const Err&)> callback) {
  debug_ipc::WriteMemoryRequest request;
  request.process_koid = koid_;
  request.address = address;
  request.data = std::move(data);
  session()->remote_api()->WriteMemory(request, [address, callback = std::move(callback)](
                                                    const Err& err,
                                                    debug_ipc::WriteMemoryReply reply) mutable {
    if (err.has_error()) {
      callback(err);
    } else if (reply.status != 0) {
      // Convert bad reply to error.
      callback(Err("Unable to write memory to 0x%" PRIx64 ", error %d.", address, reply.status));
    } else {
      // Success.
      callback(Err());
    }
  });
}

void ProcessImpl::LoadInfoHandleTable(
    fit::callback<void(ErrOr<std::vector<debug_ipc::InfoHandleExtended>> handles)> callback) {
  debug_ipc::LoadInfoHandleTableRequest request;
  request.process_koid = koid_;
  session()->remote_api()->LoadInfoHandleTable(
      request, [callback = std::move(callback)](const Err& err,
                                                debug_ipc::LoadInfoHandleTableReply reply) mutable {
        if (reply.status != 0) {
          callback(Err("Can't load handles error %d.", reply.status));
        } else if (err.ok()) {
          callback(std::move(reply.handles));
        } else {
          callback(err);
        }
      });
}

void ProcessImpl::OnThreadStarting(const debug_ipc::ThreadRecord& record, bool resume) {
  if (threads_.find(record.thread_koid) != threads_.end()) {
    // Duplicate new thread notification. Some legitimate cases could cause
    // this, like the client requesting a thread list (which will add missing
    // ones and get here) racing with the notification for just-created thread.
    return;
  }

  auto thread = std::make_unique<ThreadImpl>(this, record);
  Thread* thread_ptr = thread.get();
  threads_[record.thread_koid] = std::move(thread);

  for (auto& observer : session()->thread_observers())
    observer.DidCreateThread(thread_ptr);

  if (resume)
    thread_ptr->Continue(false);
}

void ProcessImpl::OnThreadExiting(const debug_ipc::ThreadRecord& record) {
  auto found = threads_.find(record.thread_koid);
  if (found == threads_.end()) {
    // Duplicate exit thread notification. Some legitimate cases could cause this as in
    // OnThreadStarting().
    return;
  }

  for (auto& observer : session()->thread_observers())
    observer.WillDestroyThread(found->second.get());

  threads_.erase(found);
}

void ProcessImpl::OnModules(std::vector<debug_ipc::Module> modules,
                            const std::vector<uint64_t>& stopped_thread_koids) {
  FixupEmptyModuleNames(modules);
  symbols_.SetModules(modules);

  // If this is the first thread, we see if we need to restart.
  if (start_type() == StartType::kLaunch || start_type() == StartType::kComponent) {
    bool pause_on_launch =
        session()->system().settings().GetBool(ClientSettings::System::kPauseOnLaunch);
    if (stopped_thread_koids.size() == 1u && pause_on_launch) {
      return;
    }
  }

  // The threads loading the library will be stopped so we have time to load symbols and enable any
  // pending breakpoints. Now that the notification is complete, the thread(s) can continue.
  //
  // Note that this is a "blind" resume, as the |this| does not yet know about any threads that are
  // currently running. It will issue a sync call shortly.
  if (!stopped_thread_koids.empty()) {
    debug_ipc::ResumeRequest request;
    request.process_koid = koid_;
    request.how = debug_ipc::ResumeRequest::How::kResolveAndContinue;
    request.thread_koids = stopped_thread_koids;
    session()->remote_api()->Resume(request, [](const Err& err, debug_ipc::ResumeReply) {});
  }

  // We get the list of threads for the process we are attaching.
  SyncThreads({});
}

bool ProcessImpl::HandleIO(const debug_ipc::NotifyIO& io) {
  auto& buffer = io.type == debug_ipc::NotifyIO::Type::kStdout ? stdout_ : stderr_;

  buffer.insert(buffer.end(), io.data.data(), io.data.data() + io.data.size());
  if (buffer.size() >= kMaxIOBufferSize)
    buffer.resize(kMaxIOBufferSize);

  return target()->settings().GetBool(ClientSettings::System::kShowStdout);
}

void ProcessImpl::UpdateThreads(const std::vector<debug_ipc::ThreadRecord>& new_threads) {
  // Go through all new threads, checking to added ones and updating existing.
  std::set<uint64_t> new_threads_koids;
  for (const auto& record : new_threads) {
    new_threads_koids.insert(record.thread_koid);
    auto found_existing = threads_.find(record.thread_koid);
    if (found_existing == threads_.end()) {
      // New thread added.
      OnThreadStarting(record, false);
    } else {
      // Existing one, update everything. Thread list updates don't include full stack frames for
      // performance reasons.
      found_existing->second->SetMetadata(record);
    }
  }

  // Do the reverse lookup to check for threads not in the new list. Be careful not to mutate the
  // threads_ list while iterating over it.
  std::vector<uint64_t> existing_koids;
  for (const auto& pair : threads_)
    existing_koids.push_back(pair.first);
  for (uint64_t existing_koid : existing_koids) {
    if (new_threads_koids.find(existing_koid) == new_threads_koids.end()) {
      debug_ipc::ThreadRecord record;
      record.thread_koid = existing_koid;
      OnThreadExiting(record);
    }
  }
}

void ProcessImpl::DidLoadModuleSymbols(LoadedModuleSymbols* module) {
  for (auto& observer : session()->process_observers())
    observer.DidLoadModuleSymbols(this, module);
}

void ProcessImpl::WillUnloadModuleSymbols(LoadedModuleSymbols* module) {
  for (auto& observer : session()->process_observers())
    observer.WillUnloadModuleSymbols(this, module);
}

uint64_t ProcessImpl::GetSymbolAddress(const std::string& symbol, uint64_t* size) {
  InputLocation location((Identifier(symbol)));
  auto locs = symbols_.ResolveInputLocation(location);

  for (const auto& loc : locs) {
    if (auto sym = loc.symbol().Get()) {
      if (auto elf_sym = sym->AsElfSymbol()) {
        *size = elf_sym->size();
        return loc.address();
      }
    }
  }

  return 0;
}

void ProcessImpl::DoWithHelpers(fit::callback<void(bool)> cb) {
  if (tls_helper_state_ == kFailed) {
    cb(false);
  } else if (tls_helper_state_ == kLoaded) {
    cb(true);
  } else {
    helper_waiters_.push_back(std::move(cb));
    LoadTLSHelpers();
  }
}

void ProcessImpl::LoadTLSHelpers() {
  if (tls_helper_state_ != kUnloaded) {
    return;
  }

  tls_helper_state_ = kLoading;

  struct HelperToLoad {
    uint64_t addr;
    uint64_t size;
    std::vector<uint8_t>* target;
  };

  std::vector<HelperToLoad> regions;
  regions.resize(3);

  regions[0].addr = GetSymbolAddress("zxdb.thrd_t", &regions[0].size);
  regions[0].target = &tls_helpers_.thrd_t;
  regions[1].addr = GetSymbolAddress("zxdb.link_map_tls_modid", &regions[1].size);
  regions[1].target = &tls_helpers_.link_map_tls_modid;
  regions[2].addr = GetSymbolAddress("zxdb.tlsbase", &regions[2].size);
  regions[2].target = &tls_helpers_.tlsbase;

  for (const auto& region : regions) {
    if (region.addr && region.size) {
      continue;
    }

    tls_helper_state_ = kFailed;

    for (auto& cb : helper_waiters_) {
      cb(false);
    }

    helper_waiters_.clear();

    return;
  }

  std::shared_ptr<fit::deferred_callback> finish = std::make_shared<fit::deferred_callback>(
      fit::defer_callback([this, weak_this = GetWeakPtr()]() {
        if (!weak_this) {
          return;
        }

        bool failed = tls_helpers_.thrd_t.empty() || tls_helpers_.link_map_tls_modid.empty() ||
                      tls_helpers_.tlsbase.empty();

        if (failed) {
          tls_helper_state_ = kFailed;
        } else {
          tls_helper_state_ = kLoaded;
        }

        for (auto& cb : helper_waiters_) {
          cb(!failed);
        }

        helper_waiters_.clear();
      }));

  for (const auto& region : regions) {
    ReadMemory(region.addr, region.size,
               [target = region.target, weak_this = GetWeakPtr(), finish](const Err& err,
                                                                          MemoryDump dump) {
                 if (weak_this && !err.has_error() && dump.AllValid()) {
                   for (const auto& block : dump.blocks()) {
                     std::copy(block.data.begin(), block.data.end(), std::back_inserter(*target));
                   }
                 }
               });
  }
}

void ProcessImpl::FixupEmptyModuleNames(std::vector<debug_ipc::Module>& modules) const {
  for (auto& m : modules) {
    if (m.name.empty())
      m.name = GetName();
  }
}

void ProcessImpl::OnSymbolLoadFailure(const Err& err) {
  for (auto& observer : session()->process_observers())
    observer.OnSymbolLoadFailure(this, err);
}

}  // namespace zxdb
