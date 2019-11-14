// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_arch_provider.h"

namespace debug_agent {

zx_status_t MockArchProvider::InstallHWBreakpoint(zx::thread* thread, uint64_t address) {
  bp_installs_[address]++;
  return ZX_OK;
}

zx_status_t MockArchProvider::UninstallHWBreakpoint(zx::thread* thread, uint64_t address) {
  bp_uninstalls_[address]++;
  return ZX_OK;
}

arch::WatchpointInstallationResult MockArchProvider::InstallWatchpoint(
    zx::thread*, const debug_ipc::AddressRange& range) {
  wp_installs_[range]++;
  return arch::WatchpointInstallationResult(ZX_OK, range, 0);
}

zx_status_t MockArchProvider::UninstallWatchpoint(zx::thread*,
                                                  const debug_ipc::AddressRange& range) {
  wp_uninstalls_[range]++;
  return ZX_OK;
}

size_t MockArchProvider::BreakpointInstallCount(uint64_t address) const {
  auto it = bp_installs_.find(address);
  if (it == bp_installs_.end())
    return 0;
  return it->second;
}

size_t MockArchProvider::TotalBreakpointInstallCalls() const {
  int total = 0;
  for (auto it : bp_installs_) {
    total += it.second;
  }
  return total;
}

size_t MockArchProvider::BreakpointUninstallCount(uint64_t address) const {
  auto it = bp_uninstalls_.find(address);
  if (it == bp_uninstalls_.end())
    return 0;
  return it->second;
}

size_t MockArchProvider::TotalBreakpointUninstallCalls() const {
  int total = 0;
  for (auto it : bp_uninstalls_) {
    total += it.second;
  }
  return total;
}

size_t MockArchProvider::WatchpointInstallCount(const debug_ipc::AddressRange& range) const {
  auto it = wp_installs_.find(range);
  if (it == wp_installs_.end())
    return 0;
  return it->second;
}

size_t MockArchProvider::TotalWatchpointInstallCalls() const {
  int total = 0;
  for (auto it : wp_installs_) {
    total += it.second;
  }
  return total;
}

size_t MockArchProvider::WatchpointUninstallCount(const debug_ipc::AddressRange& range) const {
  auto it = wp_uninstalls_.find(range);
  if (it == wp_uninstalls_.end())
    return 0;
  return it->second;
}

size_t MockArchProvider::TotalWatchpointUninstallCalls() const {
  int total = 0;
  for (auto it : wp_uninstalls_) {
    total += it.second;
  }
  return total;
}

}  // namespace debug_agent
