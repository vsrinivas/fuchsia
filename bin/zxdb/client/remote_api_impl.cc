// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/remote_api_impl.h"

#include "garnet/bin/zxdb/client/session.h"
#include "garnet/public/lib/fxl/logging.h"

namespace zxdb {

RemoteAPIImpl::RemoteAPIImpl(Session* session) : session_(session) {}
RemoteAPIImpl::~RemoteAPIImpl() = default;

void RemoteAPIImpl::AddOrChangeBreakpoint(
    const debug_ipc::AddOrChangeBreakpointRequest& request,
    std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb) {
  session_->Send(request, std::move(cb));
}

void RemoteAPIImpl::RemoveBreakpoint(
    const debug_ipc::RemoveBreakpointRequest& request,
    std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb) {
  session_->Send(request, std::move(cb));
}

}  // namespace zxdb
