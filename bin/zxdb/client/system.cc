// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/system.h"

#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/public/lib/fxl/logging.h"

namespace zxdb {

System::System(Session* session) : ClientObject(session) {
  targets_[next_target_id_].reset(new Target(this, next_target_id_));
  next_target_id_++;
}

System::~System() = default;

void System::GetProcessTree(ProcessTreeCallback callback) {
  // Since this System object is owned by the Session calling us, we don't
  // have to worry about lifetime issues of "this".
  session()->Send<debug_ipc::ProcessTreeRequest, debug_ipc::ProcessTreeReply>(
      debug_ipc::ProcessTreeRequest(),
      [callback = std::move(callback), this](
          Session* session, uint32_t, const Err& err,
          debug_ipc::ProcessTreeReply reply) {
        callback(this, err, std::move(reply));
      });
}

Target* System::GetActiveTarget() const {
  auto found = targets_.find(active_target_id_);
  FXL_DCHECK(found != targets_.end());
  return found->second.get();
}

Process* System::ProcessFromKoid(uint64_t koid) {
  for (auto& pair : targets_) {
    if (pair.second->process() && pair.second->process()->koid() == koid)
      return pair.second->process();
  }
  return nullptr;
}

}  // namespace zxdb
