// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <map>
#include <memory>

#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/lib/debug_ipc/protocol.h"

namespace zxdb {

class Err;

// Represents system-wide state on the debugged computer.
class System : public ClientObject {
 public:
  using TargetMap = std::map<size_t, std::unique_ptr<Target>>;

  // Callback for requesting the process tree.
  using ProcessTreeCallback = std::function<
      void(System*, const Err&, debug_ipc::ProcessTreeReply)>;

  System(Session* session);
  ~System() override;

  // The active target is guaranteed to exist.
  size_t active_target_id() const { return active_target_id_; }
  Target* GetActiveTarget() const;

  const TargetMap& targets() const { return targets_; }

  // Returns the process (and hence Target) associated with the given live
  // koid. Returns 0 if not found.
  Process* ProcessFromKoid(uint64_t koid);

  void GetProcessTree(ProcessTreeCallback callback);

 private:
  TargetMap targets_;
  size_t next_target_id_ = 0;

  size_t active_target_id_ = 0;
};

}  // namespace zxdb
