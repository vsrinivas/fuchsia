// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_THREAD_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_THREAD_HANDLE_H_

#include <lib/zx/thread.h>

#include <optional>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/thread_handle.h"

namespace debug_agent {

class ZirconThreadHandle final : public ThreadHandle {
 public:
  explicit ZirconThreadHandle(zx::thread t);

  // ThreadHandle implementation.
  const zx::thread& GetNativeHandle() const override { return thread_; }
  zx::thread& GetNativeHandle() override { return thread_; }
  zx_koid_t GetKoid() const override { return thread_koid_; }
  std::string GetName() const override;
  State GetState() const override;
  debug_ipc::ThreadRecord GetThreadRecord(zx_koid_t process_koid) const override;
  debug_ipc::ExceptionRecord GetExceptionRecord() const override;
  std::unique_ptr<SuspendHandle> Suspend() override;
  bool WaitForSuspension(TickTimePoint deadline) const override;
  std::optional<GeneralRegisters> GetGeneralRegisters() const override;
  void SetGeneralRegisters(const GeneralRegisters& regs) override;
  std::optional<DebugRegisters> GetDebugRegisters() const override;
  bool SetDebugRegisters(const DebugRegisters& regs) override;
  void SetSingleStep(bool single_step) override;
  std::vector<debug::RegisterValue> ReadRegisters(
      const std::vector<debug::RegisterCategory>& cats_to_get) const override;
  std::vector<debug::RegisterValue> WriteRegisters(
      const std::vector<debug::RegisterValue>& regs) override;
  bool InstallHWBreakpoint(uint64_t address) override;
  bool UninstallHWBreakpoint(uint64_t address) override;
  std::optional<WatchpointInfo> InstallWatchpoint(debug_ipc::BreakpointType type,
                                                  const debug::AddressRange& range) override;
  bool UninstallWatchpoint(const debug::AddressRange& range) override;

 private:
  zx_koid_t thread_koid_;
  zx::thread thread_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_THREAD_HANDLE_H_
