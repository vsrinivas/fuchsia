// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_THREAD_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_THREAD_HANDLE_H_

#include <lib/zx/thread.h>

#include <optional>
#include <vector>

#include "src/developer/debug/debug_agent/debug_registers.h"
#include "src/developer/debug/debug_agent/general_registers.h"
#include "src/developer/debug/debug_agent/suspend_handle.h"
#include "src/developer/debug/debug_agent/time.h"
#include "src/developer/debug/debug_agent/watchpoint_info.h"
#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/register_info.h"

namespace debug_agent {

// An abstract wrapper around an OS thread primitive. This abstraction is to allow mocking.
class ThreadHandle {
 public:
  struct State {
    explicit State(debug_ipc::ThreadRecord::State s = debug_ipc::ThreadRecord::State::kRunning,
                   debug_ipc::ThreadRecord::BlockedReason br =
                       debug_ipc::ThreadRecord::BlockedReason::kNotBlocked)
        : state(s), blocked_reason(br) {}

    // Creates a blocked state with the given reason.
    explicit State(debug_ipc::ThreadRecord::BlockedReason br)
        : state(debug_ipc::ThreadRecord::State::kBlocked), blocked_reason(br) {}

    // This state is common to check for and requires a combination of things to check.
    bool is_blocked_on_exception() const {
      return state == debug_ipc::ThreadRecord::State::kBlocked &&
             blocked_reason == debug_ipc::ThreadRecord::BlockedReason::kException;
    }

    debug_ipc::ThreadRecord::State state;
    debug_ipc::ThreadRecord::BlockedReason blocked_reason;
  };

  virtual ~ThreadHandle() = default;

  // Access to the underlying native thread object. This is for porting purposes, ideally this
  // object would encapsulate all details about the thread for testing purposes and this getter
  // would be removed. In testing situations, the returned value may be an empty object,
  // TODO(brettw) Remove this.
  virtual const zx::thread& GetNativeHandle() const = 0;
  virtual zx::thread& GetNativeHandle() = 0;

  virtual zx_koid_t GetKoid() const = 0;
  virtual std::string GetName() const = 0;

  virtual State GetState() const = 0;

  // Fills in everything but the stack into the returned thread record. Since the process koid
  // isn't known by the thread handle, it is passed in.
  virtual debug_ipc::ThreadRecord GetThreadRecord(zx_koid_t process_koid) const = 0;

  // ExceptionRecord.valid will be false on failure.
  virtual debug_ipc::ExceptionRecord GetExceptionRecord() const = 0;

  // Asynchronously suspends the thread. The thread will remain suspended as long as any suspend
  // handle is alive. See also WaitForSuspension().
  virtual std::unique_ptr<SuspendHandle> Suspend() = 0;

  // Waits for a previous suspend call to take effect. Does nothing if the thread is already
  // suspended. Returns true if we could find a valid suspension condition (either suspended or on
  // an exception). False if timeout or error.
  virtual bool WaitForSuspension(TickTimePoint deadline) const = 0;

  // Registers -------------------------------------------------------------------------------------

  // Reads and writes the general thread registers.
  virtual std::optional<GeneralRegisters> GetGeneralRegisters() const = 0;
  virtual void SetGeneralRegisters(const GeneralRegisters& regs) = 0;

  // Reads and writes the debug thread registers.
  virtual std::optional<DebugRegisters> GetDebugRegisters() const = 0;
  virtual bool SetDebugRegisters(const DebugRegisters& regs) = 0;

  // Puts the thread in or out of single-step mode.
  virtual void SetSingleStep(bool single_step) = 0;

  // Returns the current values of the given register categories.
  virtual std::vector<debug::RegisterValue> ReadRegisters(
      const std::vector<debug::RegisterCategory>& cats_to_get) const = 0;

  // Returns the new value of the registers that may have changed which is the result of reading
  // them after the write. This helps the client stay in sync. The may include other registers that
  // weren't updated.
  virtual std::vector<debug::RegisterValue> WriteRegisters(
      const std::vector<debug::RegisterValue>& regs) = 0;

  // Hardware breakpoints --------------------------------------------------------------------------

  // Installs or uninstalls hardware breakpoints.
  virtual bool InstallHWBreakpoint(uint64_t address) = 0;
  virtual bool UninstallHWBreakpoint(uint64_t address) = 0;

  // NOTE: AddressRange is what is used to differentiate watchpoints, not |type|.
  virtual std::optional<WatchpointInfo> InstallWatchpoint(debug_ipc::BreakpointType type,
                                                          const debug::AddressRange& range) = 0;
  virtual bool UninstallWatchpoint(const debug::AddressRange& range) = 0;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_THREAD_HANDLE_H_
