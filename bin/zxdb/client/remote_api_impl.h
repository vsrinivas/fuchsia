// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/remote_api.h"

namespace zxdb {

class Session;

class RemoteAPIImpl : public RemoteAPI {
 public:
  // The session must outlive this object.
  explicit RemoteAPIImpl(Session* session);
  ~RemoteAPIImpl();

  // RemoteAPI implementation.
  void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb)
      override;
  void RemoveBreakpoint(
      const debug_ipc::RemoveBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb)
      override;

 private:
  Session* session_;
};

}  // namespace zxdb
