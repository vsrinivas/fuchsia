// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/lib/debug_ipc/protocol.h"

namespace zxdb {

class Err;

// Represents system-wide state on the debugged computer.
class System : public ClientObject {
 public:
  // Callback for requesting the process tree.
  using ProcessTreeCallback = std::function<
      void(System*, const Err&, debug_ipc::ProcessTreeReply)>;

  System(Session* session);
  ~System() override;

  void GetProcessTree(ProcessTreeCallback callback);
};

}  // namespace zxdb
