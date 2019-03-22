// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/developer/debug/debug_agent/debugged_process.h"

namespace debug_agent {

// Meant to be used by tests for having light-weight processes that don't talk
// to zircon in order to spin up threads.
class MockProcess : public DebuggedProcess {
 public:
  MockProcess(zx_koid_t koid);
  ~MockProcess();

  void AddThread(zx_koid_t koid);

  DebuggedThread* GetThread(zx_koid_t koid) const override;

  std::vector<DebuggedThread*> GetThreads() const override;

 private:
  std::map<zx_koid_t, std::unique_ptr<DebuggedThread>> threads_;
};

}  // namespace debug_agent
