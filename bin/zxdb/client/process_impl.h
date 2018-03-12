// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/process.h"

#include <map>
#include <memory>

#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

class TargetImpl;
class ThreadImpl;

class ProcessImpl : public Process {
 public:
  ProcessImpl(TargetImpl* target, uint64_t koid);
  ~ProcessImpl() override;

  // Process implementation:
  Target* GetTarget() const override;
  uint64_t GetKoid() const override;
  std::vector<Thread*> GetThreads() const override;
  void OnThreadStarting(uint64_t thread_koid) override;
  void OnThreadExiting(uint64_t thread_koid) override;

 private:
  Target* const target_;  // The target owns |this|.
  const uint64_t koid_;

  // Threads indexed by their thread koid.
  std::map<uint64_t, std::unique_ptr<ThreadImpl>> threads_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessImpl);
};

}  // namespace zxdb
