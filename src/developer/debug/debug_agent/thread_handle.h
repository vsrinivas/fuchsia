// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_THREAD_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_THREAD_HANDLE_H_

#include <lib/zx/thread.h>

#include <vector>

#include "src/developer/debug/debug_agent/arch_helpers.h"
#include "src/developer/debug/ipc/records.h"

namespace debug_agent {

// An abstract wrapper around an OS thread primitive. This abstraction is to allow mocking.
class ThreadHandle {
 public:
  virtual ~ThreadHandle() = default;

  // Access to the underlying native thread object. This is for porting purposes, ideally this
  // object would encapsulate all details about the thread for testing purposes and this getter
  // would be removed. In testing situations, the returned value may be an empty object,
  // TODO(brettw) Remove this.
  virtual const zx::thread& GetNativeHandle() const = 0;
  virtual zx::thread& GetNativeHandle() = 0;

  virtual zx_koid_t GetKoid() const = 0;

  // Returns a ZX_THREAD_STATE_* enum for the thread.
  virtual uint32_t GetState() const = 0;

  // Fills in everything but the stack into the returned thread record.
  virtual debug_ipc::ThreadRecord GetThreadRecord() const = 0;

  // TODO(brettw) probably needs a suspension wait timeout.
  virtual zx::suspend_token Suspend() = 0;

  // Registers -------------------------------------------------------------------------------------

  // Returns the current values of the given register categories.
  virtual std::vector<debug_ipc::Register> ReadRegisters(
      const std::vector<debug_ipc::RegisterCategory>& cats_to_get) const = 0;

  // Returns the new value of the registers that may have changed which is the result of reading
  // them after the write. This helps the client stay in sync. The may include other registers that
  // weren't updated.
  virtual std::vector<debug_ipc::Register> WriteRegisters(
      const std::vector<debug_ipc::Register>& regs) = 0;

  // Hardware breakpoints --------------------------------------------------------------------------

  // Installs or uninstalls hardware breakpoints.
  virtual zx_status_t InstallHWBreakpoint(uint64_t address) = 0;
  virtual zx_status_t UninstallHWBreakpoint(uint64_t address) = 0;

  // NOTE: AddressRange is what is used to differentiate watchpoints, not |type|.
  virtual arch::WatchpointInstallationResult InstallWatchpoint(
      debug_ipc::BreakpointType type, const debug_ipc::AddressRange& range) = 0;
  virtual zx_status_t UninstallWatchpoint(const debug_ipc::AddressRange& range) = 0;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_THREAD_HANDLE_H_
