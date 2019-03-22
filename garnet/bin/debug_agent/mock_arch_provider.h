// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <map>

#include "garnet/bin/debug_agent/arch.h"
#include "src/developer/debug/ipc/records_utils.h"

namespace debug_agent {

// This is meant to mock the platform and being able to track what installations
// the code is doing within the tests.
class MockArchProvider : public arch::ArchProvider {
 public:
  zx_status_t InstallHWBreakpoint(zx::thread* thread,
                                  uint64_t address) override;

  zx_status_t UninstallHWBreakpoint(zx::thread* thread,
                                    uint64_t address) override;

  zx_status_t InstallWatchpoint(zx::thread*,
                                const debug_ipc::AddressRange&) override;

  zx_status_t UninstallWatchpoint(zx::thread*,
                                  const debug_ipc::AddressRange&) override;

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

  std::map<debug_ipc::AddressRange, size_t, debug_ipc::AddressRangeCompare>
      wp_installs_;
  std::map<debug_ipc::AddressRange, size_t, debug_ipc::AddressRangeCompare>
      wp_uninstalls_;
};

// Meant as an RAII container for MockArchProvider.
class ScopedMockArchProvider {
 public:
  ScopedMockArchProvider() {
    auto fake_arch = std::make_unique<MockArchProvider>();
    fake_arch_ = fake_arch.get();
    arch::ArchProvider::Set(std::move(fake_arch));
  }

  ~ScopedMockArchProvider() { arch::ArchProvider::Set(nullptr); }

  MockArchProvider* get_provider() const { return fake_arch_; }

 private:
  MockArchProvider* fake_arch_;
};

}  // namespace debug_agent
