// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_ARCH_PROVIDER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_ARCH_PROVIDER_H_

#include <functional>
#include <map>

#include "src/developer/debug/debug_agent/arch.h"

namespace debug_agent {

// This is meant to mock the platform and being able to track what installations
// the code is doing within the tests.
class MockArchProvider : public arch::ArchProvider {
 public:
  zx_status_t InstallHWBreakpoint(const zx::thread& thread, uint64_t address) override;

  zx_status_t UninstallHWBreakpoint(const zx::thread& thread, uint64_t address) override;

  arch::WatchpointInstallationResult InstallWatchpoint(const zx::thread&,
                                                       const debug_ipc::AddressRange&) override;

  zx_status_t UninstallWatchpoint(const zx::thread&, const debug_ipc::AddressRange&) override;

  size_t BreakpointInstallCount(uint64_t address) const;
  size_t TotalBreakpointInstallCalls() const;
  size_t BreakpointUninstallCount(uint64_t address) const;
  size_t TotalBreakpointUninstallCalls() const;

  size_t WatchpointInstallCount(const debug_ipc::AddressRange&) const;
  size_t TotalWatchpointInstallCalls() const;
  size_t WatchpointUninstallCount(const debug_ipc::AddressRange&) const;
  size_t TotalWatchpointUninstallCalls() const;

 private:
  std::map<uint64_t, size_t> bp_installs_;
  std::map<uint64_t, size_t> bp_uninstalls_;

  std::map<debug_ipc::AddressRange, size_t, debug_ipc::AddressRangeBeginCmp> wp_installs_;
  std::map<debug_ipc::AddressRange, size_t, debug_ipc::AddressRangeBeginCmp> wp_uninstalls_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_ARCH_PROVIDER_H_
