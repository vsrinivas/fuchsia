// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/system_impl.h"

#include "garnet/bin/zxdb/client/breakpoint_impl.h"
#include "garnet/bin/zxdb/client/process_impl.h"
#include "garnet/bin/zxdb/client/remote_api.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/system_observer.h"
#include "garnet/bin/zxdb/client/target_impl.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"

namespace zxdb {

SystemImpl::SystemImpl(Session* session)
    : System(session), weak_factory_(this) {
  AddNewTarget(std::make_unique<TargetImpl>(this));

  // Load the build ID file but asynchronously notify. This allows the system
  // to be loaded enough so that the observers are initialized.
  std::string symbol_msg;
  bool ids_loaded = symbols_.LoadBuildIDFile(&symbol_msg);
  debug_ipc::MessageLoop::Current()->PostTask(
      [ weak_system = weak_factory_.GetWeakPtr(), ids_loaded, symbol_msg ]() {
        if (weak_system) {
          for (auto& observer : weak_system->observers())
            observer.DidTryToLoadSymbolMapping(ids_loaded, symbol_msg);
        }
      });
}

SystemImpl::~SystemImpl() {
  // Target destruction may depend on the symbol system. Ensure the targets
  // get cleaned up first.
  for (auto& target : targets_) {
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

SystemSymbols* SystemImpl::GetSymbols() { return &symbols_; }

std::vector<Target*> SystemImpl::GetTargets() const {
  std::vector<Target*> result;
  result.reserve(targets_.size());
  for (const auto& t : targets_)
    result.push_back(t.get());
  return result;
}

std::vector<Breakpoint*> SystemImpl::GetBreakpoints() const {
  std::vector<Breakpoint*> result;
  result.reserve(breakpoints_.size());
  for (const auto& t : breakpoints_)
    result.push_back(t.get());
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
  auto target = clone ? static_cast<TargetImpl*>(clone)->Clone(this)
                      : std::make_unique<TargetImpl>(this);
  Target* to_return = target.get();
  AddNewTarget(std::move(target));
  return to_return;
}

Breakpoint* SystemImpl::CreateNewBreakpoint() {
  breakpoints_.push_back(std::make_unique<BreakpointImpl>(session()));
  Breakpoint* to_return = breakpoints_.back().get();

  // Notify observers (may mutate breakpoint list).
  for (auto& observer : observers())
    observer.DidCreateBreakpoint(to_return);

  return to_return;
}

void SystemImpl::DeleteBreakpoint(Breakpoint* breakpoint) {
  for (size_t i = 0; i < breakpoints_.size(); i++) {
    if (breakpoints_[i].get() == breakpoint) {
      // Notify observers.
      for (auto& observer : observers())
        observer.WillDestroyBreakpoint(breakpoint);

      breakpoints_.erase(breakpoints_.begin() + i);
      return;
    }
  }
  FXL_NOTREACHED();
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
  request.thread_koid = 0;   // 0 means all threads.
  request.how = debug_ipc::ResumeRequest::How::kContinue;
  session()->remote_api()->Resume(
      request, std::function<void(const Err&, debug_ipc::ResumeReply)>());
}

void SystemImpl::AddNewTarget(std::unique_ptr<TargetImpl> target) {
  Target* for_observers = target.get();

  targets_.push_back(std::move(target));
  for (auto& observer : observers())
    observer.DidCreateTarget(for_observers);
}

}  // namespace zxdb
