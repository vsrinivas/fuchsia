// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/system_impl.h"

#include <set>

#include "src/developer/debug/shared/logging/debug.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/breakpoint_impl.h"
#include "src/developer/debug/zxdb/client/job_context_impl.h"
#include "src/developer/debug/zxdb/client/process_impl.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/system_observer.h"
#include "src/developer/debug/zxdb/client/target_impl.h"
#include "src/developer/debug/zxdb/common/string_util.h"

namespace zxdb {

SystemImpl::SystemImpl(Session* session)
    : System(session), weak_factory_(this) {
  // Create the default job and target.
  AddNewJobContext(std::make_unique<JobContextImpl>(this, true));
  AddNewTarget(std::make_unique<TargetImpl>(this));

  settings_.set_name("system");

  // Forward all messages from the symbol index to our observers. It's OK to
  // bind |this| because the symbol index is owned by |this|.
  symbols_.build_id_index().set_information_callback(
      [this](const std::string& msg) {
        for (auto& observer : observers())
          observer.OnSymbolIndexingInformation(msg);
      });

  // The system is the one holding the system symbols and is the one who
  // will be updating the symbols once we get a symbol change, so the
  // System will be listening to its own options.
  // We don't use SystemSymbols because they live in the symbols library
  // and we don't want it to have a client dependency.
  settings_.AddObserver(ClientSettings::System::kDebugMode, this);
  settings_.AddObserver(ClientSettings::System::kSymbolCache, this);
  settings_.AddObserver(ClientSettings::System::kSymbolPaths, this);
  settings_.AddObserver(ClientSettings::System::kSymbolServers, this);
  settings_.AddObserver(ClientSettings::System::kQuitAgentOnExit, this);
}

SystemImpl::~SystemImpl() {
  // Target destruction may depend on the symbol system. Ensure the targets
  // get cleaned up first.
  for (auto& target : targets_) {
    // It's better if process destruction notifications are sent before target
    // ones because the target owns the process. Because this class sends the
    // target notifications, force the process destruction before doing
    // anything.
    target->ImplicitlyDetach();
    for (auto& observer : observers())
      observer.WillDestroyTarget(target.get());
  }
  targets_.clear();
}

ProcessImpl* SystemImpl::ProcessImplFromKoid(uint64_t koid) const {
  for (const auto& target : targets_) {
    ProcessImpl* process = target->process();
    if (process && process->GetKoid() == koid)
      return process;
  }
  return nullptr;
}

void SystemImpl::NotifyDidCreateProcess(Process* process) {
  for (auto& observer : observers())
    observer.GlobalDidCreateProcess(process);
}

void SystemImpl::NotifyWillDestroyProcess(Process* process) {
  for (auto& observer : observers())
    observer.GlobalWillDestroyProcess(process);
}

std::vector<TargetImpl*> SystemImpl::GetTargetImpls() const {
  std::vector<TargetImpl*> result;
  for (const auto& t : targets_)
    result.push_back(t.get());
  return result;
}

TargetImpl* SystemImpl::CreateNewTargetImpl(TargetImpl* clone) {
  auto target = clone ? clone->Clone(this) : std::make_unique<TargetImpl>(this);
  TargetImpl* to_return = target.get();
  AddNewTarget(std::move(target));
  return to_return;
}

SystemSymbols* SystemImpl::GetSymbols() { return &symbols_; }

std::vector<Target*> SystemImpl::GetTargets() const {
  std::vector<Target*> result;
  result.reserve(targets_.size());
  for (const auto& t : targets_)
    result.push_back(t.get());
  return result;
}

std::vector<JobContext*> SystemImpl::GetJobContexts() const {
  std::vector<JobContext*> result;
  result.reserve(job_contexts_.size());
  for (const auto& t : job_contexts_)
    result.push_back(t.get());
  return result;
}

std::vector<Breakpoint*> SystemImpl::GetBreakpoints() const {
  std::vector<Breakpoint*> result;
  result.reserve(breakpoints_.size());
  for (const auto& pair : breakpoints_) {
    if (!pair.second->is_internal())
      result.push_back(pair.second.get());
  }
  return result;
}

std::vector<SymbolServer*> SystemImpl::GetSymbolServers() const {
  std::vector<SymbolServer*> result;
  result.reserve(symbol_servers_.size());
  for (const auto& item : symbol_servers_) {
    result.push_back(item.get());
  }
  return result;
}

Process* SystemImpl::ProcessFromKoid(uint64_t koid) const {
  return ProcessImplFromKoid(koid);
}

void SystemImpl::GetProcessTree(ProcessTreeCallback callback) {
  session()->remote_api()->ProcessTree(debug_ipc::ProcessTreeRequest(),
                                       std::move(callback));
}

Target* SystemImpl::CreateNewTarget(Target* clone) {
  return CreateNewTargetImpl(static_cast<TargetImpl*>(clone));
}

JobContext* SystemImpl::CreateNewJobContext(JobContext* clone) {
  auto job_context = clone ? static_cast<JobContextImpl*>(clone)->Clone(this)
                           : std::make_unique<JobContextImpl>(this, false);
  JobContext* to_return = job_context.get();
  AddNewJobContext(std::move(job_context));
  return to_return;
}

Breakpoint* SystemImpl::CreateNewBreakpoint() {
  auto owning = std::make_unique<BreakpointImpl>(session(), false);
  uint32_t id = owning->backend_id();
  Breakpoint* to_return = owning.get();

  breakpoints_[id] = std::move(owning);

  // Notify observers (may mutate breakpoint list).
  for (auto& observer : observers())
    observer.DidCreateBreakpoint(to_return);

  return to_return;
}

Breakpoint* SystemImpl::CreateNewInternalBreakpoint() {
  auto owning = std::make_unique<BreakpointImpl>(session(), true);
  uint32_t id = owning->backend_id();
  Breakpoint* to_return = owning.get();

  breakpoints_[id] = std::move(owning);
  return to_return;
}

void SystemImpl::DeleteBreakpoint(Breakpoint* breakpoint) {
  BreakpointImpl* impl = static_cast<BreakpointImpl*>(breakpoint);
  auto found = breakpoints_.find(impl->backend_id());
  if (found == breakpoints_.end()) {
    // Should always have found the breakpoint.
    FXL_NOTREACHED();
    return;
  }

  // Only notify observers for non-internal breakpoints.
  if (!found->second->is_internal()) {
    for (auto& observer : observers())
      observer.WillDestroyBreakpoint(breakpoint);
  }
  breakpoints_.erase(found);
}

void SystemImpl::Pause() {
  debug_ipc::PauseRequest request;
  request.process_koid = 0;  // 0 means all processes.
  request.thread_koid = 0;   // 0 means all threads.
  session()->remote_api()->Pause(
      request, std::function<void(const Err&, debug_ipc::PauseReply)>());
}

void SystemImpl::Continue() {
  debug_ipc::ResumeRequest request;
  request.process_koid = 0;  // 0 means all processes.
  request.how = debug_ipc::ResumeRequest::How::kContinue;
  session()->remote_api()->Resume(
      request, std::function<void(const Err&, debug_ipc::ResumeReply)>());
}

void SystemImpl::DidConnect() {
  // Force reload the symbol mappings after connection. This needs to be done
  // for every connection since a new image could have been compiled and
  // launched which will have a different build ID file.
  symbols_.build_id_index().ClearCache();

  // Implicitly attach a job to the component root. If there was already an
  // implicit job created (from a previous connection) re-use it since there
  // will be settings on it about what processes to attach to that we want to
  // preserve.
  JobContextImpl* implicit_job = nullptr;
  for (auto& job : job_contexts_) {
    if (job->is_implicit_component_root()) {
      implicit_job = job.get();
      break;
    }
  }

  if (!implicit_job) {
    // No previous one, create a new implicit job.
    auto new_job = std::make_unique<JobContextImpl>(this, true);
    implicit_job = new_job.get();
    AddNewJobContext(std::move(new_job));
  }
  implicit_job->AttachToComponentRoot(
      [](fxl::WeakPtr<JobContext>, const Err&) {});
}

void SystemImpl::DidDisconnect() {
  for (auto& target : targets_)
    target->ImplicitlyDetach();
  for (auto& job : job_contexts_)
    job->ImplicitlyDetach();
}

BreakpointImpl* SystemImpl::BreakpointImplForId(uint32_t id) {
  auto found = breakpoints_.find(id);
  if (found == breakpoints_.end())
    return nullptr;
  return found->second.get();
}

void SystemImpl::AddNewTarget(std::unique_ptr<TargetImpl> target) {
  Target* for_observers = target.get();

  targets_.push_back(std::move(target));
  for (auto& observer : observers())
    observer.DidCreateTarget(for_observers);
}

void SystemImpl::AddNewJobContext(std::unique_ptr<JobContextImpl> job_context) {
  JobContext* for_observers = job_context.get();

  job_contexts_.push_back(std::move(job_context));
  for (auto& observer : observers())
    observer.DidCreateJobContext(for_observers);
}

void SystemImpl::OnSettingChanged(const SettingStore& store,
                                  const std::string& setting_name) {
  if (setting_name == ClientSettings::System::kSymbolPaths) {
    auto paths = store.GetList(ClientSettings::System::kSymbolPaths);
    BuildIDIndex& build_id_index = GetSymbols()->build_id_index();
    for (const std::string& path : paths) {
      if (StringEndsWith(path, ".txt")) {
        build_id_index.AddBuildIDMappingFile(path);
      } else {
        build_id_index.AddSymbolSource(path);
      }
    }
  } else if (setting_name == ClientSettings::System::kSymbolCache) {
    auto path = store.GetString(setting_name);

    if (!path.empty()) {
      GetSymbols()->build_id_index().AddSymbolSource(path);
    }
  } else if (setting_name == ClientSettings::System::kSymbolServers) {
    auto urls = store.GetList(setting_name);
    std::set<std::string> existing;

    for (const auto& symbol_server : symbol_servers_) {
      existing.insert(symbol_server->name());
    }

    for (const auto& url : urls) {
      if (existing.find(url) == existing.end()) {
        symbol_servers_.push_back(SymbolServer::FromURL(session(), url));

        for (auto& observer : observers()) {
          observer.DidCreateSymbolServer(symbol_servers_.back().get());
        }
      }
    }
  } else if (setting_name == ClientSettings::System::kDebugMode) {
    debug_ipc::SetDebugMode(store.GetBool(setting_name));
  } else {
    FXL_LOG(WARNING) << "Unhandled setting change: " << setting_name;
  }
}

}  // namespace zxdb
