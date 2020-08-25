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
#include <err.h>
#include <inttypes.h>
#include <lib/arch/intrin.h>
#include <lib/counters.h>
#include <lib/heap.h>
#include <lib/ktrace.h>
#include <lib/lazy_init/lazy_init.h>
#include <lib/version.h>
#include <platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/listnode.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <arch/debugger.h>
#include <arch/exception.h>
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
#include <lockdep/lockdep.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <pretty/hexdump.h>
#include <vm/kstack.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>

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

// The global thread list. This is a lazy_init type, since initial thread code
// manipulates the list before global constructors are run. This is initialized by
// thread_init_early.
static lazy_init::LazyInit<Thread::List> thread_list;

Thread::MigrateList Thread::migrate_list_;

// master thread spinlock
SpinLock thread_lock __CPU_ALIGN_EXCLUSIVE;

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

void WaitQueueState::Block(Interruptible interruptible, zx_status_t status) {
  blocked_status_ = status;
  interruptible_ = interruptible;
  Scheduler::Block();
  interruptible_ = Interruptible::No;
}

void WaitQueueState::UnblockIfInterruptible(Thread* thread, zx_status_t status) {
  if (interruptible_ == Interruptible::Yes) {
    WaitQueue::UnblockThread(thread, status);
  }
}

bool WaitQueueState::Unsleep(Thread* thread, zx_status_t status) {
  blocked_status_ = status;
  return Scheduler::Unblock(thread);
}

bool WaitQueueState::UnsleepIfInterruptible(Thread* thread, zx_status_t status) {
  if (interruptible_ == Interruptible::Yes) {
    return Unsleep(thread, status);
  }
  return false;
}

void WaitQueueState::UpdatePriorityIfBlocked(Thread* thread, int priority, PropagatePI propagate) {
  if (blocking_wait_queue_) {
    blocking_wait_queue_->PriorityChanged(thread, priority, propagate);
  }
}

WaitQueueState::~WaitQueueState() {
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

void init_thread_struct(Thread* t, const char* name) {
  memset(t, 0, sizeof(Thread));

  // Placement new to trigger any special construction requirements of the
  // Thread structure.
  //
  // TODO(johngro): now that we have converted Thread over to C++, consider
  // switching to using C++ constructors/destructors and new/delete to handle
  // all of this instead of using init_thread_struct and free_thread_resources
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

void TaskState::WakeJoiners(zx_status_t status) { retcode_wait_queue_.WakeAll(false, status); }

void Thread::Trampoline() {
  // Release the thread lock that was implicitly held across the reschedule.
  thread_lock.Release();
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
    t = static_cast<Thread*>(malloc(sizeof(Thread)));
    if (!t) {
      return nullptr;
    }
    flags |= THREAD_FLAG_FREE_STRUCT;
  }

  init_thread_struct(t, name);

  t->task_state_.Init(entry, arg);

  t->state_ = THREAD_INITIAL;

  Scheduler::InitializeThread(t, priority);

  zx_status_t status = t->stack_.Init();
  if (status != ZX_OK) {
    if (flags & THREAD_FLAG_FREE_STRUCT) {
      free(t);
    }
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
    Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
    thread_list->push_front(t);
  }

  kcounter_add(thread_create_count, 1);
  return t;
}

Thread* Thread::Create(const char* name, thread_start_routine entry, void* arg, int priority) {
  return Thread::CreateEtc(nullptr, name, entry, arg, priority, nullptr);
}

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

/**
 * @brief  Make a suspended thread executable.
 *
 * This function is called to start a thread which has just been
 * created with thread_create() or which has been suspended with
 * thread_suspend(). It can not fail.
 */
void Thread::Resume() {
  canary_.Assert();

  bool ints_disabled = arch_ints_disabled();
  bool resched = false;
  if (!ints_disabled) {  // HACK, don't resced into bootstrap thread before idle thread is set up
    resched = true;
  }

  {
    Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

    if (state_ == THREAD_DEATH) {
      // The thread is dead, resuming it is a no-op.
      return;
    }

    // Clear the suspend signal in case there is a pending suspend
    signals_ &= ~THREAD_SIGNAL_SUSPEND;

    if (state_ == THREAD_INITIAL || state_ == THREAD_SUSPENDED) {
      // wake up the new thread, putting it in a run queue on a cpu. reschedule if the local
      // cpu run queue was modified
      bool local_resched = Scheduler::Unblock(this);
      if (resched && local_resched) {
        Scheduler::Reschedule();
      }
    }
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

  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

  if (state_ == THREAD_DEATH) {
    return ZX_ERR_BAD_STATE;
  }

  signals_ |= THREAD_SIGNAL_SUSPEND;

  bool local_resched = false;
  switch (state_) {
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
      local_resched = Scheduler::Unblock(this);
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
      mp_reschedule(cpu_num_to_mask(scheduler_state_.curr_cpu_), 0);
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
      local_resched = wait_queue_state_.UnsleepIfInterruptible(this, ZX_ERR_INTERNAL_INTR_RETRY);
      break;
  }

  // reschedule if the local cpu run queue was modified
  if (local_resched) {
    Scheduler::Reschedule();
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
void Thread::Current::SignalPolicyException() {
  Thread* t = Thread::Current::Get();
  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
  t->signals_ |= THREAD_SIGNAL_POLICY_EXCEPTION;
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
    Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

    if (flags_ & THREAD_FLAG_DETACHED) {
      // the thread is detached, go ahead and exit
      return ZX_ERR_BAD_STATE;
    }

    // wait for the thread to die
    if (state_ != THREAD_DEATH) {
      zx_status_t status = task_state_.Join(deadline);
      if (status != ZX_OK) {
        return status;
      }
    }

    canary_.Assert();
    DEBUG_ASSERT(state_ == THREAD_DEATH);
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

  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

  // if another thread is blocked inside Join() on this thread,
  // wake them up with a specific return code
  task_state_.WakeJoiners(ZX_ERR_BAD_STATE);

  // if it's already dead, then just do what join would have and exit
  if (state_ == THREAD_DEATH) {
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
  DEBUG_ASSERT(t->state_ == THREAD_DEATH);

  // grab and release the thread lock, which effectively serializes us with
  // the thread that is queuing itself for destruction.
  {
    Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
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
  current_thread->state_ = THREAD_DEATH;

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

  // reschedule
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
    Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

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
  DEBUG_ASSERT(current_thread->state_ == THREAD_RUNNING);
  DEBUG_ASSERT(!current_thread->IsIdle());

  if (current_thread->user_thread_) {
    DEBUG_ASSERT(!arch_ints_disabled() || !thread_lock.IsHeld());
    current_thread->user_thread_->ExitingCurrent();
  }

  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
  Thread::Current::ExitLocked(retcode);
}

// kill a thread
void Thread::Kill() {
  canary_.Assert();

  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

  // deliver a signal to the thread.
  // NOTE: it's not important to do this atomically, since we're inside
  // the thread lock, but go ahead and flush it out to memory to avoid the amount
  // of races if another thread is looking at this.
  signals_ |= THREAD_SIGNAL_KILL;
  arch::ThreadMemoryBarrier();

  bool local_resched = false;

  // we are killing ourself
  if (this == Thread::Current::Get()) {
    return;
  }

  // general logic is to wake up the thread so it notices it had a signal delivered to it

  switch (state_) {
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
      mp_reschedule(cpu_num_to_mask(scheduler_state_.curr_cpu_), 0);
      break;
    case THREAD_SUSPENDED:
      // thread is suspended, resume it so it can get the kill signal
      local_resched = Scheduler::Unblock(this);
      break;
    case THREAD_BLOCKED:
    case THREAD_BLOCKED_READ_LOCK:
      // thread is blocked on something and marked interruptible
      wait_queue_state_.UnblockIfInterruptible(this, ZX_ERR_INTERNAL_INTR_KILLED);
      break;
    case THREAD_SLEEPING:
      // thread is sleeping
      local_resched = wait_queue_state_.UnsleepIfInterruptible(this, ZX_ERR_INTERNAL_INTR_KILLED);
      break;
    case THREAD_DEATH:
      // thread is already dead
      return;
  }

  if (local_resched) {
    // reschedule if the local cpu run queue was modified
    Scheduler::Reschedule();
  }
}

cpu_mask_t Thread::GetCpuAffinity() const {
  canary_.Assert();
  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
  return scheduler_state_.hard_affinity();
}

void Thread::SetCpuAffinity(cpu_mask_t affinity) {
  canary_.Assert();
  DEBUG_ASSERT_MSG(
      (affinity & mp_get_active_mask()) != 0,
      "Attempted to set affinity mask to %#x, which has no overlap of active CPUs %#x.", affinity,
      mp_get_active_mask());

  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

  // set the affinity mask
  scheduler_state_.hard_affinity_ = affinity;

  // let the scheduler deal with it
  Scheduler::Migrate(this);
}

void Thread::SetSoftCpuAffinity(cpu_mask_t affinity) {
  canary_.Assert();
  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

  // set the affinity mask
  scheduler_state_.soft_affinity_ = affinity;

  // let the scheduler deal with it
  Scheduler::Migrate(this);
}

cpu_mask_t Thread::GetSoftCpuAffinity() const {
  canary_.Assert();
  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
  return scheduler_state_.soft_affinity_;
}

void Thread::Current::MigrateToCpu(const cpu_num_t target_cpu) {
  Thread::Current::Get()->SetCpuAffinity(cpu_num_to_mask(target_cpu));
}

void Thread::SetMigrateFn(MigrateFn migrate_fn) {
  canary_.Assert();
  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
  SetMigrateFnLocked(ktl::move(migrate_fn));
}

void Thread::SetMigrateFnLocked(MigrateFn migrate_fn) {
  canary_.Assert();
  // If |migrate_fn_| was previously set, remove |this| from |migrate_list_|.
  if (migrate_fn_) {
    migrate_list_.erase(*this);
  }
  migrate_fn_ = ktl::move(migrate_fn);
  // If |migrate_fn_| is valid, add |this| to |migrate_list_|.
  if (migrate_fn_) {
    migrate_list_.push_front(this);
  }
}

void Thread::CallMigrateFnForCpuLocked(cpu_num_t cpu) {
  while (!migrate_list_.is_empty()) {
    Thread* const thread = migrate_list_.pop_front();

    if (thread->state_ != THREAD_READY && thread->scheduler_state().last_cpu_ == cpu) {
      thread->CallMigrateFnLocked(Thread::MigrateStage::Before);
    }
  }
}

bool Thread::CheckKillSignal() {
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(thread_lock.IsHeld());

  if (signals_ & THREAD_SIGNAL_KILL) {
    // Ensure we don't recurse into thread_exit.
    DEBUG_ASSERT(state_ != THREAD_DEATH);
    return true;
  } else {
    return false;
  }
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
    Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

    // make sure we haven't been killed while the lock was dropped for the user callback
    if (current_thread->CheckKillSignal()) {
      guard.Release();
      Thread::Current::Exit(0);
    }

    // Make sure the suspend signal wasn't cleared while we were running the
    // callback.
    if (current_thread->signals_ & THREAD_SIGNAL_SUSPEND) {
      current_thread->state_ = THREAD_SUSPENDED;
      current_thread->signals_ &= ~THREAD_SIGNAL_SUSPEND;

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
  DEBUG_ASSERT(thread_lock.IsHeld());
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
  DEBUG_ASSERT(thread_lock.IsHeld());
  DEBUG_ASSERT(this == Thread::Current::Get());
  DEBUG_ASSERT(user_thread_ != nullptr);

  DEBUG_ASSERT(user_state_saved_);
  user_state_saved_ = false;
  arch_restore_user_state(this);
}

ScopedThreadExceptionContext::ScopedThreadExceptionContext(const arch_exception_context_t* context)
    : thread_(Thread::Current::Get()), context_(context) {
  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
  // It's possible that the context and state have been installed/saved earlier in the call chain.
  // If so, then it's some other object's responsibilty to remove/restore.
  need_to_remove_ = arch_install_exception_context(thread_, context_);
  need_to_restore_ = thread_->SaveUserStateLocked();
}

ScopedThreadExceptionContext::~ScopedThreadExceptionContext() {
  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
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
  if (likely(current_thread->signals_ == 0)) {
    return;
  }

  // grab the thread lock so we can safely look at the signal mask
  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

  // This thread is about to be killed, raise an exception, or become suspended.  If this is a user
  // thread, these are all debugger-visible actions.  Save the general registers so that a debugger
  // may access them.
  const bool has_user_thread = current_thread->user_thread_ != nullptr;
  if (has_user_thread) {
    arch_set_suspended_general_regs(current_thread, source, gregs);
  }
  auto cleanup_suspended_general_regs = fbl::MakeAutoCall([current_thread, has_user_thread]() {
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
  if (has_user_thread && (current_thread->signals_ & THREAD_SIGNAL_POLICY_EXCEPTION)) {
    current_thread->signals_ &= ~THREAD_SIGNAL_POLICY_EXCEPTION;
    guard.Release();

    zx_status_t status = arch_dispatch_user_policy_exception();
    if (status != ZX_OK) {
      panic("arch_dispatch_user_policy_exception() failed: status=%d\n", status);
    }
    return;
  }

  if (current_thread->signals_ & THREAD_SIGNAL_SUSPEND) {
    DEBUG_ASSERT(current_thread->state_ == THREAD_RUNNING);
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
  DEBUG_ASSERT(current_thread->state_ == THREAD_RUNNING);
  DEBUG_ASSERT(!arch_blocking_disallowed());

  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

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
  DEBUG_ASSERT(current_thread->state_ == THREAD_RUNNING);
  DEBUG_ASSERT(!arch_blocking_disallowed());

  if (!current_thread->IsIdle()) {
    // only track when a meaningful preempt happens
    CPU_STATS_INC(irq_preempts);
  }

  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

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
  DEBUG_ASSERT(current_thread->state_ == THREAD_RUNNING);
  DEBUG_ASSERT(!arch_blocking_disallowed());

  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

  Scheduler::Reschedule();
}

void PreemptionState::CheckPreemptPending() const {
  // First check preempt_pending without the expense of taking the lock.
  // At this point, interrupts could be enabled, so an interrupt handler
  // might preempt us and set preempt_pending to false after we read it.
  if (unlikely(preempt_pending_)) {
    Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
    // Recheck preempt_pending just in case it got set to false after
    // our earlier check.  Its value now cannot change because
    // interrupts are now disabled.
    if (likely(preempt_pending_)) {
      // This will set preempt_pending = false for us.
      Scheduler::Reschedule();
    }
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

  if (state_ != THREAD_SLEEPING) {
    thread_lock.Release();
    return;
  }

  // Unblock the thread, regardless of whether the sleep was interruptible.
  const bool resched = wait_queue_state_.Unsleep(this, ZX_OK);
  if (resched) {
    Scheduler::Reschedule();
  }

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
  DEBUG_ASSERT(current_thread->state_ == THREAD_RUNNING);
  DEBUG_ASSERT(!current_thread->IsIdle());
  DEBUG_ASSERT(!arch_blocking_disallowed());

  // Skip all of the work if the deadline has already passed.
  if (deadline.when() <= now) {
    return ZX_OK;
  }

  Timer timer;

  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

  // if we've been killed and going in interruptible, abort here
  if (interruptible == Interruptible::Yes && unlikely((current_thread->signals_))) {
    if (current_thread->signals_ & THREAD_SIGNAL_KILL) {
      return ZX_ERR_INTERNAL_INTR_KILLED;
    } else {
      return ZX_ERR_INTERNAL_INTR_RETRY;
    }
  }

  // set a one shot timer to wake us up and reschedule
  timer.Set(deadline, &Thread::SleepHandler, current_thread);

  current_thread->state_ = THREAD_SLEEPING;

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
  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

  zx_duration_t runtime = scheduler_state_.runtime_ns();
  if (state_ == THREAD_RUNNING) {
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
  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
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

  cpu_num_t cpu = arch_curr_cpu_num();

  init_thread_struct(t, name);
  t->set_running();
  t->set_detached(true);

  // Setup the scheduler state before directly manipulating its members.
  Scheduler::InitializeThread(t, HIGHEST_PRIORITY);
  t->scheduler_state().curr_cpu_ = cpu;
  t->scheduler_state().last_cpu_ = cpu;
  t->scheduler_state().next_cpu_ = INVALID_CPU;
  t->scheduler_state().hard_affinity_ = cpu_num_to_mask(cpu);

  arch_thread_construct_first(t);
  arch_set_current_thread(t);

  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
  thread_list->push_front(t);
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

  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
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

  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
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
  DEBUG_ASSERT(state_ == THREAD_INITIAL);
  DEBUG_ASSERT(!user_thread_);

  user_thread_ = ktl::move(user_thread);
  user_tid_ = user_thread_->get_koid();
  user_pid_ = user_thread_->process()->get_koid();

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
  Scheduler::InitializeThread(t, IDLE_PRIORITY);

  // Pin the thread on the current cpu and mark it as already running
  t->scheduler_state_.last_cpu_ = curr_cpu;
  t->scheduler_state_.curr_cpu_ = curr_cpu;
  t->scheduler_state_.hard_affinity_ = cpu_num_to_mask(curr_cpu);

  // Cpu is active now
  mp_set_curr_cpu_active(true);

  // Grab the thread lock, mark ourself idle and reschedule
  {
    Guard<SpinLock, NoIrqSave> guard{ThreadLock::Get()};

    mp_set_cpu_idle(curr_cpu);

    Scheduler::Reschedule();
  }

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

  // At this point, the CPU isn't far enough along to allow threads to block. Set blocking
  // disallowed until to catch bugs where code might block before we're ready.
  arch_set_blocking_disallowed(true);

  // Save |this|'s stack because |thread_construct_first| will zero out the whole struct.
  KernelStack stack = ktl::move(stack_);

  char name[16];
  snprintf(name, sizeof(name), "cpu_init %u", arch_curr_cpu_num());
  thread_construct_first(this, name);

  // Restore the stack.
  stack_ = ktl::move(stack);
}

/**
 * @brief The last routine called on the secondary cpu's bootstrap thread.
 */
void thread_secondary_cpu_entry() {
  DEBUG_ASSERT(arch_blocking_disallowed());

  mp_set_curr_cpu_active(true);

  get_local_percpu()->dpc_queue.InitForCurrentCpu();

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

  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
  Scheduler::UnblockIdle(t);
  return t;
}

/**
 * @brief Return the name of the "owner" of the thread.
 *
 * Returns "kernel" if there is no owner.
 */

void Thread::OwnerName(char out_name[ZX_MAX_NAME_LEN]) {
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
  if (t->state_ == THREAD_RUNNING) {
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
            thread_state_to_str(t->state_), (int)t->scheduler_state().curr_cpu(),
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
            t->user_pid_, t->user_tid_);
    arch_dump_thread(t);
  } else {
    printf("thr %p st %4s owq %d pri %2d [%d,%d] pid %" PRIu64 " tid %" PRIu64 " (%s:%s)\n", t,
           thread_state_to_str(t->state_), !t->wait_queue_state().owned_wait_queues_.is_empty(),
           t->scheduler_state().effective_priority_, t->scheduler_state().base_priority_,
           t->scheduler_state().inherited_priority_, t->user_pid_, t->user_tid_, oname, t->name());
  }
}

void dump_thread(Thread* t, bool full) {
  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
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
  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
  dump_all_threads_locked(full);
}

void dump_thread_user_tid(zx_koid_t tid, bool full) {
  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
  dump_thread_user_tid_locked(tid, full);
}

void dump_thread_user_tid_locked(zx_koid_t tid, bool full) {
  for (Thread& t : thread_list.Get()) {
    if (t.user_tid() != tid) {
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
  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
  for (Thread& t : thread_list.Get()) {
    if (t.user_tid() == tid) {
      return &t;
    }
  }

  return nullptr;
}

/** @} */

// Used by ktrace at the start of a trace to ensure that all
// the running threads, processes, and their names are known
void ktrace_report_live_threads() {
  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
  for (Thread& t : thread_list.Get()) {
    t.canary().Assert();
    if (t.user_tid()) {
      ktrace_name(TAG_THREAD_NAME, static_cast<uint32_t>(t.user_tid()),
                  static_cast<uint32_t>(t.user_pid()), t.name());
    } else {
      ktrace_name(TAG_KTHREAD_NAME, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&t)), 0,
                  t.name());
    }
  }
}

#define THREAD_BACKTRACE_DEPTH 16
typedef struct thread_backtrace {
  void* pc[THREAD_BACKTRACE_DEPTH];
} thread_backtrace_t;

static zx_status_t thread_read_stack(Thread* t, void* ptr, void* out, size_t sz) {
  if (!is_kernel_address((uintptr_t)ptr) || (reinterpret_cast<vaddr_t>(ptr) < t->stack().base()) ||
      (reinterpret_cast<vaddr_t>(ptr) > (t->stack().top() - sizeof(void*)))) {
    return ZX_ERR_NOT_FOUND;
  }
  memcpy(out, ptr, sz);
  return ZX_OK;
}

static size_t thread_get_backtrace(Thread* t, void* fp, thread_backtrace_t* tb) {
  // without frame pointers, dont even try
  // the compiler should optimize out the body of all the callers if it's not present
  if (!WITH_FRAME_POINTERS) {
    return 0;
  }

  void* pc;
  if (t == nullptr) {
    return 0;
  }
  size_t n = 0;
  for (; n < THREAD_BACKTRACE_DEPTH; n++) {
    if (thread_read_stack(t, static_cast<char*>(fp) + 8, &pc, sizeof(void*))) {
      break;
    }
    tb->pc[n] = pc;
    if (thread_read_stack(t, fp, &fp, sizeof(void*))) {
      break;
    }
  }
  return n;
}

namespace {

constexpr const char* bt_fmt = "{{{bt:%zu:%p}}}\n";

zx_status_t thread_print_backtrace(Thread* t, void* fp) {
  if (!t || !fp) {
    return ZX_ERR_BAD_STATE;
  }

  thread_backtrace_t tb;
  size_t count = thread_get_backtrace(t, fp, &tb);
  if (count == 0) {
    return ZX_ERR_BAD_STATE;
  }

  print_backtrace_version_info();

  for (size_t n = 0; n < count; n++) {
    printf(bt_fmt, n, tb.pc[n]);
  }

  return ZX_OK;
}

}  // namespace

// Print the backtrace of the current thread, at the current spot.
void Thread::Current::PrintBacktrace() {
  thread_print_backtrace(Thread::Current::Get(), __GET_FRAME(0));
}

// Append the backtrace of the current thread to the passed in char pointer.
// Return the number of chars appended.
size_t Thread::Current::AppendBacktrace(char* out, const size_t out_len) {
  Thread* current = Thread::Current::Get();
  void* fp = __GET_FRAME(0);

  if (!current || !fp) {
    return 0;
  }

  thread_backtrace_t tb;
  size_t count = thread_get_backtrace(current, fp, &tb);
  if (count == 0) {
    return 0;
  }

  char* buf = out;
  size_t remain = out_len;
  size_t len;
  for (size_t n = 0; n < count; n++) {
    len = snprintf(buf, remain, bt_fmt, n, tb.pc[n]);
    if (len > remain) {
      return out_len;
    }
    remain -= len;
    buf += len;
  }

  return out_len - remain;
}

// Print the backtrace of the current thread, at the given spot.
void Thread::Current::PrintBacktraceAtFrame(void* caller_frame) {
  thread_print_backtrace(Thread::Current::Get(), caller_frame);
}

// Print the backtrace of a passed in thread, if possible.
zx_status_t Thread::PrintBacktrace() {
  // get the starting point if it's in a usable state
  void* fp = nullptr;
  switch (state_) {
    case THREAD_BLOCKED:
    case THREAD_BLOCKED_READ_LOCK:
    case THREAD_SLEEPING:
    case THREAD_SUSPENDED:
      // thread is blocked, so ask the arch code to get us a starting point
      fp = arch_thread_get_blocked_fp(this);
      break;
    // we can't deal with every other state
    default:
      return ZX_ERR_BAD_STATE;
  }

  return thread_print_backtrace(this, fp);
}

void Thread::UpdateRuntimeStats(const RuntimeStats& stats) {
  if (user_thread_) {
    user_thread_->UpdateRuntimeStats(stats);
  }
}
