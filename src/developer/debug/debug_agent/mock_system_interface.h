// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_SYSTEM_INTERFACE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_SYSTEM_INTERFACE_H_

#include <memory>

#include "src/developer/debug/debug_agent/mock_component_manager.h"
#include "src/developer/debug/debug_agent/mock_job_handle.h"
#include "src/developer/debug/debug_agent/mock_limbo_provider.h"
#include "src/developer/debug/debug_agent/system_interface.h"

namespace debug_agent {

class MockSystemInterface final : public SystemInterface {
 public:
  explicit MockSystemInterface(MockJobHandle root_job)
      : root_job_(std::move(root_job)), component_manager_(this) {}

  MockLimboProvider& mock_limbo_provider() { return limbo_provider_; }
  MockComponentManager& mock_component_manager() { return component_manager_; }

  // SystemInterface implementation:
  uint32_t GetNumCpus() const override { return 2; }
  uint64_t GetPhysicalMemory() const override { return 1073741824; }  // 1GB
  std::unique_ptr<JobHandle> GetRootJob() const override;
  std::unique_ptr<BinaryLauncher> GetLauncher() const override;
  ComponentManager& GetComponentManager() override { return component_manager_; }
  LimboProvider& GetLimboProvider() override { return limbo_provider_; }
  std::string GetSystemVersion() override { return "Mock version"; }

  // Creates a default process tree:
  //
  //  j: 1 root
  //    p: 2 root-p1
  //      t: 3 initial-thread
  //    p: 4 root-p2
  //      t: 5 initial-thread
  //    p: 6 root-p3
  //      t: 7 initial-thread
  //    j: 8 job1  /moniker  fuchsia-pkg://devhost/package#meta/component.cm
  //      p: 9 job1-p1
  //        t: 10 initial-thread
  //      p: 11 job1-p2
  //        t: 12 initial-thread
  //      j: 13 job11
  //        p: 14 job11-p1
  //          t: 15 initial-thread
  //          t: 16 second-thread
  //      j: 17 job12
  //        j: 18 job121
  //          p: 19 job121-p1
  //            t: 20 initial-thread
  //          p: 21 job121-p2
  //            t: 22 initial-thread
  //            t: 23 second-thread
  //            t: 24 third-thread
  static std::unique_ptr<MockSystemInterface> CreateWithData();

 private:
  MockJobHandle root_job_;
  MockComponentManager component_manager_;
  MockLimboProvider limbo_provider_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_SYSTEM_INTERFACE_H_
