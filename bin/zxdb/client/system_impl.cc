// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/system_impl.h"

#include "garnet/bin/zxdb/client/process_impl.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/system_observer.h"
#include "garnet/bin/zxdb/client/target_impl.h"

namespace zxdb {

SystemImpl::SystemImpl(Session* session) : System(session) {
  AddNewTarget(std::make_unique<TargetImpl>(this));
}

SystemImpl::~SystemImpl() = default;

ProcessImpl* SystemImpl::ProcessImplFromKoid(uint64_t koid) const {
  for (const auto& target : targets_) {
    ProcessImpl* process = target->process();
    if (process && process->GetKoid() == koid)
      return process;
  }
  return nullptr;
}

std::vector<Target*> SystemImpl::GetAllTargets() const {
  std::vector<Target*> result;
  result.reserve(targets_.size());
  for (const auto& t : targets_)
    result.push_back(t.get());
  return result;
}

Process* SystemImpl::ProcessFromKoid(uint64_t koid) const {
  return ProcessImplFromKoid(koid);
}

void SystemImpl::GetProcessTree(ProcessTreeCallback callback) {
  session()->Send<debug_ipc::ProcessTreeRequest, debug_ipc::ProcessTreeReply>(
      debug_ipc::ProcessTreeRequest(), std::move(callback));
}

Target* SystemImpl::CreateNewTarget(Target* clone) {
  auto target = clone ? static_cast<TargetImpl*>(clone)->Clone(this)
                      : std::make_unique<TargetImpl>(this);
  Target* to_return = target.get();
  AddNewTarget(std::move(target));
  return to_return;
}

void SystemImpl::Continue() {
  debug_ipc::ContinueRequest request;
  request.process_koid = 0;  // 0 means all processes.
  request.thread_koid = 0;   // 0 means all threads.
  session()->Send<debug_ipc::ContinueRequest, debug_ipc::ContinueReply>(
      request,
      std::function<void(const Err&, debug_ipc::ContinueReply)>());
}

void SystemImpl::AddNewTarget(std::unique_ptr<TargetImpl> target) {
  Target* for_observers = target.get();

  targets_.push_back(std::move(target));
  for (auto& observer : observers())
    observer.DidCreateTarget(for_observers);
}

}  // namespace zxdb
