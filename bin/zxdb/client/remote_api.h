// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include "garnet/lib/debug_ipc/protocol.h"

namespace zxdb {

class Err;

// Abstracts the IPC layer for sending messages to the debug agent. This allows
// mocking of the interface without dealing with the innards of the
// serialization.
//
// The default implementations of each of these functions asserts. The Session
// implements overrides that actually send and receive messages. Tests should
// derive from this and implement the messages they expect.
//
// TODO(brettw) convert all other IPCs to this and make Session::Send an
// implementation detail of this class.
class RemoteAPI {
 public:
  virtual ~RemoteAPI() = default;

  virtual void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)>
          cb);
  virtual void RemoveBreakpoint(
      const debug_ipc::RemoveBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb);
};

}  // namespace zxdb
