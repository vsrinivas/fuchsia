// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_THREAD_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_THREAD_HANDLE_H_

#include <zircon/syscalls/object.h>

#include <map>

#include "src/developer/debug/debug_agent/thread_handle.h"

namespace debug_agent {

class MockThreadHandle final : public ThreadHandle {
 public:
  struct WatchpointInstallation {
    debug_ipc::BreakpointType type;
    debug_ipc::AddressRange address_range;
  };

  MockThreadHandle(zx_koid_t process_koid, zx_koid_t thread_koid)
      : process_koid_(process_koid), thread_koid_(thread_koid) {}

  void set_state(uint32_t state) { state_ = state; }

  // Sets the values to be returned for the given register category query.
  void SetRegisterCategory(debug_ipc::RegisterCategory cat,
                           std::vector<debug_ipc::Register> values);

  // Sets the information to return for the next watchpoint set.
  void set_watchpoint_range_to_return(debug_ipc::AddressRange r) {
    watchpoint_range_to_return_ = r;
  }
  void set_watchpoint_slot_to_return(int slot) { watchpoint_slot_to_return_ = slot; }

  // Returns the number of breakpoint installs/uninstalls for the given address / total.
  size_t BreakpointInstallCount(uint64_t address) const;
  size_t TotalBreakpointInstallCalls() const;
  size_t BreakpointUninstallCount(uint64_t address) const;
  size_t TotalBreakpointUninstallCalls() const;

  // Log of all watchpoint additions.
  const std::vector<WatchpointInstallation>& watchpoint_installs() const {
    return watchpoint_installs_;
  }

  // Returns the number of watchpoint installs/uninstalls for the given address / total.
  size_t WatchpointInstallCount(const debug_ipc::AddressRange&) const;
  size_t TotalWatchpointInstallCalls() const;
  size_t WatchpointUninstallCount(const debug_ipc::AddressRange&) const;
  size_t TotalWatchpointUninstallCalls() const;

  // ThreadHandle implementation.
  const zx::thread& GetNativeHandle() const override { return null_handle_; }
  zx::thread& GetNativeHandle() override { return null_handle_; }
  zx_koid_t GetKoid() const override { return thread_koid_; }
  uint32_t GetState() const override { return state_; }
  debug_ipc::ThreadRecord GetThreadRecord() const override;
  zx::suspend_token Suspend() override;
  std::vector<debug_ipc::Register> ReadRegisters(
      const std::vector<debug_ipc::RegisterCategory>& cats_to_get) const override;
  std::vector<debug_ipc::Register> WriteRegisters(
      const std::vector<debug_ipc::Register>& regs) override;
  zx_status_t InstallHWBreakpoint(uint64_t address) override;
  zx_status_t UninstallHWBreakpoint(uint64_t address) override;
  arch::WatchpointInstallationResult InstallWatchpoint(
      debug_ipc::BreakpointType type, const debug_ipc::AddressRange& range) override;
  zx_status_t UninstallWatchpoint(const debug_ipc::AddressRange& range) override;

 private:
  // Always null, for returning only from the getters above.
  // TODO(brettw) Remove this when the ThreadHandle no longer exposes a zx::thread getter.
  zx::thread null_handle_;

  zx_koid_t process_koid_;
  zx_koid_t thread_koid_;

  std::vector<debug_ipc::Register>
      registers_[static_cast<size_t>(debug_ipc::RegisterCategory::kLast)];
  uint32_t state_ = ZX_THREAD_STATE_RUNNING;

  debug_ipc::AddressRange watchpoint_range_to_return_;
  int watchpoint_slot_to_return_ = 0;

  std::map<uint64_t, size_t> bp_installs_;
  std::map<uint64_t, size_t> bp_uninstalls_;

  std::vector<WatchpointInstallation> watchpoint_installs_;
  std::map<debug_ipc::AddressRange, size_t, debug_ipc::AddressRangeBeginCmp> wp_installs_;
  std::map<debug_ipc::AddressRange, size_t, debug_ipc::AddressRangeBeginCmp> wp_uninstalls_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_THREAD_HANDLE_H_
