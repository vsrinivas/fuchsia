// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/system_impl.h"

#include "garnet/bin/zxdb/client/process_impl.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/target_impl.h"

namespace zxdb {

SystemImpl::SystemImpl(Session* session) : System(session) {
  targets_.push_back(std::make_unique<TargetImpl>(this));
}

SystemImpl::~SystemImpl() = default;

std::vector<Target*> SystemImpl::GetAllTargets() const {
  std::vector<Target*> result;
  result.reserve(targets_.size());
  for (const auto& t : targets_)
    result.push_back(t.get());
  return result;
}

Process* SystemImpl::ProcessFromKoid(uint64_t koid) const {
  for (const auto& target : targets_) {
    Process* process = target->process();
    if (process && process->GetKoid() == koid)
      return process;
  }
  return nullptr;
}

void SystemImpl::GetProcessTree(ProcessTreeCallback callback) {
  // Since this System object is owned by the Session calling us, we don't
  // have to worry about lifetime issues of "this".
  session()->Send<debug_ipc::ProcessTreeRequest, debug_ipc::ProcessTreeReply>(
      debug_ipc::ProcessTreeRequest(),
      [callback = std::move(callback), this](
          Session*, uint32_t, const Err& err,
          debug_ipc::ProcessTreeReply reply) {
        callback(err, std::move(reply));
      });
}

}  // namespace zxdb
