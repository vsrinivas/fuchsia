// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

#include "arch.h"
#include "breakpoint.h"
#include "registers.h"

namespace inferior_control {

class Process;

// Represents a thread that is owned by a Process instance.
class Thread final {
 public:
  enum class State {
    kNew,
    kStopped,
    kRunning,
    kStepping,
    kExiting,
    kGone,
  };

  Thread(Process* process, zx_handle_t handle, zx_koid_t id);
  ~Thread();

  Process* process() const { return process_; }
  zx_handle_t handle() const { return handle_; }
  zx_koid_t id() const { return id_; }

  std::string GetName() const;

  // Same as GetName() except includes the ids in hex.
  // This helps matching with thread names in packets.
  std::string GetDebugName() const;

  // Returns a weak pointer to this Thread instance.
  fxl::WeakPtr<Thread> AsWeakPtr();

  // Returns a pointer to the arch::Registers object associated with this
  // thread. The returned pointer is owned by this Thread instance and should
  // not be deleted by the caller.
  Registers* registers() const { return registers_.get(); }

  // Returns the current state of this thread.
  State state() const { return state_; }

  // Returns true if thread is alive. It could be stopped, but it's still
  // alive.
  bool IsLive() const;

  static const char* StateName(Thread::State state);

  // Returns a GDB signal number based on the current exception context. If no
  // exception context was set on this Thread or if the exception data from the
  // context does not map to a meaningful GDB signal number, this method returns
  // GdbSignal::kUnsupported.
  // TODO(dje): kNone might be a better value if there is no exception.
  GdbSignal GetGdbSignal() const;

  // Called when the thread gets an exception.
  void OnException(const zx_excp_type_t type,
                   const zx_exception_context_t& context);

  // Resumes the thread from a "stopped in exception" state. Returns true on
  // success, false on failure. The thread state on return is kRunning.
  bool Resume();

  // Resumes the thread from an ZX_EXCP_THREAD_EXITING exception.
  // The thread state on entry must one of kNew, kStopped, kExiting.
  // The thread state on return is kGone.
  void ResumeForExit();

  // Steps the thread from a "stopped in exception" state. Returns true on
  // success, false on failure.
  bool Step();

#ifdef __x86_64__
  // Intel PT buffer access
  int32_t ipt_buffer() const { return ipt_buffer_; }
  void set_ipt_buffer(int32_t ipt_buffer) { ipt_buffer_ = ipt_buffer; }
#endif

 private:
  friend class Process;

  // Called by Process to set the state of its threads.
  void set_state(State state);

  // Release all resources held by the thread.
  // Called after all other processing of a thread exit has been done.
  void Clear();

  // The owning process.
  Process* process_;  // weak

  // The debug-capable handle that we use to invoke zx_debug_* syscalls.
  zx_handle_t handle_;

  // The thread ID (also the kernel object ID) of this thread.
  zx_koid_t id_;

  // The arch::Registers object associated with this thread.
  std::unique_ptr<Registers> registers_;

  // The current state of the this thread.
  State state_;

#ifdef __x86_64__
  // The Intel Processor Trace buffer descriptor attached to this thread,
  // or -1 if none.
  int32_t ipt_buffer_;
#endif

  // The collection of breakpoints that belong to this thread.
  ThreadBreakpointSet breakpoints_;

  // Pointer to the most recent exception context that this Thread received via
  // an architectural exception. Contains nullptr if the thread never received
  // an exception.
  std::unique_ptr<zx_exception_context_t> exception_context_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  fxl::WeakPtrFactory<Thread> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Thread);
};

}  // namespace inferior_control
