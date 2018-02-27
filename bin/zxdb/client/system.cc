// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/system.h"

#include "garnet/bin/zxdb/client/session.h"

namespace zxdb {

System::System(Session* session) : ClientObject(session) {}
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

}  // namespace zxdb
