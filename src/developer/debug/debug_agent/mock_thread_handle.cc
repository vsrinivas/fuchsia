// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_thread_handle.h"

#include <lib/syslog/cpp/macros.h>

namespace debug_agent {

void MockThreadHandle::SetRegisterCategory(debug_ipc::RegisterCategory cat,
                                           std::vector<debug_ipc::Register> values) {
  FX_CHECK(static_cast<size_t>(cat) < std::size(registers_));
  registers_[static_cast<size_t>(cat)] = std::move(values);
}

size_t MockThreadHandle::BreakpointInstallCount(uint64_t address) const {
  auto it = bp_installs_.find(address);
  if (it == bp_installs_.end())
    return 0;
  return it->second;
}

size_t MockThreadHandle::TotalBreakpointInstallCalls() const {
  int total = 0;
  for (auto it : bp_installs_)
    total += it.second;
  return total;
}

size_t MockThreadHandle::BreakpointUninstallCount(uint64_t address) const {
  auto it = bp_uninstalls_.find(address);
  if (it == bp_uninstalls_.end())
    return 0;
  return it->second;
}

size_t MockThreadHandle::TotalBreakpointUninstallCalls() const {
  int total = 0;
  for (auto it : bp_uninstalls_)
    total += it.second;
  return total;
}

size_t MockThreadHandle::WatchpointInstallCount(const debug_ipc::AddressRange& range) const {
  auto it = wp_installs_.find(range);
  if (it == wp_installs_.end())
    return 0;
  return it->second;
}

size_t MockThreadHandle::TotalWatchpointInstallCalls() const {
  int total = 0;
  for (auto it : wp_installs_)
    total += it.second;
  return total;
}

size_t MockThreadHandle::WatchpointUninstallCount(const debug_ipc::AddressRange& range) const {
  auto it = wp_uninstalls_.find(range);
  if (it == wp_uninstalls_.end())
    return 0;
  return it->second;
}

size_t MockThreadHandle::TotalWatchpointUninstallCalls() const {
  int total = 0;
  for (auto it : wp_uninstalls_)
    total += it.second;
  return total;
}

debug_ipc::ThreadRecord MockThreadHandle::GetThreadRecord() const {
  debug_ipc::ThreadRecord record;
  record.process_koid = process_koid_;
  record.thread_koid = thread_koid_;
  record.name = "test thread";
  record.state = state_.state;
  record.blocked_reason = state_.blocked_reason;
  return record;
}

zx::suspend_token MockThreadHandle::Suspend() { return zx::suspend_token(); }

std::optional<GeneralRegisters> MockThreadHandle::GetGeneralRegisters() const {
  return general_registers_;
}

void MockThreadHandle::SetGeneralRegisters(const GeneralRegisters& regs) {
  general_registers_ = regs;
}

std::vector<debug_ipc::Register> MockThreadHandle::ReadRegisters(
    const std::vector<debug_ipc::RegisterCategory>& cats_to_get) const {
  std::vector<debug_ipc::Register> result;
  for (const auto cat : cats_to_get) {
    FX_CHECK(static_cast<size_t>(cat) < std::size(registers_));

    const auto& source = registers_[static_cast<size_t>(cat)];
    result.insert(result.end(), source.begin(), source.end());
  }

  return result;
}

std::vector<debug_ipc::Register> MockThreadHandle::WriteRegisters(
    const std::vector<debug_ipc::Register>& regs) {
  // Return the same values as the input to pretend the write succeeded.
  return regs;
}

zx_status_t MockThreadHandle::InstallHWBreakpoint(uint64_t address) {
  bp_installs_[address]++;
  return ZX_OK;
}

zx_status_t MockThreadHandle::UninstallHWBreakpoint(uint64_t address) {
  bp_uninstalls_[address]++;
  return ZX_OK;
}

arch::WatchpointInstallationResult MockThreadHandle::InstallWatchpoint(
    debug_ipc::BreakpointType type, const debug_ipc::AddressRange& range) {
  watchpoint_installs_.push_back(WatchpointInstallation{.type = type, .address_range = range});

  wp_installs_[range]++;
  return arch::WatchpointInstallationResult(ZX_OK, watchpoint_range_to_return_,
                                            watchpoint_slot_to_return_);
}

zx_status_t MockThreadHandle::UninstallWatchpoint(const debug_ipc::AddressRange& range) {
  wp_uninstalls_[range]++;
  return ZX_OK;
}

}  // namespace debug_agent
