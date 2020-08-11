// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/mock_thread_handle.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/debug_agent/mock_suspend_handle.h"

namespace debug_agent {

zx::thread MockThreadHandle::null_handle_;

MockThreadHandle::MockThreadHandle(zx_koid_t thread_koid, std::string name)
    : thread_koid_(thread_koid), name_(std::move(name)), suspend_count_(std::make_shared<int>(0)) {
  // Tests could accidentally write to this handle since it's returned as a mutable value in some
  // cases. Catch accidents like that.
  FX_DCHECK(!null_handle_);
}

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

debug_ipc::ThreadRecord MockThreadHandle::GetThreadRecord(zx_koid_t process_koid) const {
  debug_ipc::ThreadRecord record;
  record.process_koid = process_koid;
  record.thread_koid = thread_koid_;
  record.name = "test thread";
  record.state = state_.state;
  record.blocked_reason = state_.blocked_reason;
  return record;
}

debug_ipc::ExceptionRecord MockThreadHandle::GetExceptionRecord() const {
  // Currently not implemented by this mock.
  return debug_ipc::ExceptionRecord();
}

std::unique_ptr<SuspendHandle> MockThreadHandle::Suspend() {
  return std::make_unique<MockSuspendHandle>(suspend_count_);
}

bool MockThreadHandle::WaitForSuspension(zx::time deadline) const { return true; }

std::optional<GeneralRegisters> MockThreadHandle::GetGeneralRegisters() const {
  return general_registers_;
}

void MockThreadHandle::SetGeneralRegisters(const GeneralRegisters& regs) {
  general_registers_ = regs;
}

std::optional<DebugRegisters> MockThreadHandle::GetDebugRegisters() const {
  return debug_registers_;
}

bool MockThreadHandle::SetDebugRegisters(const DebugRegisters& regs) {
  debug_registers_ = regs;
  return true;
}

void MockThreadHandle::SetSingleStep(bool single_step) { single_step_ = single_step; }

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

bool MockThreadHandle::InstallHWBreakpoint(uint64_t address) {
  bp_installs_[address]++;
  return true;
}

bool MockThreadHandle::UninstallHWBreakpoint(uint64_t address) {
  bp_uninstalls_[address]++;
  return true;
}

std::optional<WatchpointInfo> MockThreadHandle::InstallWatchpoint(
    debug_ipc::BreakpointType type, const debug_ipc::AddressRange& range) {
  watchpoint_installs_.push_back(WatchpointInstallation{.type = type, .address_range = range});

  wp_installs_[range]++;
  return WatchpointInfo(watchpoint_range_to_return_, watchpoint_slot_to_return_);
}

bool MockThreadHandle::UninstallWatchpoint(const debug_ipc::AddressRange& range) {
  wp_uninstalls_[range]++;
  return true;
}

}  // namespace debug_agent
