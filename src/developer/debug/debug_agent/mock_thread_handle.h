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

  explicit MockThreadHandle(zx_koid_t thread_koid, std::string name = std::string());

  // Note that this state is always returned. The thread could have been Suspend()-ed which will
  // indiate is_suspended(), but the GetState will still report the value set here.
  void set_state(State s) { state_ = s; }

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

  // Returns the suspend count for implementing reference-counted suspension via MockSuspendHandle.
  int suspend_count() const { return *suspend_count_; }
  bool is_suspended() const { return suspend_count() > 0; }

  bool single_step() const { return single_step_; }

  // ThreadHandle implementation.
  const zx::thread& GetNativeHandle() const override { return null_handle_; }
  zx::thread& GetNativeHandle() override { return null_handle_; }
  zx_koid_t GetKoid() const override { return thread_koid_; }
  std::string GetName() const override { return name_; }
  State GetState() const override { return state_; }
  debug_ipc::ThreadRecord GetThreadRecord(zx_koid_t process_koid) const override;
  debug_ipc::ExceptionRecord GetExceptionRecord() const override;
  std::unique_ptr<SuspendHandle> Suspend() override;
  bool WaitForSuspension(zx::time deadline) const override;
  std::optional<GeneralRegisters> GetGeneralRegisters() const override;
  void SetGeneralRegisters(const GeneralRegisters& regs) override;
  std::optional<DebugRegisters> GetDebugRegisters() const override;
  bool SetDebugRegisters(const DebugRegisters& regs) override;
  void SetSingleStep(bool single_step) override;
  std::vector<debug_ipc::Register> ReadRegisters(
      const std::vector<debug_ipc::RegisterCategory>& cats_to_get) const override;
  std::vector<debug_ipc::Register> WriteRegisters(
      const std::vector<debug_ipc::Register>& regs) override;
  bool InstallHWBreakpoint(uint64_t address) override;
  bool UninstallHWBreakpoint(uint64_t address) override;
  std::optional<WatchpointInfo> InstallWatchpoint(debug_ipc::BreakpointType type,
                                                  const debug_ipc::AddressRange& range) override;
  bool UninstallWatchpoint(const debug_ipc::AddressRange& range) override;

 private:
  // Always null, for returning only from the getters above.
  // TODO(brettw) Remove this when the ThreadHandle no longer exposes a zx::thread getter.
  static zx::thread null_handle_;

  zx_koid_t thread_koid_;
  std::string name_;

  std::vector<debug_ipc::Register>
      registers_[static_cast<size_t>(debug_ipc::RegisterCategory::kLast)];

  State state_;
  bool single_step_ = false;
  GeneralRegisters general_registers_;
  DebugRegisters debug_registers_;

  debug_ipc::AddressRange watchpoint_range_to_return_;
  int watchpoint_slot_to_return_ = 0;

  std::map<uint64_t, size_t> bp_installs_;
  std::map<uint64_t, size_t> bp_uninstalls_;

  std::vector<WatchpointInstallation> watchpoint_installs_;
  std::map<debug_ipc::AddressRange, size_t, debug_ipc::AddressRangeBeginCmp> wp_installs_;
  std::map<debug_ipc::AddressRange, size_t, debug_ipc::AddressRangeBeginCmp> wp_uninstalls_;

  // Shared count modifies by the MockSuspendHandles. Positive indicates this thread is suspended.
  std::shared_ptr<int> suspend_count_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_THREAD_HANDLE_H_
