// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_SYSTEM_INTERFACE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_SYSTEM_INTERFACE_H_

#include <memory>

#include "src/developer/debug/debug_agent/mock_job_handle.h"
#include "src/developer/debug/debug_agent/mock_limbo_provider.h"
#include "src/developer/debug/debug_agent/system_interface.h"

namespace debug_agent {

class MockSystemInterface final : public SystemInterface {
 public:
  explicit MockSystemInterface(MockJobHandle root_job) : root_job_(std::move(root_job)) {}

  MockLimboProvider& mock_limbo_provider() { return limbo_provider_; }

  // SystemInterface implementation:
  uint32_t GetNumCpus() const override { return 2; }
  uint64_t GetPhysicalMemory() const override { return 1073741824; }  // 1GB
  std::unique_ptr<JobHandle> GetRootJob() const override;
  std::unique_ptr<JobHandle> GetComponentRootJob() const override;
  std::unique_ptr<BinaryLauncher> GetLauncher() const override;
  std::unique_ptr<ComponentLauncher> GetComponentLauncher() const override;
  LimboProvider& GetLimboProvider() override { return limbo_provider_; }
  std::string GetSystemVersion() override { return "Mock version"; }

 private:
  MockJobHandle root_job_;
  MockLimboProvider limbo_provider_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_SYSTEM_INTERFACE_H_
