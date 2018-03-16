// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <vector>

#include "garnet/bin/zxdb/client/system.h"
#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

class ProcessImpl;
class TargetImpl;

class SystemImpl : public System {
 public:
  explicit SystemImpl(Session* session);
  ~SystemImpl() override;

  ProcessImpl* ProcessImplFromKoid(uint64_t koid) const;

  // System implementation:
  std::vector<Target*> GetAllTargets() const override;
  Process* ProcessFromKoid(uint64_t koid) const override;
  void GetProcessTree(ProcessTreeCallback callback) override;
  Target* CreateNewTarget(Target* clone) override;
  void Continue() override;

 private:
  void AddNewTarget(std::unique_ptr<TargetImpl> target);

  std::vector<std::unique_ptr<TargetImpl>> targets_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SystemImpl);
};

}  // namespace zxdb
