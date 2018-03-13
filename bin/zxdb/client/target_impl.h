// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/target.h"
#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

class ProcessImpl;
class SystemImpl;

class TargetImpl : public Target {
 public:
  // The system owns this object and will outlive it.
  explicit TargetImpl(SystemImpl* system);
  ~TargetImpl() override;

  ProcessImpl* process() { return process_.get(); }

  // Allocates a new target with the same settings as this one. This isn't
  // a real copy, because any process information is not cloned.
  std::unique_ptr<TargetImpl> Clone(SystemImpl* system);

  // Target implementation:
  State GetState() const override;
  Process* GetProcess() const override;
  const std::vector<std::string>& GetArgs() const override;
  void SetArgs(std::vector<std::string> args) override;
  void Launch(LaunchCallback callback) override;
  void Attach(uint64_t koid, LaunchCallback callback) override;

 private:
  void OnLaunchOrAttachReply(const Err& err, uint64_t koid, uint32_t status,
                             LaunchCallback callback);

  State state_ = kStopped;

  std::vector<std::string> args_;

  // Associated process if there is one.
  std::unique_ptr<ProcessImpl> process_;

  std::shared_ptr<WeakThunk<TargetImpl>> weak_thunk_;
  // ^ Keep at the bottom to make sure it's destructed last.

  FXL_DISALLOW_COPY_AND_ASSIGN(TargetImpl);
};

}  // namespace zxdb
