// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_SYSTEM_INTERFACE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_SYSTEM_INTERFACE_H_

#include <memory>

#include "src/developer/debug/debug_agent/system_interface.h"
#include "src/developer/debug/debug_agent/zircon_job_handle.h"

namespace debug_agent {

class ZirconSystemInterface final : public SystemInterface {
 public:
  explicit ZirconSystemInterface();

  // SystemInterface implementation:
  JobHandle& GetRootJob() override { return root_job_; }
  std::unique_ptr<ProcessHandle> GetProcess(zx_koid_t process_koid) const override;

 private:
  ZirconJobHandle root_job_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_SYSTEM_INTERFACE_H_
