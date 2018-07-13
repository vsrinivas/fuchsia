// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

#include "garnet/lib/debugger_utils/dso_list.h"
#include "garnet/lib/debugger_utils/util.h"
#include "garnet/lib/process/process_builder.h"
#include "lib/fxl/macros.h"

#include "breakpoint.h"
#include "exception_port.h"
#include "memory_process.h"
#include "thread.h"

namespace inferior_control {

class Server;
class Thread;

// Represents an inferior process that we're attached to.
class Process final {
 public:
 public:
  enum class State { kNew, kStarting, kRunning, kGone };

  // Delegate interface for listening to Process life-time events.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Called when a new thread that is part of this process has been started.
    // This is indicated by ZX_EXCP_THREAD_STARTING.
    virtual void OnThreadStarting(Process* process, Thread* thread,
                                  const zx_exception_context_t& context) = 0;

    // Called when |thread| has exited (ZX_EXCP_THREAD_EXITING).
    virtual void OnThreadExiting(Process* process, Thread* thread,
                                 const zx_exception_context_t& context) = 0;

    // Called when |process| has exited.
    virtual void OnProcessExit(Process* process) = 0;

    // Called when the kernel reports an architectural exception.
    virtual void OnArchitecturalException(
        Process* process, Thread* thread, const zx_excp_type_t type,
        const zx_exception_context_t& context) = 0;

    // Called when |thread| has gets a synthetic exception
    // (e.g., ZX_EXCP_POLICY_ERROR) that is akin to an architectural
    // exception: the program got an error and by default crashes.
    virtual void OnSyntheticException(
        Process* process, Thread* thread, const zx_excp_type_t type,
        const zx_exception_context_t& context) = 0;
  };

  explicit Process(Server* server, Delegate* delegate_);
  ~Process();

  std::string GetName() const;

  // Note: This includes the program in |argv[0]|.
  const debugger_utils::Argv& argv() { return argv_; }
  void set_argv(const debugger_utils::Argv& argv) { argv_ = argv; }

  // Add extra handles to the process.
  //
  // Must be called before |Initialize|.
  void AddStartupHandle(fuchsia::process::HandleInfo handle);

  // Returns the current state of this process.
  State state() const { return state_; }

  // Change the state to |new_state|.
  void set_state(State new_state);

  static const char* StateName(Process::State state);

  // Creates and initializes the inferior process, via ProcessBuilder, but does
  // not start it. set_argv() must have already been called.
  // This also "attaches" to the inferior: A debug-capable process handle is
  // obtained and the debugger exception port is bound to.
  // Returns false if there is an error.
  // Do not call this if the process is currently live (state is kStarting or
  // kRunning).
  bool Initialize();

  // Attach to running program with pid |pid|.
  // A debug-capable process handle is obtained and the debugger exception
  // port is bound to.
  // Returns false if there is an error.
  // Do not call this if the process is currently live (state is kStarting or
  // kRunning).
  bool Attach(zx_koid_t pid);

  // Detach from an attached process, and return to pre-attached state.
  // This includes unbinding from the exception port and closing the process
  // handle. To keep things simple and clean "detach" means "release all
  // connections with the inferior". After detaching we should have absolutely
  // no effect on the inferior, including not preserving the lifetime of the
  // kernel process instance because we still have a handle of the process.
  // Returns true on success, or false if already detached.
  bool Detach();

  // Starts running the process. Returns false in case of an error.
  // Initialize() MUST be called successfully before calling Start().
  bool Start();

  // Terminate the process.
  bool Kill();

  // Returns true if the process is running or has been running.
  bool IsLive() const;

  // Returns true if the process is currently attached.
  bool IsAttached() const;

  // Returns the process handle. This handle is owned and managed by this
  // Process instance, thus the caller should not close the handle.
  zx_handle_t handle() const { return handle_; }

  // Returns the process ID.
  zx_koid_t id() const { return id_; }

  // Returns a mutable handle to the set of breakpoints managed by this process.
  ProcessBreakpointSet* breakpoints() { return &breakpoints_; }

  // Returns the base load address of the dynamic linker.
  zx_vaddr_t base_address() const { return base_address_; }

  // Returns the entry point of the dynamic linker.
  zx_vaddr_t entry_address() const { return entry_address_; }

  // Returns the thread with the thread ID |thread_id| that's owned by this
  // process. Returns nullptr if no such thread exists. The returned pointer is
  // owned and managed by this Process instance.
  Thread* FindThreadById(zx_koid_t thread_id);

  // Returns an arbitrary thread that is owned by this process. This picks the
  // first thread that is returned from zx_object_get_info for the
  // ZX_INFO_PROCESS_THREADS topic. This will refresh all threads.
  // TODO(dje): ISTR GNU gdbserver being more random to avoid starving threads.
  Thread* PickOneThread();

  // If the thread map might be stale, refresh it.
  void EnsureThreadMapFresh();

  // Refreshes the complete Thread list for this process. Returns false if an
  // error is returned from a syscall.
  bool RefreshAllThreads();

  // Iterates through all cached threads and invokes |callback| for each of
  // them. |callback| is guaranteed to get called only before ForEachThread()
  // returns, so it is safe to bind local variables to |callback|.
  using ThreadCallback = fit::function<void(Thread*)>;
  void ForEachThread(const ThreadCallback& callback);
  // Same as ForEachThread except ignores State::Gone threads.
  void ForEachLiveThread(const ThreadCallback& callback);

  // Reads the block of memory of length |length| bytes starting at address
  // |address| into |out_buffer|. |out_buffer| must be at least as large as
  // |length|. Returns true on success or false on failure.
  bool ReadMemory(uintptr_t address, void* out_buffer, size_t length);

  // Writes the block of memory of length |length| bytes from |data| to the
  // memory address |address| of this process. Returns true on success or false
  // on failure.
  bool WriteMemory(uintptr_t address, const void* data, size_t length);

  // Fetch the process's exit code.
  int ExitCode();

  bool attached_running() const { return attached_running_; }

  // Build list of loaded dsos.
  // This must be called at a point where it is safe to read the list.
  // If |check_ldso_bkpt| is true then verify |thread| is stopped at the
  // dynamic linker breakpoint. If not then skip trying. Otherwise |thread|
  // must be nullptr.
  // TODO(dje): Maybe just pass |thread|, later.
  // TODO(dje): For the rsp server this is only called once, after the main
  // executable has been loaded. Therefore this list will not contain
  // subsequently loaded dsos.
  void TryBuildLoadedDsosList(Thread* thread, bool check_ldso_bkpt);

  // Return true if dsos, including the main executable, have been loaded
  // into the inferior.
  bool DsosLoaded() { return dsos_ != nullptr; }

  // Return list of loaded dsos.
  // Returns nullptr if none loaded yet or loading failed.
  // TODO(dje): constness wip
  debugger_utils::dsoinfo_t* GetDsos() const { return dsos_; }

  // Return the DSO for |pc| or nullptr if none.
  // TODO(dje): Result is not const for debug file lookup support.
  debugger_utils::dsoinfo_t* LookupDso(zx_vaddr_t pc) const;

  // Return the entry for the main executable from the dsos list.
  // Returns nullptr if not present (could happen if inferior data structure
  // has been clobbered).
  const debugger_utils::dsoinfo_t* GetExecDso();

 private:
  Process() = default;

  // The exception handler invoked by ExceptionPort.
  void OnExceptionOrSignal(const zx_port_packet_t& packet,
                           const zx_exception_context_t& context);

  // Debug handle mgmt.
  bool AllocDebugHandle(process::ProcessBuilder* builder);
  bool AllocDebugHandle(zx_koid_t pid);
  void CloseDebugHandle();

  // Exception port mgmt.
  bool BindExceptionPort();
  void UnbindExceptionPort();

  // Detach from the inferior, but don't clear out any data structures.
  void RawDetach();

  // Release all resources held by the process.
  // Called after all other processing of a process exit has been done.
  void Clear();

  // The server that owns us.
  Server* server_;  // weak

  // The delegate that we send life-cycle notifications to.
  Delegate* delegate_;  // weak

  // The argv that this process was initialized with.
  debugger_utils::Argv argv_;

  // Extra handles to pass to process during creation.
  std::vector<fuchsia::process::HandleInfo> extra_handles_;

  // The process::ProcessBuilder instance used to bootstrap and run the process.
  std::unique_ptr<process::ProcessBuilder> builder_;

  // The debug-capable handle that we use to invoke zx_debug_* syscalls.
  zx_handle_t handle_ = ZX_HANDLE_INVALID;

  // The current state of this process.
  State state_ = State::kNew;

  // The process ID (also the kernel object ID).
  zx_koid_t id_ = ZX_KOID_INVALID;

  // The base load address of the dynamic linker.
  zx_vaddr_t base_address_ = 0;

  // The entry point of the dynamic linker.
  zx_vaddr_t entry_address_ = 0;

  // The key we receive after binding an exception port.
  ExceptionPort::Key eport_key_ = 0;

  // True if we attached, or will attach, to a running program.
  // Otherwise we're launching a program from scratch.
  bool attached_running_ = false;

  // The API to access memory.
  std::shared_ptr<debugger_utils::ByteBlock> memory_;

  // The collection of breakpoints that belong to this process.
  ProcessBreakpointSet breakpoints_;

  // The threads owned by this process. This is map is populated lazily when
  // threads are requested through FindThreadById(). It can also be repopulated
  // from scratch, e.g., when attaching to an already running program.
  using ThreadMap = std::unordered_map<zx_koid_t, std::unique_ptr<Thread>>;
  ThreadMap threads_;

  // If true then |threads_| needs to be recalculated from scratch.
  bool thread_map_stale_ = false;

  // List of dsos loaded.
  // NULL if none have been loaded yet (including main executable).
  // TODO(dje): Code taking from crashlogger, to be rewritten.
  // TODO(dje): Doesn't include dsos loaded later.
  debugger_utils::dsoinfo_t* dsos_ = nullptr;

  // If true then building the dso list failed, don't try again.
  bool dsos_build_failed_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(Process);
};

}  // namespace inferior_control
