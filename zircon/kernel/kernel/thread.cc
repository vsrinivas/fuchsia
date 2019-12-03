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
#include <lib/counters.h>
#include <lib/heap.h>
#include <lib/ktrace.h>
#include <lib/version.h>
#include <list.h>
#include <malloc.h>
#include <platform.h>
#include <printf.h>
#include <string.h>
#include <target.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <arch/exception.h>
#include <kernel/atomic.h>
#include <kernel/dpc.h>
#include <kernel/lockdep.h>
#include <kernel/mp.h>
#include <kernel/percpu.h>
#include <kernel/sched.h>
#include <kernel/stats.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <kernel/timer.h>
#include <ktl/atomic.h>
#include <lockdep/lockdep.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <vm/kstack.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>

// kernel counters. TODO(cpu): remove LK-era counters
// The counters below never decrease.
//
// counts the number of thread_t successfully created.
KCOUNTER(thread_create_count, "thread.create")
// counts the number of thread_t joined. Never decreases.
KCOUNTER(thread_join_count, "thread.join")
// counts the number of calls to suspend() that succeeded.
KCOUNTER(thread_suspend_count, "thread.suspend")
// counts the number of calls to resume() that succeeded.
KCOUNTER(thread_resume_count, "thread.resume")

// global thread list
static struct list_node thread_list = LIST_INITIAL_VALUE(thread_list);

// master thread spinlock
spin_lock_t thread_lock __CPU_ALIGN_EXCLUSIVE = SPIN_LOCK_INITIAL_VALUE;

// local routines
static void thread_exit_locked(thread_t* current_thread, int retcode) __NO_RETURN;
static void thread_do_suspend(void);

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

static void init_thread_lock_state(thread_t* t) {
#if WITH_LOCK_DEP
  auto* state = reinterpret_cast<lockdep::ThreadLockState*>(&t->lock_state);
  lockdep::SystemInitThreadLockState(state);
#endif
}

// Default constructor/destructor.
thread_t::thread_t() {}
thread_t::~thread_t() {
  DEBUG_ASSERT(blocking_wait_queue == nullptr);
  // owned_wait_queues is a fbl:: list of unmanaged pointers.  It will debug
  // assert if it is not empty when it destructs; we do not need to do so
  // here.
}

void init_thread_struct(thread_t* t, const char* name) {
  memset(t, 0, sizeof(thread_t));

  // Placement new to trigger any special construction requirements of the
  // thread_t structure.
  //
  // TODO(johngro): now that we have converted thread_t over to C++, consider
  // switching to using C++ constructors/destructors and new/delete to handle
  // all of this instead of using init_thread_struct and free_thread_resources
  new (t) thread_t();

  t->magic = THREAD_MAGIC;
  strlcpy(t->name, name, sizeof(t->name));
  wait_queue_init(&t->retcode_wait_queue);
  init_thread_lock_state(t);
  t->hard_affinity = CPU_MASK_ALL;
  t->soft_affinity = CPU_MASK_ALL;
}

static void initial_thread_func(void) TA_REQ(thread_lock) __NO_RETURN;
static void initial_thread_func(void) {
  int ret;

  // release the thread lock that was implicitly held across the reschedule
  spin_unlock(&thread_lock);
  arch_enable_ints();

  thread_t* ct = get_current_thread();
  ret = (ct->arg) ? ct->entry(ct->arg) : ct->entry(ct->user_thread);

  thread_exit(ret);
}

/**
 * @brief  Create a new thread
 *
 * This function creates a new thread.  The thread is initially suspended, so you
 * need to call thread_resume() to execute it.
 *
 * @param  t               If not NULL, use the supplied thread_t
 * @param  name            Name of thread
 * @param  entry           Entry point of thread
 * @param  arg             Arbitrary argument passed to entry(). It can be null.
 *                         in which case |user_thread| will be used.
 * @param  priority        Execution priority for the thread.
 * @param  alt_trampoline  If not NULL, an alternate trampoline for the thread
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
 * @return  Pointer to thread object, or NULL on failure.
 */
thread_t* thread_create_etc(thread_t* t, const char* name, thread_start_routine entry, void* arg,
                            int priority, thread_trampoline_routine alt_trampoline) {
  unsigned int flags = 0;

  if (!t) {
    t = static_cast<thread_t*>(malloc(sizeof(thread_t)));
    if (!t) {
      return NULL;
    }
    flags |= THREAD_FLAG_FREE_STRUCT;
  }

  init_thread_struct(t, name);

  t->entry = entry;
  t->arg = arg;
  t->state = THREAD_INITIAL;
  t->signals = 0;
  t->blocked_status = ZX_OK;
  t->interruptable = false;
  t->curr_cpu = INVALID_CPU;
  t->last_cpu = INVALID_CPU;

  t->retcode = 0;
  wait_queue_init(&t->retcode_wait_queue);

  sched_init_thread(t, priority);

  zx_status_t status = vm_allocate_kstack(&t->stack);
  if (status != ZX_OK) {
    if (flags & THREAD_FLAG_FREE_STRUCT) {
      free(t);
    }
    return nullptr;
  }

  // save whether or not we need to free the thread struct and/or stack
  t->flags = flags;

  if (likely(alt_trampoline == NULL)) {
    alt_trampoline = initial_thread_func;
  }

  // set up the initial stack frame
  arch_thread_initialize(t, (vaddr_t)alt_trampoline);

  // add it to the global thread list
  {
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
    list_add_head(&thread_list, &t->thread_list_node);
  }

  kcounter_add(thread_create_count, 1);
  return t;
}

thread_t* thread_create(const char* name, thread_start_routine entry, void* arg, int priority) {
  return thread_create_etc(NULL, name, entry, arg, priority, NULL);
}

static void free_thread_resources(thread_t* t) {
  if (t->stack.vmar != nullptr) {
#if __has_feature(safe_stack)
    DEBUG_ASSERT(t->stack.unsafe_vmar != nullptr);
#endif
#if __has_feature(shadow_call_stack)
    DEBUG_ASSERT(t->stack.shadow_call_vmar != nullptr);
#endif
    zx_status_t status = vm_free_kstack(&t->stack);
    DEBUG_ASSERT(status == ZX_OK);
  }

  // call the tls callback for each slot as long there is one
  for (uint ix = 0; ix != THREAD_MAX_TLS_ENTRY; ++ix) {
    if (t->tls_callback[ix]) {
      t->tls_callback[ix](t->tls[ix]);
    }
  }

  // free the thread structure itself.  Manually trigger the struct's
  // destructor so that DEBUG_ASSERTs present in the owned_wait_queues member
  // get triggered.
  bool thread_needs_free = (t->flags & THREAD_FLAG_FREE_STRUCT) != 0;
  t->magic = 0;
  t->~thread_t();
  if (thread_needs_free) {
    free(t);
  }
}

/**
 * @brief Flag a thread as real time
 *
 * @param t Thread to flag
 *
 * @return ZX_OK on success
 */
zx_status_t thread_set_real_time(thread_t* t) {
  if (!t) {
    return ZX_ERR_INVALID_ARGS;
  }

  DEBUG_ASSERT(t->magic == THREAD_MAGIC);

  {
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
    if (t == get_current_thread()) {
      // if we're currently running, cancel the preemption timer.
      timer_preempt_cancel();
    }
    t->flags |= THREAD_FLAG_REAL_TIME;
  }

  return ZX_OK;
}

/**
 * @brief  Make a suspended thread executable.
 *
 * This function is called to start a thread which has just been
 * created with thread_create() or which has been suspended with
 * thread_suspend(). It can not fail.
 *
 * @param t  Thread to resume
 */
void thread_resume(thread_t* t) {
  DEBUG_ASSERT(t->magic == THREAD_MAGIC);

  bool ints_disabled = arch_ints_disabled();
  bool resched = false;
  if (!ints_disabled) {  // HACK, don't resced into bootstrap thread before idle thread is set up
    resched = true;
  }

  {
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

    if (t->state == THREAD_DEATH) {
      // The thread is dead, resuming it is a no-op.
      return;
    }

    // Clear the suspend signal in case there is a pending suspend
    t->signals &= ~THREAD_SIGNAL_SUSPEND;

    if (t->state == THREAD_INITIAL || t->state == THREAD_SUSPENDED) {
      // wake up the new thread, putting it in a run queue on a cpu. reschedule if the local
      // cpu run queue was modified
      bool local_resched = sched_unblock(t);
      if (resched && local_resched) {
        sched_reschedule();
      }
    }
  }

  kcounter_add(thread_resume_count, 1);
}

zx_status_t thread_detach_and_resume(thread_t* t) {
  zx_status_t status = thread_detach(t);
  if (status != ZX_OK) {
    return status;
  }
  thread_resume(t);
  return ZX_OK;
}

/**
 * @brief  Suspend an initialized/ready/running thread
 *
 * @param t  Thread to suspend
 *
 * @return ZX_OK on success, ZX_ERR_BAD_STATE if the thread is dead
 */
zx_status_t thread_suspend(thread_t* t) {
  DEBUG_ASSERT(t->magic == THREAD_MAGIC);
  DEBUG_ASSERT(!thread_is_idle(t));

  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

  if (t->state == THREAD_DEATH) {
    return ZX_ERR_BAD_STATE;
  }

  t->signals |= THREAD_SIGNAL_SUSPEND;

  bool local_resched = false;
  switch (t->state) {
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
      local_resched = sched_unblock(t);
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
      mp_reschedule(cpu_num_to_mask(t->curr_cpu), 0);
      break;
    case THREAD_SUSPENDED:
      // thread is suspended already
      break;
    case THREAD_BLOCKED:
    case THREAD_BLOCKED_READ_LOCK:
      // thread is blocked on something and marked interruptable
      if (t->interruptable) {
        wait_queue_unblock_thread(t, ZX_ERR_INTERNAL_INTR_RETRY);
      }
      break;
    case THREAD_SLEEPING:
      // thread is sleeping
      if (t->interruptable) {
        t->blocked_status = ZX_ERR_INTERNAL_INTR_RETRY;

        local_resched = sched_unblock(t);
      }
      break;
  }

  // reschedule if the local cpu run queue was modified
  if (local_resched) {
    sched_reschedule();
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
void thread_signal_policy_exception(void) {
  thread_t* t = get_current_thread();
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  t->signals |= THREAD_SIGNAL_POLICY_EXCEPTION;
}

zx_status_t thread_join(thread_t* t, int* retcode, zx_time_t deadline) {
  DEBUG_ASSERT(t->magic == THREAD_MAGIC);

  {
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

    if (t->flags & THREAD_FLAG_DETACHED) {
      // the thread is detached, go ahead and exit
      return ZX_ERR_BAD_STATE;
    }

    // wait for the thread to die
    if (t->state != THREAD_DEATH) {
      zx_status_t status = wait_queue_block(&t->retcode_wait_queue, deadline);
      if (status != ZX_OK) {
        return status;
      }
    }

    DEBUG_ASSERT(t->magic == THREAD_MAGIC);
    DEBUG_ASSERT(t->state == THREAD_DEATH);
    DEBUG_ASSERT(t->blocking_wait_queue == NULL);
    DEBUG_ASSERT(!list_in_list(&t->queue_node));

    // save the return code
    if (retcode) {
      *retcode = t->retcode;
    }

    // remove it from the master thread list
    list_delete(&t->thread_list_node);

    // clear the structure's magic
    t->magic = 0;
  }

  free_thread_resources(t);

  kcounter_add(thread_join_count, 1);

  return ZX_OK;
}

zx_status_t thread_detach(thread_t* t) {
  DEBUG_ASSERT(t->magic == THREAD_MAGIC);

  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

  // if another thread is blocked inside thread_join() on this thread,
  // wake them up with a specific return code
  wait_queue_wake_all(&t->retcode_wait_queue, false, ZX_ERR_BAD_STATE);

  // if it's already dead, then just do what join would have and exit
  if (t->state == THREAD_DEATH) {
    t->flags &= ~THREAD_FLAG_DETACHED;  // makes sure thread_join continues
    guard.Release();
    return thread_join(t, NULL, 0);
  } else {
    t->flags |= THREAD_FLAG_DETACHED;
    return ZX_OK;
  }
}

// called back in the DPC worker thread to free the stack and/or the thread structure
// itself for a thread that is exiting on its own.
static void thread_free_dpc(struct dpc* dpc) {
  thread_t* t = (thread_t*)dpc->arg;

  DEBUG_ASSERT(t->magic == THREAD_MAGIC);
  DEBUG_ASSERT(t->state == THREAD_DEATH);

  // grab and release the thread lock, which effectively serializes us with
  // the thread that is queuing itself for destruction.
  {
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
  }

  free_thread_resources(t);
}

__NO_RETURN static void thread_exit_locked(thread_t* current_thread, int retcode)
    TA_REQ(thread_lock) {
  // create a dpc on the stack to queue up a free.
  // must be put at top scope in this function to force the compiler to keep it from
  // reusing the stack before the function exits
  dpc_t free_dpc = DPC_INITIAL_VALUE;

  // enter the dead state
  current_thread->state = THREAD_DEATH;
  current_thread->retcode = retcode;

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
  if (current_thread->flags & THREAD_FLAG_DETACHED) {
    // remove it from the master thread list
    list_delete(&current_thread->thread_list_node);

    // queue a dpc to free the stack and, optionally, the thread structure
    if (current_thread->stack.base || (current_thread->flags & THREAD_FLAG_FREE_STRUCT)) {
      free_dpc.func = thread_free_dpc;
      free_dpc.arg = (void*)current_thread;
      zx_status_t status = dpc_queue_thread_locked(&free_dpc);
      DEBUG_ASSERT(status == ZX_OK);
    }
  } else {
    // signal if anyone is waiting
    wait_queue_wake_all(&current_thread->retcode_wait_queue, false, 0);
  }

  // reschedule
  sched_resched_internal();

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
void thread_forget(thread_t* t) {
  {
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

    __UNUSED thread_t* current_thread = get_current_thread();
    DEBUG_ASSERT(current_thread != t);

    list_delete(&t->thread_list_node);
  }

  DEBUG_ASSERT(!list_in_list(&t->queue_node));

  free_thread_resources(t);
}

/**
 * @brief  Terminate the current thread
 *
 * Current thread exits with the specified return code.
 *
 * This function does not return.
 */
void thread_exit(int retcode) {
  thread_t* current_thread = get_current_thread();

  DEBUG_ASSERT(current_thread->magic == THREAD_MAGIC);
  DEBUG_ASSERT(current_thread->state == THREAD_RUNNING);
  DEBUG_ASSERT(!thread_is_idle(current_thread));

  if (current_thread->user_thread) {
    DEBUG_ASSERT(!arch_ints_disabled() || !spin_lock_held(&thread_lock));
    current_thread->user_thread->Exiting();
  }

  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  thread_exit_locked(current_thread, retcode);
}

// kill a thread
void thread_kill(thread_t* t) {
  DEBUG_ASSERT(t->magic == THREAD_MAGIC);

  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

  // deliver a signal to the thread.
  // NOTE: it's not important to do this atomically, since we're inside
  // the thread lock, but go ahead and flush it out to memory to avoid the amount
  // of races if another thread is looking at this.
  t->signals |= THREAD_SIGNAL_KILL;
  smp_mb();

  bool local_resched = false;

  // we are killing ourself
  if (t == get_current_thread()) {
    return;
  }

  // general logic is to wake up the thread so it notices it had a signal delivered to it

  switch (t->state) {
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
      mp_reschedule(cpu_num_to_mask(t->curr_cpu), 0);
      break;
    case THREAD_SUSPENDED:
      // thread is suspended, resume it so it can get the kill signal
      local_resched = sched_unblock(t);
      break;
    case THREAD_BLOCKED:
    case THREAD_BLOCKED_READ_LOCK:
      // thread is blocked on something and marked interruptable
      if (t->interruptable) {
        wait_queue_unblock_thread(t, ZX_ERR_INTERNAL_INTR_KILLED);
      }
      break;
    case THREAD_SLEEPING:
      // thread is sleeping
      if (t->interruptable) {
        t->blocked_status = ZX_ERR_INTERNAL_INTR_KILLED;

        local_resched = sched_unblock(t);
      }
      break;
    case THREAD_DEATH:
      // thread is already dead
      return;
  }

  if (local_resched) {
    // reschedule if the local cpu run queue was modified
    sched_reschedule();
  }
}

cpu_mask_t thread_get_cpu_affinity(const thread_t* t) {
  DEBUG_ASSERT(t->magic == THREAD_MAGIC);
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  return t->hard_affinity;
}

void thread_set_cpu_affinity(thread_t* t, cpu_mask_t affinity) {
  DEBUG_ASSERT(t->magic == THREAD_MAGIC);
  DEBUG_ASSERT_MSG(
      (affinity & mp_get_active_mask()) != 0,
      "Attempted to set affinity mask to %#x, which has no overlap of active CPUs %#x.", affinity,
      mp_get_active_mask());

  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

  // set the affinity mask
  t->hard_affinity = affinity;

  // let the scheduler deal with it
  sched_migrate(t);
}

void thread_set_soft_cpu_affinity(thread_t* t, cpu_mask_t affinity) {
  DEBUG_ASSERT(t->magic == THREAD_MAGIC);
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

  // set the affinity mask
  t->soft_affinity = affinity;

  // let the scheduler deal with it
  sched_migrate(t);
}

cpu_mask_t thread_get_soft_cpu_affinity(const thread_t* t) {
  DEBUG_ASSERT(t->magic == THREAD_MAGIC);
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  return t->soft_affinity;
}

void thread_migrate_to_cpu(const cpu_num_t target_cpu) {
  thread_set_cpu_affinity(get_current_thread(), cpu_num_to_mask(target_cpu));
}

// Returns true if it decides to kill the thread. The thread_lock must be held
// when calling this function.
static bool check_kill_signal(thread_t* current_thread) TA_REQ(thread_lock) {
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(spin_lock_held(&thread_lock));

  if (current_thread->signals & THREAD_SIGNAL_KILL) {
    // Ensure we don't recurse into thread_exit.
    DEBUG_ASSERT(current_thread->state != THREAD_DEATH);
    return true;
  } else {
    return false;
  }
}

// finish suspending the current thread
static void thread_do_suspend(void) {
  thread_t* current_thread = get_current_thread();
  // Note: After calling this callback, we must not return without
  // calling the callback with THREAD_USER_STATE_RESUME.  That is
  // because those callbacks act as barriers which control when it is
  // safe for the zx_thread_read_state()/zx_thread_write_state()
  // syscalls to access the userland register state kept by thread_t.
  if (current_thread->user_thread) {
    DEBUG_ASSERT(!arch_ints_disabled() || !spin_lock_held(&thread_lock));
    current_thread->user_thread->Suspending();
  }

  {
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

    // make sure we haven't been killed while the lock was dropped for the user callback
    if (check_kill_signal(current_thread)) {
      guard.Release();
      thread_exit(0);
    }

    // Make sure the suspend signal wasn't cleared while we were running the
    // callback.
    if (current_thread->signals & THREAD_SIGNAL_SUSPEND) {
      current_thread->state = THREAD_SUSPENDED;
      current_thread->signals &= ~THREAD_SIGNAL_SUSPEND;

      // directly invoke the context switch, since we've already manipulated this thread's state
      sched_resched_internal();

      // If the thread was killed, we should not allow it to resume.  We
      // shouldn't call user_callback() with THREAD_USER_STATE_RESUME in
      // this case, because there might not have been any request to
      // resume the thread.
      if (check_kill_signal(current_thread)) {
        guard.Release();
        thread_exit(0);
      }
    }
  }

  if (current_thread->user_thread) {
    DEBUG_ASSERT(!arch_ints_disabled() || !spin_lock_held(&thread_lock));
    current_thread->user_thread->Resuming();
  }
}

// check for any pending signals and handle them
void thread_process_pending_signals(void) {
  thread_t* current_thread = get_current_thread();
  if (likely(current_thread->signals == 0)) {
    return;
  }

  // grab the thread lock so we can safely look at the signal mask
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  if (check_kill_signal(current_thread)) {
    guard.Release();
    thread_exit(0);
  }

  // Report exceptions raised by syscalls
  if (current_thread->signals & THREAD_SIGNAL_POLICY_EXCEPTION) {
    current_thread->signals &= ~THREAD_SIGNAL_POLICY_EXCEPTION;
    guard.Release();

    zx_status_t status = arch_dispatch_user_policy_exception();
    if (status != ZX_OK) {
      panic("arch_dispatch_user_policy_exception() failed: status=%d\n", status);
    }
    return;
  }

  if (current_thread->signals & THREAD_SIGNAL_SUSPEND) {
    // transition the thread to the suspended state
    DEBUG_ASSERT(current_thread->state == THREAD_RUNNING);
    guard.Release();

    thread_do_suspend();
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
void thread_yield(void) {
  __UNUSED thread_t* current_thread = get_current_thread();

  DEBUG_ASSERT(current_thread->magic == THREAD_MAGIC);
  DEBUG_ASSERT(current_thread->state == THREAD_RUNNING);
  DEBUG_ASSERT(!arch_blocking_disallowed());

  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

  CPU_STATS_INC(yields);
  sched_yield();
}

/**
 * @brief Preempt the current thread from an interrupt
 *
 * This function places the current thread at the head of the run
 * queue and then yields the cpu to another thread.
 */
void thread_preempt(void) {
  thread_t* current_thread = get_current_thread();

  DEBUG_ASSERT(current_thread->magic == THREAD_MAGIC);
  DEBUG_ASSERT(current_thread->state == THREAD_RUNNING);
  DEBUG_ASSERT(!arch_blocking_disallowed());

  if (!thread_is_idle(current_thread)) {
    // only track when a meaningful preempt happens
    CPU_STATS_INC(irq_preempts);
  }

  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

  sched_preempt();
}

/**
 * @brief Reevaluate the run queue on the current cpu.
 *
 * This function places the current thread at the head of the run
 * queue and then yields the cpu to another thread. Similar to
 * thread_preempt, but intended to be used at non interrupt context.
 */
void thread_reschedule(void) {
  thread_t* current_thread = get_current_thread();

  DEBUG_ASSERT(current_thread->magic == THREAD_MAGIC);
  DEBUG_ASSERT(current_thread->state == THREAD_RUNNING);
  DEBUG_ASSERT(!arch_blocking_disallowed());

  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

  sched_reschedule();
}

void thread_check_preempt_pending(void) {
  thread_t* current_thread = get_current_thread();

  // First check preempt_pending without the expense of taking the lock.
  // At this point, interrupts could be enabled, so an interrupt handler
  // might preempt us and set preempt_pending to false after we read it.
  if (unlikely(current_thread->preempt_pending)) {
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
    // Recheck preempt_pending just in case it got set to false after
    // our earlier check.  Its value now cannot change because
    // interrupts are now disabled.
    if (likely(current_thread->preempt_pending)) {
      // This will set preempt_pending = false for us.
      sched_reschedule();
    }
  }
}

// timer callback to wake up a sleeping thread
static void thread_sleep_handler(timer_t* timer, zx_time_t now, void* arg) {
  thread_t* t = (thread_t*)arg;

  DEBUG_ASSERT(t->magic == THREAD_MAGIC);

  // spin trylocking on the thread lock since the routine that set up the callback,
  // thread_sleep_etc, may be trying to simultaneously cancel this timer while holding the
  // thread_lock.
  if (timer_trylock_or_cancel(timer, &thread_lock)) {
    return;
  }

  if (t->state != THREAD_SLEEPING) {
    spin_unlock(&thread_lock);
    return;
  }

  t->blocked_status = ZX_OK;

  // unblock the thread
  if (sched_unblock(t)) {
    sched_reschedule();
  }

  spin_unlock(&thread_lock);
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
  return MAX(MIN_SLEEP_SLACK, MIN(slack, MAX_SLEEP_SLACK));
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
 * interruptable argument allows this routine to return early if the thread was signaled
 * for something.
 */
zx_status_t thread_sleep_etc(const Deadline& deadline, bool interruptable, zx_time_t now) {
  thread_t* current_thread = get_current_thread();

  DEBUG_ASSERT(current_thread->magic == THREAD_MAGIC);
  DEBUG_ASSERT(current_thread->state == THREAD_RUNNING);
  DEBUG_ASSERT(!thread_is_idle(current_thread));
  DEBUG_ASSERT(!arch_blocking_disallowed());

  // Skip all of the work if the deadline has already passed.
  if (deadline.when() <= now) {
    return ZX_OK;
  }

  timer_t timer;
  timer_init(&timer);

  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

  // if we've been killed and going in interruptable, abort here
  if (interruptable && unlikely((current_thread->signals))) {
    if (current_thread->signals & THREAD_SIGNAL_KILL) {
      return ZX_ERR_INTERNAL_INTR_KILLED;
    } else {
      return ZX_ERR_INTERNAL_INTR_RETRY;
    }
  }

  // set a one shot timer to wake us up and reschedule
  timer_set(&timer, deadline, thread_sleep_handler, current_thread);

  current_thread->state = THREAD_SLEEPING;
  current_thread->blocked_status = ZX_OK;

  current_thread->interruptable = interruptable;
  sched_block();
  current_thread->interruptable = false;

  // always cancel the timer, since we may be racing with the timer tick on other cpus
  timer_cancel(&timer);

  return current_thread->blocked_status;
}

zx_status_t thread_sleep(zx_time_t deadline) {
  const zx_time_t now = current_time();
  return thread_sleep_etc(Deadline::no_slack(deadline), false, now);
}

zx_status_t thread_sleep_relative(zx_duration_t delay) {
  const zx_time_t now = current_time();
  const Deadline deadline = Deadline::no_slack(zx_time_add_duration(now, delay));
  return thread_sleep_etc(deadline, false, now);
}

zx_status_t thread_sleep_interruptable(zx_time_t deadline) {
  const zx_time_t now = current_time();
  const TimerSlack slack(sleep_slack(deadline, now), TIMER_SLACK_LATE);
  const Deadline slackDeadline(deadline, slack);
  return thread_sleep_etc(slackDeadline, true, now);
}

/**
 * @brief Return the number of nanoseconds a thread has been running for.
 *
 * This takes the thread_lock to ensure there are no races while calculating the
 * runtime of the thread.
 */
zx_duration_t thread_runtime(const thread_t* t) {
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};

  zx_duration_t runtime = t->runtime_ns;
  if (t->state == THREAD_RUNNING) {
    zx_duration_t recent = zx_time_sub_time(current_time(), t->last_started_running);
    runtime = zx_duration_add_duration(runtime, recent);
  }

  return runtime;
}

/**
 * @brief Get the last CPU the given thread was run on, or INVALID_CPU if the
 * thread has never run.
 */
cpu_num_t thread_last_cpu(const thread_t* t) {
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  return t->last_cpu;
}

/**
 * @brief Construct a thread t around the current running state
 *
 * This should be called once per CPU initialization.  It will create
 * a thread that is pinned to the current CPU and running at the
 * highest priority.
 */
void thread_construct_first(thread_t* t, const char* name) {
  DEBUG_ASSERT(arch_ints_disabled());

  cpu_num_t cpu = arch_curr_cpu_num();

  init_thread_struct(t, name);
  t->state = THREAD_RUNNING;
  t->flags = THREAD_FLAG_DETACHED;
  t->signals = 0;
  t->curr_cpu = cpu;
  t->last_cpu = cpu;
  t->hard_affinity = cpu_num_to_mask(cpu);
  sched_init_thread(t, HIGHEST_PRIORITY);

  arch_thread_construct_first(t);
  set_current_thread(t);

  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  list_add_head(&thread_list, &t->thread_list_node);
}

/**
 * @brief  Initialize threading system
 *
 * This function is called once, from kmain()
 */
void thread_init_early(void) {
  DEBUG_ASSERT(arch_curr_cpu_num() == 0);

  // Init the boot percpu data.
  percpu::InitializeBoot();

  // create a thread to cover the current running state
  thread_t* t = &percpu::Get(0).idle_thread;
  thread_construct_first(t, "bootstrap");
}

/**
 * @brief Change name of current thread
 */
void thread_set_name(const char* name) {
  thread_t* current_thread = get_current_thread();
  strlcpy(current_thread->name, name, sizeof(current_thread->name));
}

/**
 * @brief Change priority of current thread
 *
 * Sets the thread to use the fair scheduling discipline using the given
 * priority.
 *
 * See thread_create() for a discussion of priority values.
 */
void thread_set_priority(thread_t* t, int priority) {
  DEBUG_ASSERT(t->magic == THREAD_MAGIC);
  ASSERT(priority >= LOWEST_PRIORITY && priority <= HIGHEST_PRIORITY);

  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  sched_change_priority(t, priority);
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
void thread_set_deadline(thread_t* t, const zx_sched_deadline_params_t& params) {
  DEBUG_ASSERT(t->magic == THREAD_MAGIC);
  ASSERT(params.capacity > 0 && params.capacity <= params.relative_deadline &&
         params.relative_deadline <= params.period);

  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  sched_change_deadline(t, params);
}

/**
* @brief Set the pointer to the user-mode thread, this will receive callbacks:
* ThreadDispatcher::Exiting()
* ThreadDispatcher::Suspending() / Resuming()
*/
void thread_set_usermode_thread(thread_t* t, ThreadDispatcher* user_thread) {
  DEBUG_ASSERT(t->magic == THREAD_MAGIC);
  DEBUG_ASSERT(t->state == THREAD_INITIAL);
  t->user_thread = user_thread;
}


/**
 * @brief  Become an idle thread
 *
 * This function marks the current thread as the idle thread -- the one which
 * executes when there is nothing else to do.  This function does not return.
 * This thread is called once at boot on the first cpu.
 */
void thread_become_idle(void) {
  DEBUG_ASSERT(arch_ints_disabled());

  thread_t* t = get_current_thread();
  cpu_num_t curr_cpu = arch_curr_cpu_num();

  // Set our name
  char name[16];
  snprintf(name, sizeof(name), "idle %u", curr_cpu);
  thread_set_name(name);

  // Mark ourself as idle
  t->flags |= THREAD_FLAG_IDLE;
  sched_init_thread(t, IDLE_PRIORITY);

  // Pin the thread on the current cpu and mark it as already running
  t->last_cpu = curr_cpu;
  t->curr_cpu = curr_cpu;
  t->hard_affinity = cpu_num_to_mask(curr_cpu);

  // Cpu is active now
  mp_set_curr_cpu_active(true);

  // Grab the thread lock, mark ourself idle and reschedule
  {
    Guard<spin_lock_t, NoIrqSave> guard{ThreadLock::Get()};

    mp_set_cpu_idle(curr_cpu);

    sched_reschedule();
  }

  // We're now properly in the idle routine. Reenable interrupts and drop
  // into the idle routine, never return.
  arch_enable_ints();
  arch_idle_thread_routine(NULL);

  __UNREACHABLE;
}

/**
 * @brief Create a thread around the current execution context, preserving |t|'s stack
 *
 * Prior to calling, |t->stack| must be properly constructed. See |vm_allocate_kstack|.
 */
void thread_secondary_cpu_init_early(thread_t* t) {
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(t->stack.base != 0);

  // Save |t|'s stack because |thread_construct_first| will zero out the whole struct.
  kstack_t stack = t->stack;

  char name[16];
  snprintf(name, sizeof(name), "cpu_init %u", arch_curr_cpu_num());
  thread_construct_first(t, name);

  // Restore the stack.
  t->stack = stack;
}

/**
 * @brief The last routine called on the secondary cpu's bootstrap thread.
 */
void thread_secondary_cpu_entry(void) {
  mp_set_curr_cpu_active(true);

  dpc_init_for_cpu();

  // Exit from our bootstrap thread, and enter the scheduler on this cpu
  thread_exit(0);
}

/**
 * @brief Create an idle thread for a secondary CPU
 */
thread_t* thread_create_idle_thread(cpu_num_t cpu_num) {
  DEBUG_ASSERT(cpu_num != 0 && cpu_num < SMP_MAX_CPUS);

  // Shouldn't be initialized yet
  // ZX-3672: if the idle thread appears initialized, dump some data
  // around it
  if (unlikely(percpu::Get(cpu_num).idle_thread.magic != 0)) {
    platform_panic_start();
    hexdump(&percpu::Get(cpu_num).idle_thread, 256);
    panic("ZX-3672: detected non zeroed idle thread for core %u\n", cpu_num);
  }

  char name[16];
  snprintf(name, sizeof(name), "idle %u", cpu_num);

  thread_t* t = thread_create_etc(&percpu::Get(cpu_num).idle_thread, name, arch_idle_thread_routine,
                                  NULL, IDLE_PRIORITY, NULL);
  if (t == NULL) {
    return t;
  }
  t->flags |= THREAD_FLAG_IDLE | THREAD_FLAG_DETACHED;
  t->hard_affinity = cpu_num_to_mask(cpu_num);

  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  sched_unblock_idle(t);
  return t;
}

/**
 * @brief Return the name of the "owner" of the thread.
 *
 * Returns "kernel" if there is no owner.
 */

void thread_owner_name(thread_t* t, char out_name[THREAD_NAME_LENGTH]) {
  if (t->user_thread) {
    t->user_thread->process()->get_name(out_name);
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
void dump_thread_locked(thread_t* t, bool full_dump) {
  if (t->magic != THREAD_MAGIC) {
    dprintf(INFO, "dump_thread WARNING: thread at %p has bad magic\n", t);
  }

  zx_duration_t runtime = t->runtime_ns;
  if (t->state == THREAD_RUNNING) {
    zx_duration_t recent = zx_time_sub_time(current_time(), t->last_started_running);
    runtime = zx_duration_add_duration(runtime, recent);
  }

  char oname[THREAD_NAME_LENGTH];
  thread_owner_name(t, oname);

  if (full_dump) {
    dprintf(INFO, "dump_thread: t %p (%s:%s)\n", t, oname, t->name);
    dprintf(INFO,
            "\tstate %s, curr/last cpu %d/%d, hard_affinity %#x, soft_cpu_affinty %#x, "
            "priority %d [%d:%d,%d], remaining time slice %" PRIi64 "\n",
            thread_state_to_str(t->state), (int)t->curr_cpu, (int)t->last_cpu, t->hard_affinity,
            t->soft_affinity, t->effec_priority, t->base_priority, t->priority_boost,
            t->inherited_priority, t->remaining_time_slice);
    dprintf(INFO, "\truntime_ns %" PRIi64 ", runtime_s %" PRIi64 "\n", runtime,
            runtime / 1000000000);
    dprintf(INFO, "\tstack.base 0x%lx, stack.vmar %p, stack.size %zu\n", t->stack.base,
            t->stack.vmar, t->stack.size);
#if __has_feature(safe_stack)
    dprintf(INFO, "\tstack.unsafe_base 0x%lx, stack.unsafe_vmar %p\n", t->stack.unsafe_base,
            t->stack.unsafe_vmar);
#endif
#if __has_feature(shadow_call_stack)
    dprintf(INFO, "\tstack.shadow_call_base 0x%lx, stack.shadow_call_vmar %p\n",
            t->stack.shadow_call_base, t->stack.shadow_call_vmar);
#endif
    dprintf(INFO, "\tentry %p, arg %p, flags 0x%x %s%s%s%s\n", t->entry, t->arg, t->flags,
            (t->flags & THREAD_FLAG_DETACHED) ? "Dt" : "",
            (t->flags & THREAD_FLAG_FREE_STRUCT) ? "Ft" : "",
            (t->flags & THREAD_FLAG_REAL_TIME) ? "Rt" : "",
            (t->flags & THREAD_FLAG_IDLE) ? "Id" : "");

    dprintf(INFO, "\twait queue %p, blocked_status %d, interruptable %d, wait queues owned %s\n",
            t->blocking_wait_queue, t->blocked_status, t->interruptable,
            t->owned_wait_queues.is_empty() ? "no" : "yes");

    dprintf(INFO, "\taspace %p\n", t->aspace);
    dprintf(INFO, "\tuser_thread %p, pid %" PRIu64 ", tid %" PRIu64 "\n", t->user_thread,
            t->user_pid, t->user_tid);
    arch_dump_thread(t);
  } else {
    printf("thr %p st %4s owq %d pri %2d [%d:%d,%d] pid %" PRIu64 " tid %" PRIu64 " (%s:%s)\n", t,
           thread_state_to_str(t->state), !t->owned_wait_queues.is_empty(), t->effec_priority,
           t->base_priority, t->priority_boost, t->inherited_priority, t->user_pid, t->user_tid,
           oname, t->name);
  }
}

void dump_thread(thread_t* t, bool full) {
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  dump_thread_locked(t, full);
}

/**
 * @brief  Dump debugging info about all threads
 */
void dump_all_threads_locked(bool full) {
  thread_t* t;

  list_for_every_entry (&thread_list, t, thread_t, thread_list_node) {
    if (t->magic != THREAD_MAGIC) {
      dprintf(INFO, "bad magic on thread struct %p, aborting.\n", t);
      hexdump(t, sizeof(thread_t));
      break;
    }
    dump_thread_locked(t, full);
  }
}

void dump_all_threads(bool full) {
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  dump_all_threads_locked(full);
}

void dump_thread_user_tid(uint64_t tid, bool full) {
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  dump_thread_user_tid_locked(tid, full);
}

void dump_thread_user_tid_locked(uint64_t tid, bool full) {
  thread_t* t;

  list_for_every_entry (&thread_list, t, thread_t, thread_list_node) {
    if (t->user_tid != tid) {
      continue;
    }

    if (t->magic != THREAD_MAGIC) {
      dprintf(INFO, "bad magic on thread struct %p, aborting.\n", t);
      hexdump(t, sizeof(thread_t));
      break;
    }
    dump_thread_locked(t, full);
  }
}

thread_t* thread_id_to_thread_slow(uint64_t tid) {
  thread_t* t;
  list_for_every_entry (&thread_list, t, thread_t, thread_list_node) {
    if (t->user_tid == tid) {
      return t;
    }
  }

  return NULL;
}

/** @} */

// Used by ktrace at the start of a trace to ensure that all
// the running threads, processes, and their names are known
void ktrace_report_live_threads(void) {
  thread_t* t;

  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  list_for_every_entry (&thread_list, t, thread_t, thread_list_node) {
    DEBUG_ASSERT(t->magic == THREAD_MAGIC);
    if (t->user_tid) {
      ktrace_name(TAG_THREAD_NAME, static_cast<uint32_t>(t->user_tid),
                  static_cast<uint32_t>(t->user_pid), t->name);
    } else {
      ktrace_name(TAG_KTHREAD_NAME, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(t)), 0,
                  t->name);
    }
  }
}

#define THREAD_BACKTRACE_DEPTH 16
typedef struct thread_backtrace {
  void* pc[THREAD_BACKTRACE_DEPTH];
} thread_backtrace_t;

static zx_status_t thread_read_stack(thread_t* t, void* ptr, void* out, size_t sz) {
  if (!is_kernel_address((uintptr_t)ptr) || (reinterpret_cast<vaddr_t>(ptr) < t->stack.base) ||
      (reinterpret_cast<vaddr_t>(ptr) > (t->stack.base + t->stack.size - sizeof(void*)))) {
    return ZX_ERR_NOT_FOUND;
  }
  memcpy(out, ptr, sz);
  return ZX_OK;
}

static size_t thread_get_backtrace(thread_t* t, void* fp, thread_backtrace_t* tb) {
  // without frame pointers, dont even try
  // the compiler should optimize out the body of all the callers if it's not present
  if (!WITH_FRAME_POINTERS) {
    return 0;
  }

  void* pc;
  if (t == NULL) {
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
}

static zx_status_t _thread_print_backtrace(thread_t* t, void* fp) {
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

// print the backtrace of the current thread, at the current spot
void thread_print_current_backtrace(void) {
  _thread_print_backtrace(get_current_thread(), __GET_FRAME(0));
}

// append the backtrace of the current thread to the passed in char pointer.
// return the number of chars appended.
size_t thread_append_current_backtrace(char* out, const size_t out_len) {
  thread_t* current = get_current_thread();
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

// print the backtrace of the current thread, at the given spot
void thread_print_current_backtrace_at_frame(void* caller_frame) {
  _thread_print_backtrace(get_current_thread(), caller_frame);
}

// print the backtrace of a passed in thread, if possible
zx_status_t thread_print_backtrace(thread_t* t) {
  // get the starting point if it's in a usable state
  void* fp = NULL;
  switch (t->state) {
    case THREAD_BLOCKED:
    case THREAD_BLOCKED_READ_LOCK:
    case THREAD_SLEEPING:
    case THREAD_SUSPENDED:
      // thread is blocked, so ask the arch code to get us a starting point
      fp = arch_thread_get_blocked_fp(t);
      break;
    // we can't deal with every other state
    default:
      return ZX_ERR_BAD_STATE;
  }

  return _thread_print_backtrace(t, fp);
}
