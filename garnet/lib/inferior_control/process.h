// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_INFERIOR_CONTROL_PROCESS_H_
#define GARNET_LIB_INFERIOR_CONTROL_PROCESS_H_

#include <memory>
#include <string>
#include <unordered_map>

#include <lib/fit/function.h>
#include <src/lib/fxl/macros.h>
#include <lib/zx/process.h>
#include <lib/zx/suspend_token.h>
#include <lib/zx/time.h>
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

#include "garnet/lib/debugger_utils/dso_list.h"
#include "garnet/lib/debugger_utils/processes.h"
#include "garnet/lib/debugger_utils/util.h"
#include "garnet/lib/process/process_builder.h"

#include "breakpoint.h"
#include "delegate.h"
#include "exception_port.h"
#include "memory_process.h"
#include "thread.h"

namespace inferior_control {

class Server;
class Thread;

// Represents an inferior process that we're attached to.
class Process final {
 public:
  enum class State { kNew, kStarting, kRunning, kGone };

  using StartCallback = fit::function<zx_status_t(Process*)>;

  // This value is used as the return code if something prevents us from
  // obtaining it from the process.
  static constexpr int kDefaultFailureReturnCode = -1;

  static const char* StateName(Process::State state);

  explicit Process(Server* server, Delegate* delegate_);
  ~Process();

  std::string GetName() const;

  // Returns the current state of this process.
  State state() const { return state_; }

  // Change the state to |new_state|.
  void set_state(State new_state);

  int return_code() const { return return_code_; }
  bool return_code_is_set() const { return return_code_is_set_; }

  // Initialize a new inferior process that was built using |ProcessBuilder|.
  // Returns false if there is an error.
  // Do not call this if the process is currently live (state is kStarting or
  // kRunning).
  bool InitializeFromBuilder(std::unique_ptr<process::ProcessBuilder> builder);

  // Attach to newly created process |process|.
  // |start_callback| is called by |Start()| to start execution of the process.
  bool AttachToNew(zx::process process, StartCallback start_callback);

  // Attach to running program |process|.
  // Returns false if there is an error.
  // Do not call this if the process is currently live (state is kStarting or
  // kRunning).
  bool AttachToRunning(zx::process process);

  // Detach from an attached process, and return to pre-attached state.
  // This includes unbinding from the exception port and closing the process
  // handle. To keep things simple and clean "detach" means "release all
  // connections with the inferior". After detaching we should have absolutely
  // no effect on the inferior, including not preserving the lifetime of the
  // kernel process instance because we still have a handle of the process.
  // Returns true on success, or false if already detached.
  // N.B. It is the caller's responsibility to have first removed any and all
  // breakpoints. This does not include the ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET
  // ld.so breakpoint which is automagically set if we launched the inferior
  // and is managed internally. Furthermore, if the ld.so breakpoint
  // hasn't been hit yet, which can be determined by calling
  // |ldso_debug_data_has_initialized()|, then this must be called while the
  // inferior is stopped. Typically this happens when processing the
  // THREAD_STARTING exception for the initial thread.
  bool Detach();

  // Starts running the process. Returns false in case of an error.
  // |AttachToNew()| MUST be called successfully before calling Start().
  // |InitializeFromBuilder| does this implicitly.
  bool Start();

  // Terminate the process.
  // This doesn't wait for the process to die. The server loop will get a
  // ZX_PROCESS_TERMINATED signal when that happens.
  bool Kill();

  // Request all threads in the process to suspend.
  // This doesn't wait for them to suspend, just requests it.
  // It is up to the app's server loop to wait for threads to suspend if
  // it wants to.
  bool RequestSuspend();

  // Resume the process after having been suspended.
  void ResumeFromSuspension();

  // Returns true if the process is running or has been running.
  bool IsLive() const;

  // Returns true if the process is currently attached.
  bool IsAttached() const;

  // Returns the process handle. This handle is owned and managed by this
  // Process instance, thus the caller should not close the handle.
  const zx::process& process() const { return process_; }

  // Returns the process ID.
  zx_koid_t id() const { return id_; }

  Server* server() { return server_; }

  Delegate* delegate() const { return delegate_; }

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
  // This may not be called while detached.
  void EnsureThreadMapFresh();

  // Iterates through all cached threads and invokes |callback| for each of
  // them. |callback| is guaranteed to get called only before ForEachThread()
  // returns, so it is safe to bind local variables to |callback|.
  using ThreadCallback = fit::function<void(Thread*)>;
  void ForEachThread(const ThreadCallback& callback);
  // Same as ForEachThread except ignores State::kGone threads.
  void ForEachLiveThread(const ThreadCallback& callback);

  // Reads the block of memory of length |length| bytes starting at address
  // |address| into |out_buffer|. |out_buffer| must be at least as large as
  // |length|. Returns true on success or false on failure.
  bool ReadMemory(uintptr_t address, void* out_buffer, size_t length);

  // Writes the block of memory of length |length| bytes from |data| to the
  // memory address |address| of this process. Returns true on success or false
  // on failure.
  bool WriteMemory(uintptr_t address, const void* data, size_t length);

  bool attached_running() const { return attached_running_; }

  // See if the list of loaded dsos has been built, and if not build it.
  // This is called when |thread| is stopped at s/w breakpoints (and thus
  // potentially dynamic linker breakpoints).
  // Returns true if the thread was stopped at a dynamic linker breakpoint,
  // and thus the caller should immediately resume the thread.
  bool CheckDsosList(Thread* thread);

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

  // Called when ZX_PROCESS_TERMINATED is received, update our internal state.
  void OnTermination();

  // Print an Inspector-style dump of each thread.
  // Threads that are not currently in an exception or suspended are ignored.
  // It is the caller's responsibility to stop desired threads first (and wait
  // for them to stop).
  void Dump();

  zx_vaddr_t debug_addr_property() const { return debug_addr_property_; }

  bool ldso_debug_data_has_initialized() const {
    return ldso_debug_data_has_initialized_;
  }

  zx_vaddr_t ldso_debug_break_addr() const { return ldso_debug_break_addr_; }

  zx_vaddr_t ldso_debug_map_addr() const { return ldso_debug_map_addr_; }

 private:
  // When refreshing the thread list, new threads could be created.
  // Add this to the number of existing threads to account for new ones.
  // The number is large but the cost is only 8 bytes per extra thread for
  // the thread's koid.
  static constexpr size_t kNumExtraRefreshThreads = 20;

  // When refreshing the thread list, if threads are being created faster than
  // we can keep up, keep looking, but don't keep trying forever.
  static constexpr size_t kRefreshThreadsTryCount = 4;

  // Refreshes the complete Thread list for this process. Returns false if an
  // error is returned from a syscall. Any threads that were accumulated up to
  // that point are retained.
  // Pointers to existing threads are maintained.
  void RefreshAllThreads();

  // Wrapper on |zx_object_get_property()| to fetch the value of
  // ZX_PROP_PROCESS_DEBUG_ADDR.
  zx_vaddr_t GetDebugAddrProperty();

  // Wrapper on |zx_object_set_property()| to set the value of
  // ZX_PROP_PROCESS_DEBUG_ADDR.
  void SetDebugAddrProperty(zx_vaddr_t debug_addr);

  // Cause ld.so to execute a s/w breakpoint instruction after all dsos have
  // been loaded at startup.
  void SetLdsoDebugTrigger();

  // Fetch the value of ZX_PROP_PROCESS_DEBUG_ADDR.
  // Returns true the value if it has been set (by the dynamic linker) or
  // zero if it has not been set yet (or there's an error).
  // The value is cached in |debug_addr_|.
  zx_vaddr_t GetDebugAddr();

  // Assuming the inferior is stopped at a s/w breakpoint, check if it's
  // stopped at the ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET breakpoint.
  // If so, update |ldso_debug_break_addr_| and
  // |seen_ldso_debug_addr_break_| and return true.
  // Otherwise return false.
  bool CheckLdsoDebugAddrBreak();

  // Try to build the list of loaded dsos.
  // This must be called at a point where it is safe to read the list.
  void TryBuildLoadedDsosList(Thread* thread);

  // The exception handler invoked by ExceptionPort.
  // TODO(dje): Friend is temporary, pending completion of the refactor moving
  // the exception/signal handler to |Server|.
  friend class Server;
  void OnExceptionOrSignal(const zx_port_packet_t& packet);

  // Exception port mgmt.
  bool BindExceptionPort();
  void UnbindExceptionPort();

  // Helper routine to implement |AttachToNew(),AttachToRunning()|.
  bool AttachWorker(zx::process process, bool attach_running);

  // Detach from the inferior, but don't clear out any data structures.
  void RawDetach();

  // Release all resources held by the process.
  // Called after all other processing of a process exit has been done.
  // This does not clear |id_|, that is still retrievable after the process
  // has terminated.
  void Clear();

  // Record new thread |thread_handle,thread_id|.
  Thread* AddThread(zx_handle_t thread_handle, zx_koid_t thread_id);

  // Record the process's return code.
  void RecordReturnCode();

  // The server that owns us (non-owning).
  Server* server_;

  // The delegate that we send life-cycle notifications to (non-owning).
  Delegate* delegate_;

  // The process::ProcessBuilder instance used to create and run the process.
  std::unique_ptr<process::ProcessBuilder> builder_;

  // The debug-capable handle that we use to invoke zx_debug_* syscalls.
  zx::process process_;

  // The current state of this process.
  State state_ = State::kNew;

  // The process ID (also the kernel object ID).
  zx_koid_t id_ = ZX_KOID_INVALID;

  // The value of ZX_PROP_PROCESS_DEBUG_ADDR or zero if not known yet.
  // The value is never legitimately zero, except if we attached to a running
  // program prior to ld.so reaching its debug breakpoint on startup.
  zx_vaddr_t debug_addr_property_ = 0;

  // True if ld.so's debug data structures are initialized.
  bool ldso_debug_data_has_initialized_ = false;

  // The address of the "standard" dynamic linker breakpoint.
  // I.e., the contents of |r_debug.r_brk|.
  // Zero if not known yet.
  zx_vaddr_t ldso_debug_break_addr_ = 0;

  // The address of the dynamic linker's list of loaded shared libraries.
  // I.e., the contents of |r_debug.r_map|.
  // Zero if not known yet.
  zx_vaddr_t ldso_debug_map_addr_ = 0;

  // The base load address of the dynamic linker.
  zx_vaddr_t base_address_ = 0;

  // The entry point of the dynamic linker.
  zx_vaddr_t entry_address_ = 0;

  // True if the debugging exception port has been bound.
  bool eport_bound_ = false;

  // True if we attached, or will attach, to a running program.
  // Otherwise we're launching a program from scratch.
  bool attached_running_ = false;

  // This callback is invoked by |Start()|.
  StartCallback start_callback_;

  // Suspend token when entire process is suspended.
  zx::suspend_token suspend_token_;

  // The API to access memory.
  std::shared_ptr<debugger_utils::ByteBlock> memory_;

  // The collection of breakpoints that belong to this process.
  ProcessBreakpointSet breakpoints_;

  // The threads owned by this process. This map is populated lazily when
  // threads are requested through FindThreadById(). It can also be repopulated
  // from scratch, e.g., when attaching to an already running program.
  using ThreadMap = std::unordered_map<zx_koid_t, std::unique_ptr<Thread>>;
  ThreadMap threads_;

  // If true then |threads_| needs to be recalculated.
  bool thread_map_stale_ = false;

  // List of dsos loaded.
  // NULL if none have been loaded yet (including main executable).
  // TODO(dje): Code taking from crashlogger, to be rewritten.
  // TODO(dje): Doesn't include dsos loaded later.
  debugger_utils::dsoinfo_t* dsos_ = nullptr;

  // If true then building the dso list failed, don't try again.
  bool dsos_build_failed_ = false;

  // Processes are detached from when they exit.
  // Save the return code for later testing.
  int return_code_ = kDefaultFailureReturnCode;
  bool return_code_is_set_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(Process);
};

}  // namespace inferior_control

#endif  // GARNET_LIB_INFERIOR_CONTROL_PROCESS_H_
