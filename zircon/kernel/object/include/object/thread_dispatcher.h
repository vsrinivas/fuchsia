// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_THREAD_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_THREAD_DISPATCHER_H_

#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

#include <arch/exception.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_piece.h>
#include <kernel/dpc.h>
#include <kernel/event.h>
#include <kernel/owned_wait_queue.h>
#include <kernel/thread.h>
#include <object/channel_dispatcher.h>
#include <object/dispatcher.h>
#include <object/exception_dispatcher.h>
#include <object/exceptionate.h>
#include <object/excp_port.h>
#include <object/handle.h>
#include <object/thread_state.h>
#include <vm/vm_address_region.h>

class ProcessDispatcher;

class ThreadDispatcher final : public SoloDispatcher<ThreadDispatcher, ZX_DEFAULT_THREAD_RIGHTS> {
 public:
  // Traits to belong in the parent process's list.
  struct ThreadListTraits {
    static fbl::DoublyLinkedListNodeState<ThreadDispatcher*>& node_state(ThreadDispatcher& obj) {
      return obj.dll_thread_;
    }
  };

  // When in a blocking syscall, or blocked in an exception, the blocking reason.
  // There is one of these for each syscall marked "blocking".
  // See syscalls.abigen.
  enum class Blocked {
    // Not blocked.
    NONE,
    // The thread is blocked in an exception.
    EXCEPTION,
    // The thread is sleeping (zx_nanosleep).
    SLEEPING,
    // zx_futex_wait
    FUTEX,
    // zx_port_wait
    PORT,
    // zx_channel_call
    CHANNEL,
    // zx_object_wait_one
    WAIT_ONE,
    // zx_object_wait_many
    WAIT_MANY,
    // zx_interrupt_wait
    INTERRUPT,
    // pager
    PAGER,
  };

  // Entry state for a thread
  struct EntryState {
    uintptr_t pc = 0;
    uintptr_t sp = 0;
    uintptr_t arg1 = 0;
    uintptr_t arg2 = 0;
  };

  static zx_status_t Create(fbl::RefPtr<ProcessDispatcher> process, uint32_t flags,
                            fbl::StringPiece name, KernelHandle<ThreadDispatcher>* out_handle,
                            zx_rights_t* out_rights);
  ~ThreadDispatcher();

  static ThreadDispatcher* GetCurrent() { return get_current_thread()->user_thread; }

  // Dispatcher implementation.
  zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_THREAD; }
  zx_koid_t get_related_koid() const final;

  // Performs initialization on a newly constructed ThreadDispatcher
  // If this fails, then the object is invalid and should be deleted
  zx_status_t Initialize();
  // Start this thread running inside the parent process with the provided entry state, only
  // valid to be called on a thread in the INITIALIZED state that has not yet been started.
  zx_status_t Start(const EntryState& entry, bool initial_thread);
  // Transitions a thread from the INITIALIZED state to either the RUNNING or SUSPENDED state.
  // Is the caller's responsibility to ensure this thread is registered with the parent process,
  // as such this is only expected to be called from the ProcessDispatcher.
  zx_status_t MakeRunnable(const EntryState& entry, bool suspended);
  void Exit() __NO_RETURN;
  void Kill();

  // Suspends the thread.
  // Returns ZX_OK on success, or ZX_ERR_BAD_STATE iff the thread is dying or dead.
  zx_status_t Suspend();
  void Resume();

  // accessors
  ProcessDispatcher* process() const { return process_.get(); }

  // Returns true if the thread is dying or dead. Threads never return to a previous state
  // from dying/dead so once this is true it will never flip back to false.
  bool IsDyingOrDead() const;

  zx_status_t set_name(const char* name, size_t len) final __NONNULL((2));
  void get_name(char out_name[ZX_MAX_NAME_LEN]) const final __NONNULL((2));
  uint64_t runtime_ns() const { return thread_runtime(core_thread_); }
  cpu_num_t last_cpu() const { return thread_last_cpu(core_thread_); }

  zx_status_t SetExceptionPort(fbl::RefPtr<ExceptionPort> eport);
  // Returns true if a port had been set.
  bool ResetExceptionPort();
  fbl::RefPtr<ExceptionPort> exception_port();

  // Send a report to the associated exception handler of |eport| and wait
  // for a response.
  // Note this takes a specific exception port as an argument because there are several:
  // debugger, thread, process, and system. The kind of the exception port is
  // specified by |eport->type()|.
  // Returns:
  // ZX_OK: the exception was handled in some way, and |*out_estatus|
  // specifies how.
  // ZX_ERR_INTERNAL_INTR_KILLED: the thread was killed (probably via zx_task_kill)
  zx_status_t ExceptionHandlerExchange(fbl::RefPtr<ExceptionPort> eport,
                                       const zx_exception_report_t* report,
                                       const arch_exception_context_t* arch_context,
                                       ThreadState::Exception* out_estatus);

  // Record entry/exit to being in an exception.
  void EnterException(fbl::RefPtr<ExceptionPort> eport, const zx_exception_report_t* report,
                      const arch_exception_context_t* arch_context);
  void ExitExceptionLocked() TA_REQ(get_lock());

  // Called when an exception handler is finished processing the exception.
  // If |eport| is non-nullptr, then the exception is only continued if
  // |eport| corresponds to the current exception port.
  zx_status_t MarkExceptionHandled(PortDispatcher* eport);
  zx_status_t MarkExceptionNotHandled(PortDispatcher* eport);

  // Called when exception port |eport| is removed.
  // If the thread is waiting for the associated exception handler, continue
  // exception processing as if the exception port had not been installed.
  void OnExceptionPortRemoval(const fbl::RefPtr<ExceptionPort>& eport);

  // Assuming the thread is stopped waiting for an exception response,
  // fill in |*report| with the exception report.
  // Returns ZX_ERR_BAD_STATE if not in an exception.
  zx_status_t GetExceptionReport(zx_exception_report_t* report);

  // TODO(ZX-3072): remove the port-based exception code once everyone is
  // switched over to channels.
  Exceptionate* exceptionate();

  // Sends an exception over the exception channel and blocks for a response.
  //
  // |sent| will indicate whether the exception was successfully sent over
  // the given |exceptionate| channel. This can be used in the ZX_ERR_NEXT
  // case to determine whether the exception channel didn't exist or it did
  // exist but the receiver opted not to handle the exception.
  //
  // Returns:
  //   ZX_OK if the exception was processed and the thread should resume.
  //   ZX_ERR_NEXT if there is no channel or the receiver opted to skip.
  //   ZX_ERR_NO_MEMORY on allocation failure.
  //   ZX_ERR_INTERNAL_INTR_KILLED if the thread was killed before
  //       receiving a response.
  zx_status_t HandleException(Exceptionate* exceptionate,
                              fbl::RefPtr<ExceptionDispatcher> exception, bool* sent);

  // Similar to HandleException(), but for single-shot exceptions which are
  // sent to at most one handler, e.g. ZX_EXCP_THREAD_STARTING.
  //
  // The main difference is that this takes |exception_type| and |context|
  // rather than a full exception object, and internally sets up the required
  // state and creates the exception object.
  //
  // Returns true if the exception was sent.
  bool HandleSingleShotException(Exceptionate* exceptionate, zx_excp_type_t exception_type,
                                 const arch_exception_context_t& context);

  // Fetch the state of the thread for userspace tools.
  zx_status_t GetInfoForUserspace(zx_info_thread_t* info);

  // Fetch per thread stats for userspace.
  zx_status_t GetStatsForUserspace(zx_info_thread_stats_t* info);

  // For debugger usage.
  zx_status_t ReadState(zx_thread_state_topic_t state_kind, user_out_ptr<void> buffer,
                        size_t buffer_size);
  zx_status_t WriteState(zx_thread_state_topic_t state_kind, user_in_ptr<const void> buffer,
                         size_t buffer_size);

  // Profile support
  zx_status_t SetPriority(int32_t priority) TA_EXCL(get_lock());
  zx_status_t SetDeadline(const zx_sched_deadline_params_t& params) TA_EXCL(get_lock());
  zx_status_t SetSoftAffinity(cpu_mask_t mask) TA_EXCL(get_lock());

  // For ChannelDispatcher use.
  ChannelDispatcher::MessageWaiter* GetMessageWaiter() { return &channel_waiter_; }

  // Blocking syscalls, once they commit to a path that will likely block the
  // thread, use this helper class to properly set/restore |blocked_reason_|.
  class AutoBlocked final {
   public:
    explicit AutoBlocked(Blocked reason)
        : thread_(ThreadDispatcher::GetCurrent()), prev_reason(thread_->blocked_reason_) {
      DEBUG_ASSERT(reason != Blocked::NONE);
      thread_->blocked_reason_ = reason;
    }
    ~AutoBlocked() { thread_->blocked_reason_ = prev_reason; }

   private:
    ThreadDispatcher* const thread_;
    const Blocked prev_reason;
  };

  // callback from kernel when thread is exiting, just before it stops for good.
  void Exiting();

  // callback from kernel when thread is suspending
  void Suspending();
  // callback from kernel when thread is resuming
  void Resuming();

 private:
  ThreadDispatcher(fbl::RefPtr<ProcessDispatcher> process, thread_t* core_thread, uint32_t flags);
  ThreadDispatcher(const ThreadDispatcher&) = delete;
  ThreadDispatcher& operator=(const ThreadDispatcher&) = delete;

  // friend FutexContext so that it can manipulate the blocking_futex_id_ member of
  // ThreadDispatcher, and so that it can access the "thread_" member of the class so that
  // wait_queue opertations can be performed on ThreadDispatchers
  friend class FutexContext;

  // kernel level entry point
  static int StartRoutine(void* arg);

  // Return true if waiting for an exception response.
  bool InPortExceptionLocked() TA_REQ(get_lock());
  bool InChannelExceptionLocked() TA_REQ(get_lock());

  // Returns true if the thread is suspended or processing an exception.
  bool SuspendedOrInExceptionLocked() TA_REQ(get_lock());

  // Helper routine to minimize code duplication.
  zx_status_t MarkExceptionHandledWorker(PortDispatcher* eport,
                                         ThreadState::Exception handled_state);
  // change states of the object, do what is appropriate for the state transition
  void SetStateLocked(ThreadState::Lifecycle lifecycle) TA_REQ(get_lock());

  bool IsDyingOrDeadLocked() const TA_REQ(get_lock());

  template <typename T, typename F>
  zx_status_t ReadStateGeneric(F get_state_func, thread_t* thread, user_out_ptr<void> buffer,
                               size_t buffer_size);
  template <typename T, typename F>
  zx_status_t WriteStateGeneric(F set_state_func, thread_t* thread, user_in_ptr<const void> buffer,
                                size_t buffer_size);

  // The containing process holds a list of all its threads.
  fbl::DoublyLinkedListNodeState<ThreadDispatcher*> dll_thread_;
  // a ref pointer back to the parent process.
  const fbl::RefPtr<ProcessDispatcher> process_;

  // The thread as understood by the lower kernel.
  thread_t* const core_thread_;

  // User thread starting register values.
  EntryState user_entry_;

  ThreadState state_ TA_GUARDED(get_lock());

  // This is only valid while |state_.is_running()|.
  // This is just a volatile, and not something like an atomic, because
  // the only writer is the thread itself, and readers can just pick up
  // whatever value is currently here. This value is written when the thread
  // is likely to be put on a wait queue, and the following context switch
  // will force this value's visibility to other cpus. If the thread doesn't
  // get put on a wait queue, the thread was never really blocked.
  volatile Blocked blocked_reason_ = Blocked::NONE;

  // Thread-level exception handler.
  // Exceptionates have internal locking so we don't need to guard it here.
  Exceptionate exceptionate_;
  fbl::RefPtr<ExceptionPort> exception_port_ TA_GUARDED(get_lock());

  // Support for sending an exception to an exception handler and then waiting for a response.

  // The exception port of the handler the thread is waiting for a response from.
  fbl::RefPtr<ExceptionPort> exception_wait_port_ TA_GUARDED(get_lock());
  const zx_exception_report_t* exception_report_ TA_GUARDED(get_lock());
  event_t exception_event_ = EVENT_INITIAL_VALUE(exception_event_, false, EVENT_FLAG_AUTOUNSIGNAL);

  // Non-null if the thread is currently processing a channel exception.
  fbl::RefPtr<ExceptionDispatcher> exception_ TA_GUARDED(get_lock());

  // Some glue to temporarily bridge state between channel-based and
  // port-based exception handling until we remove ports.
  ExceptionPort::Type channel_exception_wait_type_ TA_GUARDED(get_lock()) =
      ExceptionPort::Type::NONE;

  // cleanup dpc structure
  dpc_t cleanup_dpc_ = {LIST_INITIAL_CLEARED_VALUE, nullptr, nullptr};

  // Tracks the number of times Suspend() has been called. Resume() will resume this thread
  // only when this reference count reaches 0.
  int suspend_count_ TA_GUARDED(get_lock()) = 0;

  // Used to protect thread name read/writes
  mutable DECLARE_SPINLOCK(ThreadDispatcher) name_lock_;

  // Per-thread structure used while waiting in a ChannelDispatcher::Call.
  // Needed to support the requirements of being able to interrupt a Call
  // in order to suspend a thread.
  ChannelDispatcher::MessageWaiter channel_waiter_;

  // If true and ancestor job has a debugger attached, thread will block on
  // start and will send a process start exception.
  bool is_initial_thread_ = false;

  // The ID of the futex we are currently waiting on, or 0 if we are not
  // waiting on any futex at the moment.
  //
  // TODO(johngro): figure out some way to apply clang static thread analysis
  // to this.  Right now, there is no good (cost free) way for the compiler to
  // figure out that this thread belongs to a specific process/futex-context,
  // and therefor the thread's futex-context lock can be used to guard this
  // futex ID.
  uintptr_t blocking_futex_id_ = 0;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_THREAD_DISPATCHER_H_
