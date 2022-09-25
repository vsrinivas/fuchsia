// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

/**
 * @file
 * @brief  Kernel threading
 *
 * This file is the core kernel threading interface.
 *
 * @defgroup thread Threads
 * @{
 */
#include "kernel/thread.h"

#include <assert.h>
#include <debug.h>
#include <inttypes.h>
#include <lib/arch/intrin.h>
#include <lib/counters.h>
#include <lib/fit/defer.h>
#include <lib/heap.h>
#include <lib/ktrace.h>
#include <lib/lazy_init/lazy_init.h>
#include <lib/version.h>
#include <lib/zircon-internal/macros.h>
#include <platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/listnode.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <arch/debugger.h>
#include <arch/exception.h>
#include <arch/interrupt.h>
#include <arch/ops.h>
#include <kernel/auto_preempt_disabler.h>
#include <kernel/cpu.h>
#include <kernel/dpc.h>
#include <kernel/lockdep.h>
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/scheduler.h>
#include <kernel/stats.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <kernel/timer.h>
#include <ktl/algorithm.h>
#include <ktl/atomic.h>
#include <lk/main.h>
#include <lockdep/lockdep.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <pretty/hexdump.h>
#include <vm/kstack.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>

#include <ktl/enforce.h>

#define LOCAL_TRACE 0

// kernel counters.
// The counters below never decrease.
//
// counts the number of Threads successfully created.
KCOUNTER(thread_create_count, "thread.create")
// counts the number of detached Threads that exited. Never decreases.
KCOUNTER(thread_detached_exit_count, "thread.detached_exit")
// counts the number of Threads joined. Never decreases.
KCOUNTER(thread_join_count, "thread.join")
// counts the number of calls to suspend() that succeeded.
KCOUNTER(thread_suspend_count, "thread.suspend")
// counts the number of calls to resume() that succeeded.
KCOUNTER(thread_resume_count, "thread.resume")
// counts the number of times a thread's timeslice extension was activated (see
// |PreemptionState::SetTimesliceExtension|).
KCOUNTER(thread_timeslice_extended, "thread.timeslice_extended")

// The global thread list. This is a lazy_init type, since initial thread code
// manipulates the list before global constructors are run. This is initialized by
// thread_init_early.
static lazy_init::LazyInit<Thread::List> thread_list;

Thread::MigrateList Thread::migrate_list_;

// master thread spinlock
MonitoredSpinLock thread_lock __CPU_ALIGN_EXCLUSIVE;

// The global preempt disabled token singleton
PreemptDisabledToken preempt_disabled_token;

const char* ToString(enum thread_state state) {
  switch (state) {
    case THREAD_INITIAL:
      return "initial";
    case THREAD_READY:
      return "ready";
    case THREAD_RUNNING:
      return "running";
    case THREAD_BLOCKED:
      return "blocked";
    case THREAD_BLOCKED_READ_LOCK:
      return "blocked read lock";
    case THREAD_SLEEPING:
      return "sleeping";
    case THREAD_SUSPENDED:
      return "suspended";
    case THREAD_DEATH:
      return "death";
    default:
      return "[unknown]";
  }
}

static void init_thread_lock_state(Thread* t) {
#if WITH_LOCK_DEP
  lockdep::SystemInitThreadLockState(&t->lock_state());
#endif
}

void WaitQueueCollection::ThreadState::Block(Interruptible interruptible, zx_status_t status) {
  blocked_status_ = status;
  interruptible_ = interruptible;
  Scheduler::Block();
  interruptible_ = Interruptible::No;
}

void WaitQueueCollection::ThreadState::UnblockIfInterruptible(Thread* thread, zx_status_t status) {
  if (interruptible_ == Interruptible::Yes) {
    WaitQueue::UnblockThread(thread, status);
  }
}

void WaitQueueCollection::ThreadState::Unsleep(Thread* thread, zx_status_t status) {
  blocked_status_ = status;
  Scheduler::Unblock(thread);
}

void WaitQueueCollection::ThreadState::UnsleepIfInterruptible(Thread* thread, zx_status_t status) {
  if (interruptible_ == Interruptible::Yes) {
    Unsleep(thread, status);
  }
}

void WaitQueueCollection::ThreadState::UpdatePriorityIfBlocked(Thread* thread, int priority,
                                                               PropagatePI propagate) {
  if (blocking_wait_queue_) {
    blocking_wait_queue_->PriorityChanged(thread, priority, propagate);
  }
}

WaitQueueCollection::ThreadState::~ThreadState() {
  DEBUG_ASSERT(blocking_wait_queue_ == nullptr);

  // owned_wait_queues_ is a fbl:: list of unmanaged pointers.  It will debug
  // assert if it is not empty when it destructs; we do not need to do so
  // here.
}

// Default constructor/destructor.
Thread::Thread() {}

Thread::~Thread() {
  // At this point, the thread must not be on the global thread list or migrate
  // list.
  DEBUG_ASSERT(!thread_list_node_.InContainer());
  DEBUG_ASSERT(!migrate_list_node_.InContainer());
}

void Thread::set_name(ktl::string_view name) {
  // |name| must fit in ZX_MAX_NAME_LEN bytes, minus 1 for the trailing NUL.
  name = name.substr(0, ZX_MAX_NAME_LEN - 1);
  memcpy(name_, name.data(), name.size());
  memset(name_ + name.size(), 0, ZX_MAX_NAME_LEN - name.size());
}

void construct_thread(Thread* t, const char* name) {
  // Placement new to trigger any special construction requirements of the
  // Thread structure.
  //
  // TODO(johngro): now that we have converted Thread over to C++, consider
  // switching to using C++ constructors/destructors and new/delete to handle
  // all of this instead of using construct_thread and free_thread_resources
  new (t) Thread();

  t->set_name(name);
  init_thread_lock_state(t);
}

void TaskState::Init(thread_start_routine entry, void* arg) {
  entry_ = entry;
  arg_ = arg;
}

zx_status_t TaskState::Join(zx_time_t deadline) {
  return retcode_wait_queue_.Block(deadline, Interruptible::No);
}

void TaskState::WakeJoiners(zx_status_t status) { retcode_wait_queue_.WakeAll(status); }

static void free_thread_resources(Thread* t) {
  // free the thread structure itself.  Manually trigger the struct's
  // destructor so that DEBUG_ASSERTs present in the owned_wait_queues member
  // get triggered.
  bool thread_needs_free = t->free_struct();
  t->~Thread();
  if (thread_needs_free) {
    free(t);
  }
}

void Thread::Trampoline() {
  // Release the incoming lock held across reschedule.
  Scheduler::LockHandoff();
  arch_enable_ints();

  Thread* ct = Thread::Current::Get();
  int ret = ct->task_state_.entry()(ct->task_state_.arg());
  Thread::Current::Exit(ret);
}

/**
 * @brief  Create a new thread
 *
 * This function creates a new thread.  The thread is initially suspended, so you
 * need to call thread_resume() to execute it.
 *
 * @param  t               If not nullptr, use the supplied Thread
 * @param  name            Name of thread
 * @param  entry           Entry point of thread
 * @param  arg             Arbitrary argument passed to entry(). It can be null.
 *                         in which case |user_thread| will be used.
 * @param  priority        Execution priority for the thread.
 * @param  alt_trampoline  If not nullptr, an alternate trampoline for the thread
 *                         to start on.
 *
 * Thread priority is an integer from 0 (lowest) to 31 (highest).  Some standard
 * priorities are defined in <kernel/thread.h>:
 *
 *  HIGHEST_PRIORITY
 *  DPC_PRIORITY
 *  HIGH_PRIORITY
 *  DEFAULT_PRIORITY
 *  LOW_PRIORITY
 *  IDLE_PRIORITY
 *  LOWEST_PRIORITY
 *
 * Stack size is set to DEFAULT_STACK_SIZE
 *
 * @return  Pointer to thread object, or nullptr on failure.
 */
Thread* Thread::CreateEtc(Thread* t, const char* name, thread_start_routine entry, void* arg,
                          int priority, thread_trampoline_routine alt_trampoline) {
  unsigned int flags = 0;

  if (!t) {
    t = static_cast<Thread*>(memalign(alignof(Thread), sizeof(Thread)));
    if (!t) {
      return nullptr;
    }
    flags |= THREAD_FLAG_FREE_STRUCT;
  }

  // assert that t is at least as aligned as the Thread is supposed to be
  DEBUG_ASSERT(IS_ALIGNED(t, alignof(Thread)));

  construct_thread(t, name);

  t->task_state_.Init(entry, arg);
  Scheduler::InitializeThread(t, priority);

  zx_status_t status = t->stack_.Init();
  if (status != ZX_OK) {
    free_thread_resources(t);
    return nullptr;
  }

  // save whether or not we need to free the thread struct and/or stack
  t->flags_ = flags;

  if (likely(alt_trampoline == nullptr)) {
    alt_trampoline = &Thread::Trampoline;
  }

  // set up the initial stack frame
  arch_thread_initialize(t, (vaddr_t)alt_trampoline);

  // add it to the global thread list
  {
    Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
    thread_list->push_front(t);
  }

  kcounter_add(thread_create_count, 1);
  return t;
}

Thread* Thread::Create(const char* name, thread_start_routine entry, void* arg, int priority) {
  return Thread::CreateEtc(nullptr, name, entry, arg, priority, nullptr);
}

/**
 * @brief  Make a suspended thread executable.
 *
 * This function is called to start a thread which has just been
 * created with thread_create() or which has been suspended with
 * thread_suspend(). It can not fail.
 */
void Thread::Resume() {
  canary_.Assert();

  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};

  if (state() == THREAD_DEATH) {
    // The thread is dead, resuming it is a no-op.
    return;
  }

  // Clear the suspend signal in case there is a pending suspend
  signals_.fetch_and(~THREAD_SIGNAL_SUSPEND, ktl::memory_order_relaxed);
  if (state() == THREAD_INITIAL || state() == THREAD_SUSPENDED) {
    // Wake up the new thread, putting it in a run queue on a cpu.
    Scheduler::Unblock(this);
  }

  kcounter_add(thread_resume_count, 1);
}

zx_status_t Thread::DetachAndResume() {
  zx_status_t status = Detach();
  if (status != ZX_OK) {
    return status;
  }
  Resume();
  return ZX_OK;
}

/**
 * @brief  Suspend an initialized/ready/running thread
 *
 * @return ZX_OK on success, ZX_ERR_BAD_STATE if the thread is dead
 */
zx_status_t Thread::Suspend() {
  canary_.Assert();
  DEBUG_ASSERT(!IsIdle());

  // Disable preemption to defer rescheduling until the end of this scope.
  AnnotatedAutoPreemptDisabler preempt_disable;
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};

  if (state() == THREAD_DEATH) {
    return ZX_ERR_BAD_STATE;
  }

  signals_.fetch_or(THREAD_SIGNAL_SUSPEND, ktl::memory_order_relaxed);

  switch (state()) {
    case THREAD_DEATH:
      // This should be unreachable because this state was handled above.
      panic("Unexpected thread state");
    case THREAD_INITIAL:
      // Thread hasn't been started yet, add it to the run queue to transition
      // properly through the INITIAL -> READY state machine first, then it
      // will see the signal and go to SUSPEND before running user code.
      //
      // Though the state here is still INITIAL, the higher-level code has
      // already executed ThreadDispatcher::Start() so all the userspace
      // entry data has been initialized and will be ready to go as soon as
      // the thread is unsuspended.
      Scheduler::Unblock(this);
      break;
    case THREAD_READY:
      // thread is ready to run and not blocked or suspended.
      // will wake up and deal with the signal soon.
      break;
    case THREAD_RUNNING:
      // thread is running (on another cpu)
      // The following call is not essential.  It just makes the
      // thread suspension happen sooner rather than at the next
      // timer interrupt or syscall.
      mp_interrupt(MP_IPI_TARGET_MASK, cpu_num_to_mask(scheduler_state_.curr_cpu_));
      break;
    case THREAD_SUSPENDED:
      // thread is suspended already
      break;
    case THREAD_BLOCKED:
    case THREAD_BLOCKED_READ_LOCK:
      // thread is blocked on something and marked interruptible
      wait_queue_state_.UnblockIfInterruptible(this, ZX_ERR_INTERNAL_INTR_RETRY);
      break;
    case THREAD_SLEEPING:
      // thread is sleeping
      wait_queue_state_.UnsleepIfInterruptible(this, ZX_ERR_INTERNAL_INTR_RETRY);
      break;
  }

  kcounter_add(thread_suspend_count, 1);
  return ZX_OK;
}

// Signal an exception on the current thread, to be handled when the
// current syscall exits.  Unlike other signals, this is synchronous, in
// the sense that a thread signals itself.  This exists primarily so that
// we can unwind the stack in order to get the state of userland's
// callee-saved registers at the point where userland invoked the
// syscall.
void Thread::Current::SignalPolicyException(uint32_t policy_exception_code,
                                            uint32_t policy_exception_data) {
  Thread* t = Thread::Current::Get();
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  t->signals_.fetch_or(THREAD_SIGNAL_POLICY_EXCEPTION, ktl::memory_order_relaxed);
  t->extra_policy_exception_code_ = policy_exception_code;
  t->extra_policy_exception_data_ = policy_exception_data;
}

void Thread::EraseFromListsLocked() {
  thread_list->erase(*this);
  if (migrate_list_node_.InContainer()) {
    migrate_list_.erase(*this);
  }
}

zx_status_t Thread::Join(int* out_retcode, zx_time_t deadline) {
  canary_.Assert();

  {
    Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};

    if (flags_ & THREAD_FLAG_DETACHED) {
      // the thread is detached, go ahead and exit
      return ZX_ERR_BAD_STATE;
    }

    // wait for the thread to die
    if (state() != THREAD_DEATH) {
      zx_status_t status = task_state_.Join(deadline);
      if (status != ZX_OK) {
        return status;
      }
    }

    canary_.Assert();
    DEBUG_ASSERT(state() == THREAD_DEATH);
    wait_queue_state_.AssertNotBlocked();

    // save the return code
    if (out_retcode) {
      *out_retcode = task_state_.retcode();
    }

    // remove it from global lists
    EraseFromListsLocked();

    // Our canary_ will be cleared out in free_thread_resources, which
    // explicitly invokes ~Thread.
  }

  free_thread_resources(this);

  kcounter_add(thread_join_count, 1);

  return ZX_OK;
}

zx_status_t Thread::Detach() {
  canary_.Assert();

  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};

  // if another thread is blocked inside Join() on this thread,
  // wake them up with a specific return code
  task_state_.WakeJoiners(ZX_ERR_BAD_STATE);

  // if it's already dead, then just do what join would have and exit
  if (state() == THREAD_DEATH) {
    flags_ &= ~THREAD_FLAG_DETACHED;  // makes sure Join continues
    guard.Release();
    return Join(nullptr, 0);
  } else {
    flags_ |= THREAD_FLAG_DETACHED;
    return ZX_OK;
  }
}

// called back in the DPC worker thread to free the stack and/or the thread structure
// itself for a thread that is exiting on its own.
void Thread::FreeDpc(Dpc* dpc) {
  Thread* t = dpc->arg<Thread>();

  t->canary_.Assert();
  DEBUG_ASSERT(t->state() == THREAD_DEATH);

  // grab and release the thread lock, which effectively serializes us with
  // the thread that is queuing itself for destruction.
  {
    Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
  }

  free_thread_resources(t);
}

__NO_RETURN void Thread::Current::ExitLocked(int retcode) TA_REQ(thread_lock) {
  Thread* current_thread = Thread::Current::Get();

  // create a dpc on the stack to queue up a free.
  // must be put at top scope in this function to force the compiler to keep it from
  // reusing the stack before the function exits
  Dpc free_dpc;

  // enter the dead state
  current_thread->set_death();
  current_thread->task_state_.set_retcode(retcode);
  current_thread->CallMigrateFnLocked(Thread::MigrateStage::Exiting);

  // Make sure that we have released any wait queues we may have owned when we
  // exited.  TODO(johngro):  Should we log a warning or take any other
  // actions here?  Normally, if a thread exits while owning a wait queue, it
  // means that it exited while holding some sort of mutex or other
  // synchronization object which will now never be released.  This is usually
  // Very Bad.  If any of the OwnedWaitQueues are being used for user-mode
  // futexes, who can say what the right thing to do is.  In the case of a
  // kernel mode mutex, it might be time to panic.
  OwnedWaitQueue::DisownAllQueues(current_thread);

  // Disable preemption to keep from switching to the DPC thread until the final
  // reschedule.
  current_thread->preemption_state().PreemptDisable();

  // if we're detached, then do our teardown here
  if (current_thread->flags_ & THREAD_FLAG_DETACHED) {
    kcounter_add(thread_detached_exit_count, 1);

    // remove it from global lists
    current_thread->EraseFromListsLocked();

    // queue a dpc to free the stack and, optionally, the thread structure
    if (current_thread->stack_.base() || (current_thread->flags_ & THREAD_FLAG_FREE_STRUCT)) {
      free_dpc = Dpc(&Thread::FreeDpc, current_thread);
      zx_status_t status = free_dpc.QueueThreadLocked();
      DEBUG_ASSERT(status == ZX_OK);
    }
  } else {
    // signal if anyone is waiting
    current_thread->task_state_.WakeJoiners(ZX_OK);
  }

  // Final reschedule.
  Scheduler::RescheduleInternal();

  panic("somehow fell through thread_exit()\n");
}

/**
 * @brief Remove this thread from the scheduler, discarding
 * its execution state.
 *
 * This is almost certainly not the function you want.  In the general case,
 * this is incredibly unsafe.
 *
 * This will free any resources allocated by thread_create.
 */
void Thread::Forget() {
  {
    Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};

    __UNUSED Thread* current_thread = Thread::Current::Get();
    DEBUG_ASSERT(current_thread != this);

    EraseFromListsLocked();
  }

  DEBUG_ASSERT(!wait_queue_state_.InWaitQueue());

  free_thread_resources(this);
}

/**
 * @brief  Terminate the current thread
 *
 * Current thread exits with the specified return code.
 *
 * This function does not return.
 */
void Thread::Current::Exit(int retcode) {
  Thread* current_thread = Thread::Current::Get();

  current_thread->canary_.Assert();
  DEBUG_ASSERT(current_thread->state() == THREAD_RUNNING);
  DEBUG_ASSERT(!current_thread->IsIdle());

  if (current_thread->user_thread_) {
    DEBUG_ASSERT(!arch_ints_disabled() || !thread_lock.IsHeld());
    current_thread->user_thread_->ExitingCurrent();
  }

  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  Thread::Current::ExitLocked(retcode);
}

void Thread::Current::Kill() {
  Thread* current_thread = Thread::Current::Get();

  current_thread->canary_.Assert();
  DEBUG_ASSERT(current_thread->state() == THREAD_RUNNING);
  DEBUG_ASSERT(!current_thread->IsIdle());

  current_thread->Kill();
}

// kill a thread
void Thread::Kill() {
  canary_.Assert();

  // Disable preemption to defer rescheduling until the end of this scope.
  AnnotatedAutoPreemptDisabler preempt_disable;
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};

  // deliver a signal to the thread.
  signals_.fetch_or(THREAD_SIGNAL_KILL, ktl::memory_order_relaxed);

  // we are killing ourself
  if (this == Thread::Current::Get()) {
    return;
  }

  // general logic is to wake up the thread so it notices it had a signal delivered to it

  switch (state()) {
    case THREAD_INITIAL:
      // thread hasn't been started yet.
      // not really safe to wake it up, since it's only in this state because it's under
      // construction by the creator thread.
      break;
    case THREAD_READY:
      // thread is ready to run and not blocked or suspended.
      // will wake up and deal with the signal soon.
      // TODO: short circuit if it was blocked from user space
      break;
    case THREAD_RUNNING:
      // thread is running (on another cpu).
      // The following call is not essential.  It just makes the
      // thread termination happen sooner rather than at the next
      // timer interrupt or syscall.
      mp_interrupt(MP_IPI_TARGET_MASK, cpu_num_to_mask(scheduler_state_.curr_cpu_));
      break;
    case THREAD_SUSPENDED:
      // thread is suspended, resume it so it can get the kill signal
      Scheduler::Unblock(this);
      break;
    case THREAD_BLOCKED:
    case THREAD_BLOCKED_READ_LOCK:
      // thread is blocked on something and marked interruptible
      wait_queue_state_.UnblockIfInterruptible(this, ZX_ERR_INTERNAL_INTR_KILLED);
      break;
    case THREAD_SLEEPING:
      // thread is sleeping
      wait_queue_state_.UnsleepIfInterruptible(this, ZX_ERR_INTERNAL_INTR_KILLED);
      break;
    case THREAD_DEATH:
      // thread is already dead
      return;
  }
}

cpu_mask_t Thread::GetCpuAffinity() const {
  canary_.Assert();
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  return scheduler_state_.hard_affinity();
}

void Thread::SetCpuAffinity(cpu_mask_t affinity) {
  canary_.Assert();
  DEBUG_ASSERT_MSG(
      (affinity & mp_get_active_mask()) != 0,
      "Attempted to set affinity mask to %#x, which has no overlap of active CPUs %#x.", affinity,
      mp_get_active_mask());

  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};

  // set the affinity mask
  scheduler_state_.hard_affinity_ = affinity;

  // let the scheduler deal with it
  Scheduler::Migrate(this);
}

void Thread::SetSoftCpuAffinity(cpu_mask_t affinity) {
  canary_.Assert();
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};

  // set the affinity mask
  scheduler_state_.soft_affinity_ = affinity;

  // let the scheduler deal with it
  Scheduler::Migrate(this);
}

cpu_mask_t Thread::GetSoftCpuAffinity() const {
  canary_.Assert();
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  return scheduler_state_.soft_affinity_;
}

void Thread::Current::MigrateToCpu(const cpu_num_t target_cpu) {
  Thread::Current::Get()->SetCpuAffinity(cpu_num_to_mask(target_cpu));
}

void Thread::SetMigrateFn(MigrateFn migrate_fn) {
  canary_.Assert();
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  SetMigrateFnLocked(ktl::move(migrate_fn));
}

void Thread::SetMigrateFnLocked(MigrateFn migrate_fn) {
  DEBUG_ASSERT(!migrate_fn || !migrate_pending_);
  canary_.Assert();
  // If |migrate_fn_| was previously set, remove |this| from |migrate_list_|.
  if (migrate_fn_) {
    migrate_list_.erase(*this);
  }

  migrate_fn_ = ktl::move(migrate_fn);

  // Clear stale state when (un) setting the migrate fn.
  // TODO(fxbug.dev/84078): Cleanup the migrate fn feature and associated state
  // and clearly define and check invariants.
  scheduler_state().next_cpu_ = INVALID_CPU;
  migrate_pending_ = false;

  // If |migrate_fn_| is valid, add |this| to |migrate_list_|.
  if (migrate_fn_) {
    migrate_list_.push_front(this);
  }
}

void Thread::CallMigrateFnLocked(MigrateStage stage) {
  if (unlikely(migrate_fn_)) {
    switch (stage) {
      case MigrateStage::Before:
        if (!migrate_pending_) {
          migrate_pending_ = true;
          migrate_fn_(this, stage);
        }
        break;

      case MigrateStage::After:
        if (migrate_pending_) {
          migrate_pending_ = false;
          migrate_fn_(this, stage);
        }
        break;

      case MigrateStage::Exiting:
        migrate_fn_(this, stage);
        break;
    }
  }
}

void Thread::CallMigrateFnForCpuLocked(cpu_num_t cpu) {
  for (auto& thread : migrate_list_) {
    if (thread.state() != THREAD_READY && thread.scheduler_state().last_cpu_ == cpu) {
      thread.CallMigrateFnLocked(Thread::MigrateStage::Before);
    }
  }
}

bool Thread::CheckKillSignal() {
  thread_lock.AssertHeld();

  if (signals() & THREAD_SIGNAL_KILL) {
    // Ensure we don't recurse into thread_exit.
    DEBUG_ASSERT(state() != THREAD_DEATH);
    return true;
  } else {
    return false;
  }
}

zx_status_t Thread::CheckKillOrSuspendSignal() const {
  const auto current_signals = signals();
  if (unlikely(current_signals & THREAD_SIGNAL_KILL)) {
    return ZX_ERR_INTERNAL_INTR_KILLED;
  }
  if (unlikely(current_signals & THREAD_SIGNAL_SUSPEND)) {
    return ZX_ERR_INTERNAL_INTR_RETRY;
  }
  return ZX_OK;
}

// finish suspending the current thread
void Thread::Current::DoSuspend() {
  Thread* current_thread = Thread::Current::Get();

  // Note: After calling this callback, we must not return without
  // calling the callback with THREAD_USER_STATE_RESUME.  That is
  // because those callbacks act as barriers which control when it is
  // safe for the zx_thread_read_state()/zx_thread_write_state()
  // syscalls to access the userland register state kept by Thread.
  if (current_thread->user_thread_) {
    DEBUG_ASSERT(!arch_ints_disabled() || !thread_lock.IsHeld());
    current_thread->user_thread_->Suspending();
  }

  {
    Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};

    // make sure we haven't been killed while the lock was dropped for the user callback
    if (current_thread->CheckKillSignal()) {
      guard.Release();
      Thread::Current::Exit(0);
    }

    // Make sure the suspend signal wasn't cleared while we were running the
    // callback.
    if (current_thread->signals() & THREAD_SIGNAL_SUSPEND) {
      current_thread->set_suspended();
      current_thread->signals_.fetch_and(~THREAD_SIGNAL_SUSPEND, ktl::memory_order_relaxed);

      // directly invoke the context switch, since we've already manipulated this thread's state
      Scheduler::RescheduleInternal();

      // If the thread was killed, we should not allow it to resume.  We
      // shouldn't call user_callback() with THREAD_USER_STATE_RESUME in
      // this case, because there might not have been any request to
      // resume the thread.
      if (current_thread->CheckKillSignal()) {
        guard.Release();
        Thread::Current::Exit(0);
      }
    }
  }

  if (current_thread->user_thread_) {
    DEBUG_ASSERT(!arch_ints_disabled() || !thread_lock.IsHeld());
    current_thread->user_thread_->Resuming();
  }
}

bool Thread::SaveUserStateLocked() {
  thread_lock.AssertHeld();
  DEBUG_ASSERT(this == Thread::Current::Get());
  DEBUG_ASSERT(user_thread_ != nullptr);

  if (user_state_saved_) {
    return false;
  }
  user_state_saved_ = true;
  arch_save_user_state(this);
  return true;
}

void Thread::RestoreUserStateLocked() {
  thread_lock.AssertHeld();
  DEBUG_ASSERT(this == Thread::Current::Get());
  DEBUG_ASSERT(user_thread_ != nullptr);

  DEBUG_ASSERT(user_state_saved_);
  user_state_saved_ = false;
  arch_restore_user_state(this);
}

ScopedThreadExceptionContext::ScopedThreadExceptionContext(const arch_exception_context_t* context)
    : thread_(Thread::Current::Get()), context_(context) {
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  // It's possible that the context and state have been installed/saved earlier in the call chain.
  // If so, then it's some other object's responsibilty to remove/restore.
  need_to_remove_ = arch_install_exception_context(thread_, context_);
  need_to_restore_ = thread_->SaveUserStateLocked();
}

ScopedThreadExceptionContext::~ScopedThreadExceptionContext() {
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  // Did we save the state?  If so, then it's our job to restore it.
  if (need_to_restore_) {
    thread_->RestoreUserStateLocked();
  }
  // Did we install the exception context? If so, then it's out job to remove it.
  if (need_to_remove_) {
    arch_remove_exception_context(thread_);
  }
}

// check for any pending signals and handle them
void Thread::Current::ProcessPendingSignals(GeneralRegsSource source, void* gregs) {
  Thread* current_thread = Thread::Current::Get();
  if (likely(current_thread->signals() == 0)) {
    return;
  }

  // grab the thread lock so we can safely look at the signal mask
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};

  // This thread is about to be killed, raise an exception, or become suspended.  If this is a user
  // thread, these are all debugger-visible actions.  Save the general registers so that a debugger
  // may access them.
  const bool has_user_thread = current_thread->user_thread_ != nullptr;
  if (has_user_thread) {
    arch_set_suspended_general_regs(current_thread, source, gregs);
  }
  auto cleanup_suspended_general_regs = fit::defer([current_thread, has_user_thread]() {
    if (has_user_thread) {
      arch_reset_suspended_general_regs(current_thread);
    }
  });

  if (current_thread->CheckKillSignal()) {
    guard.Release();
    cleanup_suspended_general_regs.cancel();
    Thread::Current::Exit(0);
  }

  // Report any policy exceptions raised by syscalls.
  const unsigned int signals = current_thread->signals();
  if (has_user_thread && (signals & THREAD_SIGNAL_POLICY_EXCEPTION)) {
    current_thread->signals_.fetch_and(~THREAD_SIGNAL_POLICY_EXCEPTION, ktl::memory_order_relaxed);
    uint32_t policy_exception_code = current_thread->extra_policy_exception_code_;
    uint32_t policy_exception_data = current_thread->extra_policy_exception_data_;
    guard.Release();

    zx_status_t status =
        arch_dispatch_user_policy_exception(policy_exception_code, policy_exception_data);
    if (status != ZX_OK) {
      panic("arch_dispatch_user_policy_exception() failed: status=%d\n", status);
    }
    return;
  }

  if (signals & THREAD_SIGNAL_SUSPEND) {
    DEBUG_ASSERT(current_thread->state() == THREAD_RUNNING);
    // This thread has been asked to suspend.  If it has a user mode component we need to save the
    // user register state prior to calling |thread_do_suspend| so that a debugger may access it
    // while the thread is suspended.
    if (has_user_thread) {
      // The enclosing function, |thread_process_pending_signals|, is called at the boundary of
      // kernel and user mode (e.g. just before returning from a syscall, timer interrupt, or
      // architectural exception/fault).  We're about the perform a save.  If the save fails
      // (returns false), then we likely have a mismatched save/restore pair, which is a bug.
      const bool saved = current_thread->SaveUserStateLocked();
      DEBUG_ASSERT(saved);
      guard.CallUnlocked([]() { Thread::Current::DoSuspend(); });
      if (saved) {
        current_thread->RestoreUserStateLocked();
      }
    } else {
      // No user mode component so nothing to save.
      guard.Release();
      Thread::Current::DoSuspend();
    }
  }
}

/**
 * @brief Yield the cpu to another thread
 *
 * This function places the current thread at the end of the run queue
 * and yields the cpu to another waiting thread (if any.)
 *
 * This function will return at some later time. Possibly immediately if
 * no other threads are waiting to execute.
 */
void Thread::Current::Yield() {
  __UNUSED Thread* current_thread = Thread::Current::Get();

  current_thread->canary_.Assert();
  DEBUG_ASSERT(current_thread->state() == THREAD_RUNNING);
  DEBUG_ASSERT(!arch_blocking_disallowed());

  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};

  CPU_STATS_INC(yields);
  Scheduler::Yield();
}

/**
 * @brief Preempt the current thread from an interrupt
 *
 * This function places the current thread at the head of the run
 * queue and then yields the cpu to another thread.
 */
void Thread::Current::Preempt() {
  Thread* current_thread = Thread::Current::Get();

  current_thread->canary_.Assert();
  DEBUG_ASSERT(current_thread->state() == THREAD_RUNNING);
  DEBUG_ASSERT(!arch_blocking_disallowed());

  if (!current_thread->IsIdle()) {
    // only track when a meaningful preempt happens
    CPU_STATS_INC(irq_preempts);
  }

  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};

  Scheduler::Preempt();
}

/**
 * @brief Reevaluate the run queue on the current cpu.
 *
 * This function places the current thread at the head of the run
 * queue and then yields the cpu to another thread. Similar to
 * thread_preempt, but intended to be used at non interrupt context.
 */
void Thread::Current::Reschedule() {
  Thread* current_thread = Thread::Current::Get();

  current_thread->canary_.Assert();
  DEBUG_ASSERT(current_thread->state() == THREAD_RUNNING);
  DEBUG_ASSERT(!arch_blocking_disallowed());

  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};

  Scheduler::Reschedule();
}

void PreemptionState::SetPreemptionTimerForExtension(zx_time_t deadline) {
  // Interrupts must be disabled when calling PreemptReset.
  InterruptDisableGuard interrupt_disable;
  percpu::Get(arch_curr_cpu_num()).timer_queue.PreemptReset(deadline);
  kcounter_add(thread_timeslice_extended, 1);
}

void PreemptionState::FlushPendingContinued(Flush flush) {
  // If we're flushing the local CPU, make sure OK to block since flushing
  // local may trigger a reschedule.
  DEBUG_ASSERT(((flush & FlushLocal) == 0) || !arch_blocking_disallowed());

  const auto do_flush = [this, flush]() TA_REQ(thread_lock) {
    // Recheck, pending preemptions could have been flushed by a context switch
    // before interrupts were disabled.
    const cpu_mask_t pending_mask = preempts_pending_;

    // If there is a pending local preemption the scheduler will take care of
    // flushing all pending reschedules.
    const cpu_mask_t current_cpu_mask = cpu_num_to_mask(arch_curr_cpu_num());
    if ((pending_mask & current_cpu_mask) != 0 && (flush & FlushLocal) != 0) {
      // Clear the local preempt pending flag before calling preempt.  Failure
      // to do this can cause recursion during Scheduler::Preempt if any code
      // (such as debug tracing code) attempts to disable and re-enable
      // preemption during the scheduling operation.
      preempts_pending_ &= ~current_cpu_mask;
      Scheduler::Preempt();
    } else if ((flush & FlushRemote) != 0) {
      // The current cpu is ignored by mp_reschedule if present in the mask.
      mp_reschedule(pending_mask, 0);
      preempts_pending_ &= current_cpu_mask;
    }
  };

  // This method may be called with interrupts enabled or disabled and with or
  // without holding the thread lock.
  InterruptDisableGuard interrupt_disable;
  if (thread_lock.IsHeld()) {
    thread_lock.AssertHeld();
    do_flush();
  } else {
    Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
    do_flush();
  }
}

// timer callback to wake up a sleeping thread
void Thread::SleepHandler(Timer* timer, zx_time_t now, void* arg) {
  Thread* t = static_cast<Thread*>(arg);
  t->canary_.Assert();
  t->HandleSleep(timer, now);
}

void Thread::HandleSleep(Timer* timer, zx_time_t now) {
  // spin trylocking on the thread lock since the routine that set up the callback,
  // thread_sleep_etc, may be trying to simultaneously cancel this timer while holding the
  // thread_lock.
  if (timer->TrylockOrCancel(&thread_lock)) {
    return;
  }

  if (state() != THREAD_SLEEPING) {
    thread_lock.Release();
    return;
  }

  // Unblock the thread, regardless of whether the sleep was interruptible.
  wait_queue_state_.Unsleep(this, ZX_OK);
  thread_lock.Release();
}

#define MIN_SLEEP_SLACK ZX_USEC(1)
#define MAX_SLEEP_SLACK ZX_SEC(1)
#define DIV_SLEEP_SLACK 10u

// computes the amount of slack the thread_sleep timer will use
static zx_duration_t sleep_slack(zx_time_t deadline, zx_time_t now) {
  if (deadline < now) {
    return MIN_SLEEP_SLACK;
  }
  zx_duration_t slack = zx_time_sub_time(deadline, now) / DIV_SLEEP_SLACK;
  return ktl::max(MIN_SLEEP_SLACK, ktl::min(slack, MAX_SLEEP_SLACK));
}

/**
 * @brief  Put thread to sleep; deadline specified in ns
 *
 * This function puts the current thread to sleep until the specified
 * deadline has occurred.
 *
 * Note that this function could continue to sleep after the specified deadline
 * if other threads are running.  When the deadline occurrs, this thread will
 * be placed at the head of the run queue.
 *
 * interruptible argument allows this routine to return early if the thread was signaled
 * for something.
 */
zx_status_t Thread::Current::SleepEtc(const Deadline& deadline, Interruptible interruptible,
                                      zx_time_t now) {
  Thread* current_thread = Thread::Current::Get();

  current_thread->canary_.Assert();
  DEBUG_ASSERT(current_thread->state() == THREAD_RUNNING);
  DEBUG_ASSERT(!current_thread->IsIdle());
  DEBUG_ASSERT(!arch_blocking_disallowed());

  // Skip all of the work if the deadline has already passed.
  if (deadline.when() <= now) {
    return ZX_OK;
  }

  Timer timer;

  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};

  // if we've been killed and going in interruptible, abort here
  if (interruptible == Interruptible::Yes && unlikely((current_thread->signals()))) {
    if (current_thread->signals() & THREAD_SIGNAL_KILL) {
      return ZX_ERR_INTERNAL_INTR_KILLED;
    } else {
      return ZX_ERR_INTERNAL_INTR_RETRY;
    }
  }

  // set a one shot timer to wake us up and reschedule
  timer.Set(deadline, &Thread::SleepHandler, current_thread);

  current_thread->set_sleeping();
  current_thread->wait_queue_state_.Block(interruptible, ZX_OK);

  // always cancel the timer, since we may be racing with the timer tick on other cpus
  timer.Cancel();

  return current_thread->wait_queue_state_.BlockedStatus();
}

zx_status_t Thread::Current::Sleep(zx_time_t deadline) {
  const zx_time_t now = current_time();
  return SleepEtc(Deadline::no_slack(deadline), Interruptible::No, now);
}

zx_status_t Thread::Current::SleepRelative(zx_duration_t delay) {
  const zx_time_t now = current_time();
  const Deadline deadline = Deadline::no_slack(zx_time_add_duration(now, delay));
  return SleepEtc(deadline, Interruptible::No, now);
}

zx_status_t Thread::Current::SleepInterruptible(zx_time_t deadline) {
  const zx_time_t now = current_time();
  const TimerSlack slack(sleep_slack(deadline, now), TIMER_SLACK_LATE);
  const Deadline slackDeadline(deadline, slack);
  return SleepEtc(slackDeadline, Interruptible::Yes, now);
}

/**
 * @brief Return the number of nanoseconds a thread has been running for.
 *
 * This takes the thread_lock to ensure there are no races while calculating the
 * runtime of the thread.
 */
zx_duration_t Thread::Runtime() const {
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};

  zx_duration_t runtime = scheduler_state_.runtime_ns();
  if (state() == THREAD_RUNNING) {
    zx_duration_t recent =
        zx_time_sub_time(current_time(), scheduler_state_.last_started_running());
    runtime = zx_duration_add_duration(runtime, recent);
  }

  return runtime;
}

/**
 * @brief Get the last CPU the given thread was run on, or INVALID_CPU if the
 * thread has never run.
 */
cpu_num_t Thread::LastCpu() const {
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  return scheduler_state_.last_cpu_;
}

/**
 * @brief Get the last CPU the given thread was run on, or INVALID_CPU if the
 * thread has never run.
 */
cpu_num_t Thread::LastCpuLocked() const { return scheduler_state_.last_cpu_; }

/**
 * @brief Construct a thread t around the current running state
 *
 * This should be called once per CPU initialization.  It will create
 * a thread that is pinned to the current CPU and running at the
 * highest priority.
 */
void thread_construct_first(Thread* t, const char* name) {
  DEBUG_ASSERT(arch_ints_disabled());

  construct_thread(t, name);
  t->set_detached(true);

  // Setup the scheduler state.
  Scheduler::InitializeFirstThread(t);

  // Start out with preemption disabled to avoid attempts to reschedule until
  // threading is fulling enabled. This simplifies code paths shared between
  // initialization and runtime (e.g. logging). Preemption is enabled when the
  // idle thread for the current CPU is ready.
  t->preemption_state().PreemptDisable();

  arch_thread_construct_first(t);

  // Take care not to touch any locks when invoked by early init code that runs
  // before global ctors are called. The thread_list is safe to mutate before
  // global ctors are run.
  if (lk_global_constructors_called()) {
    Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
    thread_list->push_front(t);
  } else {
    [t]() TA_NO_THREAD_SAFETY_ANALYSIS { thread_list->push_front(t); }();
  }
}

/**
 * @brief  Initialize threading system
 *
 * This function is called once, from kmain()
 */
void thread_init_early() {
  DEBUG_ASSERT(arch_curr_cpu_num() == 0);

  // Initialize the thread list. This needs to be done manually now, since initial thread code
  // manipulates the list before global constructors are run.
  thread_list.Initialize();

  // Init the boot percpu data.
  percpu::InitializeBoot();

  // create a thread to cover the current running state
  Thread* t = &percpu::Get(0).idle_thread;
  thread_construct_first(t, "bootstrap");
}

/**
 * @brief Change name of current thread
 */
void Thread::Current::SetName(const char* name) {
  Thread* current_thread = Thread::Current::Get();
  strlcpy(current_thread->name_, name, sizeof(current_thread->name_));
}

/**
 * @brief Change priority of current thread
 *
 * Sets the thread to use the fair scheduling discipline using the given
 * priority.
 *
 * See Thread::Create() for a discussion of priority values.
 */
void Thread::SetPriority(int priority) {
  canary_.Assert();
  ASSERT(priority >= LOWEST_PRIORITY && priority <= HIGHEST_PRIORITY);

  // It is not sufficient to simply hold the thread lock while changing the
  // profile of a thread. Doing so runs the risk that a change to a PI graph
  // results in another thread becoming "more runnable" than we are, and then
  // immediately context switching to that thread.
  //
  // Basically, when we interact with the scheduler, we cannot always think of
  // the thread lock as a lock.  While we cannot take any interrupts, and no
  // other threads can access our object's state, we _can_ accidentally give up
  // our timeslice to another thread, and the thread lock as well in the
  // process.  That thread can then (rarely) end up calling back into object
  // state we are modifying (like, an OwnedWaitQueue) which could end up being
  // Very Bad.
  //
  // By adding an AutoPreemptDisabler, we can make the thread_lock behave more
  // like a real lock (at least for the OWQ state).  Interactions with the
  // scheduler might result in another thread needing to run, but at least we
  // will have have deferred that until we are finished interacting with our
  // queue and have dropped the thread lock.
  AnnotatedAutoPreemptDisabler apd;
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  this->get_lock().AssertHeld();
  Scheduler::ChangePriority(this, priority);
}

/**
 * @brief Change the deadline of current thread
 *
 * Sets the thread to use the deadline scheduling discipline using the given
 * parameters.
 *
 * @param t The thread to set or change deadline scheduling parameters.
 * @param params The deadline parameters to apply to the thread.
 */
void Thread::SetDeadline(const zx_sched_deadline_params_t& params) {
  canary_.Assert();
  ASSERT(params.capacity > 0 && params.capacity <= params.relative_deadline &&
         params.relative_deadline <= params.period);

  // See the comment in Thread::SetPriority
  AnnotatedAutoPreemptDisabler apd;
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  this->get_lock().AssertHeld();
  Scheduler::ChangeDeadline(this, params);
}

/**
 * @brief Set the pointer to the user-mode thread, this will receive callbacks:
 * ThreadDispatcher::Exiting()
 * ThreadDispatcher::Suspending() / Resuming()
 *
 * This also caches the assocatiated koids of the thread and process
 * dispatchers associated with the given ThreadDispatcher.
 */
void Thread::SetUsermodeThread(fbl::RefPtr<ThreadDispatcher> user_thread) {
  canary_.Assert();
  DEBUG_ASSERT(state() == THREAD_INITIAL);
  DEBUG_ASSERT(!user_thread_);

  user_thread_ = ktl::move(user_thread);
  tid_ = user_thread_->get_koid();
  pid_ = user_thread_->process()->get_koid();

  // All user mode threads are detached since they are responsible for cleaning themselves up.
  // We can set this directly because we've checked that we are in the initial state.
  flags_ |= THREAD_FLAG_DETACHED;
}

/**
 * @brief  Become an idle thread
 *
 * This function marks the current thread as the idle thread -- the one which
 * executes when there is nothing else to do.  This function does not return.
 * This thread is called once at boot on the first cpu.
 */
void Thread::Current::BecomeIdle() {
  DEBUG_ASSERT(arch_ints_disabled());

  Thread* t = Thread::Current::Get();
  cpu_num_t curr_cpu = arch_curr_cpu_num();

  // Set our name
  char name[16];
  snprintf(name, sizeof(name), "idle %u", curr_cpu);
  Thread::Current::SetName(name);

  // Mark ourself as idle
  t->flags_ |= THREAD_FLAG_IDLE;

  // Now that we are the idle thread, make sure that we drop out of the
  // scheduler's bookkeeping altogether.
  Scheduler::RemoveFirstThread(t);
  t->set_running();

  // Cpu is active.
  mp_set_curr_cpu_active(true);
  mp_set_cpu_idle(curr_cpu);

  // Pend a preemption to ensure a reschedule.
  arch_set_blocking_disallowed(true);
  t->preemption_state().PreemptSetPending();
  arch_set_blocking_disallowed(false);

  // Enable preemption to start scheduling. Preemption is disabled during early
  // threading startup on each CPU to prevent incidental thread wakeups (e.g.
  // due to logging) from rescheduling on the local CPU before the idle thread
  // is ready.
  t->preemption_state().PreemptReenable();
  DEBUG_ASSERT(t->preemption_state().PreemptIsEnabled());

  // We're now properly in the idle routine. Reenable interrupts and drop
  // into the idle routine, never return.
  arch_enable_ints();
  arch_idle_thread_routine(nullptr);

  __UNREACHABLE;
}

/**
 * @brief Create a thread around the current execution context, preserving |t|'s stack
 *
 * Prior to calling, |t->stack| must be properly constructed. See |vm_allocate_kstack|.
 */
void Thread::SecondaryCpuInitEarly() {
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(stack_.base() != 0);
  DEBUG_ASSERT(IS_ALIGNED(this, alignof(Thread)));

  // At this point, the CPU isn't far enough along to allow threads to block. Set blocking
  // disallowed until to catch bugs where code might block before we're ready.
  arch_set_blocking_disallowed(true);

  percpu::InitializeSecondaryFinish();

  char name[16];
  snprintf(name, sizeof(name), "cpu_init %u", arch_curr_cpu_num());
  thread_construct_first(this, name);
}

/**
 * @brief The last routine called on the secondary cpu's bootstrap thread.
 */
void thread_secondary_cpu_entry() {
  DEBUG_ASSERT(arch_blocking_disallowed());

  mp_set_curr_cpu_active(true);

  percpu::GetCurrent().dpc_queue.InitForCurrentCpu();

  // Remove ourselves from the Scheduler's bookkeeping
  Scheduler::RemoveFirstThread(Thread::Current::Get());

  // Exit from our bootstrap thread, and enter the scheduler on this cpu
  Thread::Current::Exit(0);
}

/**
 * @brief Create an idle thread for a secondary CPU
 */
Thread* Thread::CreateIdleThread(cpu_num_t cpu_num) {
  DEBUG_ASSERT(cpu_num != 0 && cpu_num < SMP_MAX_CPUS);

  char name[16];
  snprintf(name, sizeof(name), "idle %u", cpu_num);

  Thread* t = Thread::CreateEtc(&percpu::Get(cpu_num).idle_thread, name, arch_idle_thread_routine,
                                nullptr, IDLE_PRIORITY, nullptr);
  if (t == nullptr) {
    return t;
  }
  t->flags_ |= THREAD_FLAG_IDLE | THREAD_FLAG_DETACHED;
  t->scheduler_state_.hard_affinity_ = cpu_num_to_mask(cpu_num);

  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  Scheduler::UnblockIdle(t);
  return t;
}

/**
 * @brief Return the name of the "owner" of the thread.
 *
 * Returns "kernel" if there is no owner.
 */

void Thread::OwnerName(char (&out_name)[ZX_MAX_NAME_LEN]) {
  if (user_thread_) {
    user_thread_->process()->get_name(out_name);
    return;
  }
  memcpy(out_name, "kernel", 7);
}

static const char* thread_state_to_str(enum thread_state state) {
  switch (state) {
    case THREAD_INITIAL:
      return "init";
    case THREAD_SUSPENDED:
      return "susp";
    case THREAD_READY:
      return "rdy";
    case THREAD_RUNNING:
      return "run";
    case THREAD_BLOCKED:
    case THREAD_BLOCKED_READ_LOCK:
      return "blok";
    case THREAD_SLEEPING:
      return "slep";
    case THREAD_DEATH:
      return "deth";
    default:
      return "unkn";
  }
}

/**
 * @brief  Dump debugging info about the specified thread.
 */
void dump_thread_locked(Thread* t, bool full_dump) {
  if (!t->canary().Valid()) {
    dprintf(INFO, "dump_thread WARNING: thread at %p has bad magic\n", t);
  }

  zx_duration_t runtime = t->scheduler_state().runtime_ns();
  if (t->state() == THREAD_RUNNING) {
    zx_duration_t recent =
        zx_time_sub_time(current_time(), t->scheduler_state().last_started_running());
    runtime = zx_duration_add_duration(runtime, recent);
  }

  char oname[ZX_MAX_NAME_LEN];
  t->OwnerName(oname);

  if (full_dump) {
    dprintf(INFO, "dump_thread: t %p (%s:%s)\n", t, oname, t->name());
    dprintf(INFO,
            "\tstate %s, curr/last cpu %d/%d, hard_affinity %#x, soft_cpu_affinity %#x, "
            "priority %d [%d,%d], remaining time slice %" PRIi64 "\n",
            thread_state_to_str(t->state()), (int)t->scheduler_state().curr_cpu(),
            (int)t->scheduler_state().last_cpu(), t->scheduler_state().hard_affinity(),
            t->scheduler_state().soft_affinity(), t->scheduler_state().effective_priority(),
            t->scheduler_state().base_priority(), t->scheduler_state().inherited_priority(),
            t->scheduler_state().time_slice_ns());
    dprintf(INFO, "\truntime_ns %" PRIi64 ", runtime_s %" PRIi64 "\n", runtime,
            runtime / 1000000000);
    t->stack().DumpInfo(INFO);
    dprintf(INFO, "\tentry %p, arg %p, flags 0x%x %s%s%s%s\n", t->task_state_.entry_,
            t->task_state_.arg_, t->flags_, (t->flags_ & THREAD_FLAG_DETACHED) ? "Dt" : "",
            (t->flags_ & THREAD_FLAG_FREE_STRUCT) ? "Ft" : "",
            (t->flags_ & THREAD_FLAG_IDLE) ? "Id" : "", (t->flags_ & THREAD_FLAG_VCPU) ? "Vc" : "");

    dprintf(INFO, "\twait queue %p, blocked_status %d, interruptible %s, wait queues owned %s\n",
            t->wait_queue_state().blocking_wait_queue_, t->wait_queue_state().blocked_status_,
            t->wait_queue_state().interruptible_ == Interruptible::Yes ? "yes" : "no",
            t->wait_queue_state().owned_wait_queues_.is_empty() ? "no" : "yes");

    dprintf(INFO, "\taspace %p\n", t->aspace_);
    dprintf(INFO, "\tuser_thread %p, pid %" PRIu64 ", tid %" PRIu64 "\n", t->user_thread_.get(),
            t->pid(), t->tid());
    arch_dump_thread(t);
  } else {
    printf("thr %p st %4s owq %d pri %2d [%d,%d] pid %" PRIu64 " tid %" PRIu64 " (%s:%s)\n", t,
           thread_state_to_str(t->state()), !t->wait_queue_state().owned_wait_queues_.is_empty(),
           t->scheduler_state().effective_priority_, t->scheduler_state().base_priority_,
           t->scheduler_state().inherited_priority_, t->pid(), t->tid(), oname, t->name());
  }
}

void dump_thread(Thread* t, bool full) {
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  dump_thread_locked(t, full);
}

/**
 * @brief  Dump debugging info about all threads
 */
void dump_all_threads_locked(bool full) {
  for (Thread& t : thread_list.Get()) {
    if (!t.canary().Valid()) {
      dprintf(INFO, "bad magic on thread struct %p, aborting.\n", &t);
      hexdump(&t, sizeof(Thread));
      break;
    }
    dump_thread_locked(&t, full);
  }
}

void dump_all_threads(bool full) {
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  dump_all_threads_locked(full);
}

void dump_thread_tid(zx_koid_t tid, bool full) {
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  dump_thread_tid_locked(tid, full);
}

void dump_thread_tid_locked(zx_koid_t tid, bool full) {
  for (Thread& t : thread_list.Get()) {
    if (t.tid() != tid) {
      continue;
    }

    if (!t.canary().Valid()) {
      dprintf(INFO, "bad magic on thread struct %p, aborting.\n", &t);
      hexdump(&t, sizeof(Thread));
      break;
    }
    dump_thread_locked(&t, full);
  }
}

Thread* thread_id_to_thread_slow(zx_koid_t tid) {
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  for (Thread& t : thread_list.Get()) {
    if (t.tid() == tid) {
      return &t;
    }
  }

  return nullptr;
}

/** @} */

// Used by ktrace at the start of a trace to ensure that all
// the running threads, processes, and their names are known
void ktrace_report_live_threads() {
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
  for (Thread& t : thread_list.Get()) {
    t.canary().Assert();
    fxt_kernel_object(
        TAG_THREAD_NAME, /*always*/ true, t.tid(), ZX_OBJ_TYPE_THREAD, fxt::StringRef(t.name()),
        fxt::Argument<fxt::ArgumentType::kKoid, fxt::RefType::kId>(
            fxt::StringRef(static_cast<uint16_t>("process"_stringref->GetFxtId())), t.pid()));
  }
}

void Thread::UpdateSchedulerStats(const RuntimeStats::SchedulerStats& stats) {
  if (user_thread_) {
    user_thread_->UpdateSchedulerStats(stats);
  }
}

namespace {

// TODO(maniscalco): Consider moving this method to the KernelStack class.
// That's probably a better home for it.
zx_status_t ReadStack(Thread* thread, vaddr_t ptr, vaddr_t* out, size_t sz) {
  if (!is_kernel_address(ptr) || (ptr < thread->stack().base()) ||
      (ptr > (thread->stack().top() - sz))) {
    return ZX_ERR_NOT_FOUND;
  }
  memcpy(out, reinterpret_cast<const void*>(ptr), sz);
  return ZX_OK;
}

void GetBacktraceCommon(Thread* thread, vaddr_t fp, Backtrace& out_bt) {
  // Be sure that all paths out of this function leave with |out_bt| either
  // properly filled in or empty.
  out_bt.reset();

  // Without frame pointers, dont even try.  The compiler should optimize out
  // the body of all the callers if it's not present.
  if (!WITH_FRAME_POINTERS) {
    return;
  }

  // Perhaps we don't yet have a thread context?
  if (thread == nullptr) {
    return;
  }

  if (fp == 0) {
    return;
  }

  vaddr_t pc;
  size_t n = 0;
  for (; n < Backtrace::kMaxSize; n++) {
    if (ReadStack(thread, fp + 8, &pc, sizeof(vaddr_t))) {
      break;
    }
    out_bt.push_back(pc);
    if (ReadStack(thread, fp, &fp, sizeof(vaddr_t))) {
      break;
    }
  }
}

}  // namespace

void Thread::Current::GetBacktrace(Backtrace& out_bt) {
  auto fp = reinterpret_cast<vaddr_t>(__GET_FRAME(0));
  GetBacktraceCommon(Thread::Current::Get(), fp, out_bt);

  // (fxbug.dev/97528): Force the function to not tail call GetBacktraceCommon.
  // This will make sure the frame pointer we grabbed at the top
  // of the function is still valid across the call.
  asm("");
}

void Thread::Current::GetBacktrace(vaddr_t fp, Backtrace& out_bt) {
  GetBacktraceCommon(Thread::Current::Get(), fp, out_bt);
}

void Thread::GetBacktrace(Backtrace& out_bt) {
  Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};

  // Get the starting point if it's in a usable state.
  vaddr_t fp = 0;
  switch (state()) {
    case THREAD_BLOCKED:
    case THREAD_BLOCKED_READ_LOCK:
    case THREAD_SLEEPING:
    case THREAD_SUSPENDED:
      // Thread is blocked, so ask the arch code to get us a starting point.
      fp = arch_thread_get_blocked_fp(this);
      break;
    default:
      // Not in a valid state, can't get a backtrace.  Reset it so the caller
      // doesn't inadvertently use a previous value.
      out_bt.reset();
      return;
  }

  GetBacktraceCommon(this, fp, out_bt);
}
