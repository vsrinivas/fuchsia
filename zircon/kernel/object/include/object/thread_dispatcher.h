// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_THREAD_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_THREAD_DISPATCHER_H_

#include <platform.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

#include <arch/exception.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <kernel/event.h>
#include <kernel/owned_wait_queue.h>
#include <kernel/thread.h>
#include <ktl/atomic.h>
#include <ktl/string_view.h>
#include <object/channel_dispatcher.h>
#include <object/dispatcher.h>
#include <object/exceptionate.h>
#include <object/futex_context.h>
#include <object/handle.h>
#include <object/thread_state.h>
#include <vm/vm_address_region.h>

class ProcessDispatcher;

class ThreadDispatcher final : public SoloDispatcher<ThreadDispatcher, ZX_DEFAULT_THREAD_RIGHTS>,
                               public fbl::DoublyLinkedListable<ThreadDispatcher*> {
 public:
  // When in a blocking syscall, or blocked in an exception, the blocking reason.
  // There is one of these for each syscall marked "blocking".
  // See //zircon/vdso.
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
                            ktl::string_view name, KernelHandle<ThreadDispatcher>* out_handle,
                            zx_rights_t* out_rights);
  ~ThreadDispatcher();

  static ThreadDispatcher* GetCurrent() { return Thread::Current::Get()->user_thread(); }

  // Terminates the current thread. Does not return.
  static void ExitCurrent() __NO_RETURN { Thread::Current::Exit(0); }
  // Marks the current thread for termination. The thread will actually termiante when
  // the kernel stack unwinds.
  static void KillCurrent() { Thread::Current::Kill(); }

  // Dispatcher implementation.
  zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_THREAD; }
  zx_koid_t get_related_koid() const final;

  // Sets whether or not this is the initial thread in its process.
  // Should only be called by ProcessDispatcher upon adding the initialized thread.
  void set_is_initial_thread(bool is_initial_thread) { is_initial_thread_ = is_initial_thread; }

  // Performs initialization on a newly constructed ThreadDispatcher
  // If this fails, then the object is invalid and should be deleted
  zx_status_t Initialize() TA_EXCL(get_lock());
  // Start this thread running inside the parent process with the provided entry state, only
  // valid to be called on a thread in the INITIALIZED state that has not yet been started. If
  // `ensure_initial_thread` is true, the thread will only start if it is the first thread in the
  // process.
  zx_status_t Start(const EntryState& entry, bool ensure_initial_thread);
  // Transitions a thread from the INITIALIZED state to either the RUNNING or SUSPENDED state.
  // Is the caller's responsibility to ensure this thread is registered with the parent process,
  // as such this is only expected to be called from the ProcessDispatcher.
  zx_status_t MakeRunnable(const EntryState& entry, bool suspended);
  void Kill();

  // Suspends the thread.
  // Returns ZX_OK on success, or ZX_ERR_BAD_STATE iff the thread is dying or dead.
  zx_status_t Suspend();
  void Resume();

  // accessors
  ProcessDispatcher* process() const { return process_.get(); }

  // Returns true if the thread is dying or dead. Threads never return to a previous state
  // from dying/dead so once this is true it will never flip back to false.
  bool IsDyingOrDead() const TA_EXCL(get_lock());

  // Returns true if the thread was ever started (even if it is dead now).
  // Threads never return to an INITIAL state after starting, so once this is
  // true it will never flip back to false.
  bool HasStarted() const TA_EXCL(get_lock());

  zx_status_t set_name(const char* name, size_t len) final __NONNULL((2)) TA_EXCL(get_lock());
  void get_name(char (&out_name)[ZX_MAX_NAME_LEN]) const final TA_EXCL(get_lock());

  // Assuming the thread is stopped waiting for an exception response,
  // fill in |*report| with the exception report.
  // Returns ZX_ERR_BAD_STATE if not in an exception.
  zx_status_t GetExceptionReport(zx_exception_report_t* report);

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
                                 const arch_exception_context_t& context) TA_EXCL(get_lock());

  // Fetch the state of the thread for userspace tools.
  zx_status_t GetInfoForUserspace(zx_info_thread_t* info);

  // Fetch per thread stats for userspace.
  zx_status_t GetStatsForUserspace(zx_info_thread_stats_t* info) TA_EXCL(get_lock());

  // Fetch a consistent snapshot of the runtime stats.
  zx_status_t GetRuntimeStats(TaskRuntimeStats* out) const;

  // Aggregate the runtime stats for this thread into the given struct.
  zx_status_t AccumulateRuntimeTo(zx_info_task_runtime_t* info) const {
    TaskRuntimeStats out;
    zx_status_t err = GetRuntimeStats(&out);
    if (err != ZX_OK) {
      return err;
    }

    out.AccumulateRuntimeTo(info);
    return ZX_OK;
  }

  // For debugger usage.
  zx_status_t ReadState(zx_thread_state_topic_t state_kind, user_out_ptr<void> buffer,
                        size_t buffer_size) TA_EXCL(get_lock());
  zx_status_t WriteState(zx_thread_state_topic_t state_kind, user_in_ptr<const void> buffer,
                         size_t buffer_size) TA_EXCL(get_lock());

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
        : thread_(ThreadDispatcher::GetCurrent()),
          prev_reason(thread_->blocked_reason_.load(ktl::memory_order_acquire)) {
      DEBUG_ASSERT(reason != Blocked::NONE);
      thread_->blocked_reason_.store(reason, ktl::memory_order_release);
    }
    ~AutoBlocked() { thread_->blocked_reason_.store(prev_reason, ktl::memory_order_release); }

   private:
    ThreadDispatcher* const thread_;
    const Blocked prev_reason;
  };

  // This is called from Thread as it is exiting, just before it stops for good.
  // It is an error to call this on anything other than the current thread.
  void ExitingCurrent();

  // callback from kernel when thread is suspending
  void Suspending();
  // callback from kernel when thread is resuming
  void Resuming();

  // Provide an update to this thread's scheduler-related runtime stats.
  //
  // WARNING: This method must not be called concurrently by two separate threads.
  // For now, this method is protected by the thread_lock, but in the future this may change.
  void UpdateSchedulerStats(const Thread::RuntimeStats::SchedulerStats& update)
      TA_REQ(thread_lock) {
    uint64_t before = stats_generation_count_.fetch_add(1, ktl::memory_order_acq_rel);
    runtime_stats_.UpdateSchedulerStats(update);
    uint64_t after = stats_generation_count_.fetch_add(1, ktl::memory_order_acq_rel);
    // Ensure no concurrent write was happening at the start and that no concurrent writes happened
    // during this operation.
    DEBUG_ASSERT((before % 2) == 0);
    DEBUG_ASSERT(after == before + 1);
  }

  // Update time spent handling page faults.
  // Safe for concurrent use.
  void AddPageFaultTicks(zx_ticks_t ticks) { runtime_stats_.AddPageFaultTicks(ticks); }

  // Update time spent contended on locks.
  // Safe for concurrent use.
  void AddLockContentionTicks(zx_ticks_t ticks) { runtime_stats_.AddLockContentionTicks(ticks); }

 private:
  ThreadDispatcher(fbl::RefPtr<ProcessDispatcher> process, uint32_t flags);
  ThreadDispatcher(const ThreadDispatcher&) = delete;
  ThreadDispatcher& operator=(const ThreadDispatcher&) = delete;

  // friend FutexContext so that it can manipulate the blocking_futex_id_ member of
  // ThreadDispatcher, and so that it can access the "thread_" member of the class so that
  // wait_queue operations can be performed on ThreadDispatchers
  friend class FutexContext;

  // kernel level entry point
  static int StartRoutine(void* arg);

  // Return true if waiting for an exception response.
  bool InExceptionLocked() TA_REQ(get_lock());

  // Returns true if the thread is suspended or processing an exception.
  bool SuspendedOrInExceptionLocked() TA_REQ(get_lock());

  // change states of the object, do what is appropriate for the state transition
  void SetStateLocked(ThreadState::Lifecycle lifecycle) TA_REQ(get_lock());

  bool IsDyingOrDeadLocked() const TA_REQ(get_lock());

  bool HasStartedLocked() const TA_REQ(get_lock());

  template <typename T, typename F>
  zx_status_t ReadStateGeneric(F get_state_func, user_out_ptr<void> buffer, size_t buffer_size)
      TA_EXCL(get_lock());
  template <typename T, typename F>
  zx_status_t WriteStateGeneric(F set_state_func, user_in_ptr<const void> buffer,
                                size_t buffer_size) TA_EXCL(get_lock());

  // a ref pointer back to the parent process.
  const fbl::RefPtr<ProcessDispatcher> process_;

  // The thread as understood by the lower kernel. This is set to nullptr when
  // `state_` transitions to DEAD.
  Thread* core_thread_ TA_GUARDED(get_lock()) = nullptr;

  // User thread starting register values.
  EntryState user_entry_;

  ThreadState state_ TA_GUARDED(get_lock());

  // This is only valid while |state_.lifecycle()| is RUNNING.
  //
  // This field is an atomic because it may be accessed concurrently by multiple
  // threads.  It may be read by any thread, but may only be updated by the
  // "this" thread.
  //
  // In general, loads of this field should be performed with acquire semantics
  // and stores with release semantics because this field is used to synchronize
  // threads (think: wait for a thread to become blocked, then inspect some
  // state the thread has written).
  //
  // Because this is simply an atomic, readers must be OK with observing stale
  // values.  That is, by the time a reader can take action on the value, the
  // value may no longer be accurate.
  ktl::atomic<Blocked> blocked_reason_ = Blocked::NONE;

  // Support for sending an exception to an exception handler and then waiting for a response.
  // Exceptionates have internal locking so we don't need to guard it here.
  Exceptionate exceptionate_;

  // Non-null if the thread is currently processing a channel exception.
  fbl::RefPtr<ExceptionDispatcher> exception_ TA_GUARDED(get_lock());

  // Holds the type of the exceptionate currently processing the exception,
  // which may be our |exceptionate_| or one of our parents'.
  uint32_t exceptionate_type_ TA_GUARDED(get_lock()) = ZX_EXCEPTION_CHANNEL_TYPE_NONE;

  // Tracks the number of times Suspend() has been called. Resume() will resume this thread
  // only when this reference count reaches 0.
  int suspend_count_ TA_GUARDED(get_lock()) = 0;

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
  FutexId blocking_futex_id_{FutexId::Null()};

  // Generation counter protecting runtime stats.
  //
  // This count provides single-writer, multi-reader consistency on reads from the runtime_stats_
  // variable.
  //
  // Locking strategy:
  // - All writes are preceded by and followed by acq-rel atomic fetch-adds.
  // - All reads consist of:
  //   1) atomic read with acquire ordering of the generation count,
  //   2) copy stats out,
  //   3) atomic read with acquire ordering of the generation count,
  //   4) comparison of the two generation counts (must be even and match)
  // - Reads retry until a consistent snapshot can be taken.
  ktl::atomic<uint64_t> stats_generation_count_ = 0;
  // The runtime stats for this thread.
  Thread::RuntimeStats runtime_stats_ = {};
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_THREAD_DISPATCHER_H_
