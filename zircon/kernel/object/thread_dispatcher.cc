// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/thread_dispatcher.h"

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <lib/counters.h>
#include <platform.h>
#include <string.h>
#include <trace.h>
#include <zircon/rights.h>
#include <zircon/syscalls/debug.h>
#include <zircon/types.h>

#include <arch/debugger.h>
#include <arch/exception.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <kernel/thread.h>
#include <object/handle.h>
#include <object/job_dispatcher.h>
#include <object/process_dispatcher.h>
#include <vm/kstack.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_paged.h>

#define LOCAL_TRACE 0

KCOUNTER(dispatcher_thread_create_count, "dispatcher.thread.create")
KCOUNTER(dispatcher_thread_destroy_count, "dispatcher.thread.destroy")

// static
zx_status_t ThreadDispatcher::Create(fbl::RefPtr<ProcessDispatcher> process, uint32_t flags,
                                     fbl::StringPiece name,
                                     KernelHandle<ThreadDispatcher>* out_handle,
                                     zx_rights_t* out_rights) {
  // Create the lower level thread and attach it to the scheduler.
  thread_t* core_thread = thread_create(name.data(), StartRoutine, nullptr, DEFAULT_PRIORITY);
  if (!core_thread) {
    return ZX_ERR_NO_MEMORY;
  }
  // Create the user-mode thread and attach it to the process and lower level thread.
  fbl::AllocChecker ac;
  auto user_thread = fbl::AdoptRef(new (&ac) ThreadDispatcher(process, core_thread, flags));
  if (!ac.check()) {
    thread_forget(core_thread);
    return ZX_ERR_NO_MEMORY;
  }

  // The syscall layer will call Initialize(), which used to be called here.

  *out_rights = default_rights();
  *out_handle = KernelHandle(ktl::move(user_thread));
  return ZX_OK;
}

ThreadDispatcher::ThreadDispatcher(fbl::RefPtr<ProcessDispatcher> process, thread_t* core_thread,
                                   uint32_t flags)
    : process_(ktl::move(process)),
      core_thread_(core_thread),
      exceptionate_(ZX_EXCEPTION_CHANNEL_TYPE_THREAD) {
  LTRACE_ENTRY_OBJ;
  thread_set_usermode_thread(core_thread_, this);
  kcounter_add(dispatcher_thread_create_count, 1);
}

ThreadDispatcher::~ThreadDispatcher() {
  LTRACE_ENTRY_OBJ;

  kcounter_add(dispatcher_thread_destroy_count, 1);

  DEBUG_ASSERT(core_thread_ != nullptr);
  DEBUG_ASSERT(core_thread_ != get_current_thread());

  switch (state_.lifecycle()) {
    case ThreadState::Lifecycle::DEAD: {
      // join the LK thread before doing anything else to clean up LK state and ensure
      // the thread we're destroying has stopped.
      LTRACEF("joining LK thread to clean up state\n");
      [[maybe_unused]] auto ret = thread_join(core_thread_, nullptr, ZX_TIME_INFINITE);
      LTRACEF("done joining LK thread\n");
      DEBUG_ASSERT_MSG(ret == ZX_OK, "thread_join returned something other than ZX_OK\n");
      break;
    }
    case ThreadState::Lifecycle::INITIAL:
      __FALLTHROUGH;
      // this gets a pass, we can destruct a partially constructed thread.
    case ThreadState::Lifecycle::INITIALIZED:
      // as we've been initialized previously, forget the LK thread.
      // note that thread_forget is not called for self since the thread is not running.
      thread_forget(core_thread_);
      break;
    default:
      DEBUG_ASSERT_MSG(false, "bad state %s, this %p\n",
                       ThreadLifecycleToString(state_.lifecycle()), this);
  }

  event_destroy(&exception_event_);

  if (state_.lifecycle() != ThreadState::Lifecycle::INITIAL) {
    // We grew the pool in Initialize(), which transitioned the thread from its
    // inintial state.
    process_->futex_context().ShrinkFutexStatePool();
  }
}

// complete initialization of the thread object outside of the constructor
zx_status_t ThreadDispatcher::Initialize() {
  LTRACE_ENTRY_OBJ;
  // Associate the proc's address space with this thread.
  process_->aspace()->AttachToThread(core_thread_);

  // Make sure we contribute a FutexState object to our process's futex state.
  auto result = process_->futex_context().GrowFutexStatePool();
  if (result != ZX_OK) {
    return result;
  }

  Guard<fbl::Mutex> guard{get_lock()};
  // we've entered the initialized state
  SetStateLocked(ThreadState::Lifecycle::INITIALIZED);
  return ZX_OK;
}

zx_status_t ThreadDispatcher::set_name(const char* name, size_t len) {
  canary_.Assert();

  // ignore characters after the first NUL
  len = strnlen(name, len);

  if (len >= ZX_MAX_NAME_LEN)
    len = ZX_MAX_NAME_LEN - 1;

  Guard<SpinLock, IrqSave> guard{&name_lock_};
  memcpy(core_thread_->name, name, len);
  memset(core_thread_->name + len, 0, ZX_MAX_NAME_LEN - len);
  return ZX_OK;
}

void ThreadDispatcher::get_name(char out_name[ZX_MAX_NAME_LEN]) const {
  canary_.Assert();

  Guard<SpinLock, IrqSave> guard{&name_lock_};
  memset(out_name, 0, ZX_MAX_NAME_LEN);
  strlcpy(out_name, core_thread_->name, ZX_MAX_NAME_LEN);
}

// start a thread
zx_status_t ThreadDispatcher::Start(const EntryState& entry, bool initial_thread) {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;

  is_initial_thread_ = initial_thread;

  // add ourselves to the process, which may fail if the process is in a dead state.
  // If the process is live then it will call our StartRunning routine.
  return process_->AddInitializedThread(this, initial_thread, entry);
}

zx_status_t ThreadDispatcher::MakeRunnable(const EntryState& entry, bool suspended) {
  Guard<fbl::Mutex> guard{get_lock()};

  if (state_.lifecycle() != ThreadState::Lifecycle::INITIALIZED)
    return ZX_ERR_BAD_STATE;

  // save the user space entry state
  user_entry_ = entry;

  // update our suspend count to account for our parent state
  if (suspended)
    suspend_count_++;

  // bump the ref on this object that the LK thread state will now own until the lk thread has
  // exited
  AddRef();
  core_thread_->user_tid = get_koid();
  core_thread_->user_pid = process_->get_koid();

  // start the thread in RUNNING state, if we're starting suspended it will transition to
  // SUSPENDED when it checks thread signals before executing any user code
  SetStateLocked(ThreadState::Lifecycle::RUNNING);

  if (suspend_count_ == 0) {
    thread_resume(core_thread_);
  } else {
    // thread_suspend() only fails if the underlying thread is already dead, which we should
    // ignore here to match the behavior of thread_resume(); our Exiting() callback will run
    // shortly to clean us up
    thread_suspend(core_thread_);
  }

  return ZX_OK;
}

// called in the context of our thread
void ThreadDispatcher::Exit() {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;

  // only valid to call this on the current thread
  DEBUG_ASSERT(get_current_thread() == core_thread_);

  {
    Guard<fbl::Mutex> guard{get_lock()};

    SetStateLocked(ThreadState::Lifecycle::DYING);
  }

  // exit here
  // this will recurse back to us in ::Exiting()
  thread_exit(0);

  __UNREACHABLE;
}

void ThreadDispatcher::Kill() {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;

  Guard<fbl::Mutex> guard{get_lock()};

  switch (state_.lifecycle()) {
    case ThreadState::Lifecycle::INITIAL:
    case ThreadState::Lifecycle::INITIALIZED:
      // thread was never started, leave in this state
      break;
    case ThreadState::Lifecycle::RUNNING:
    case ThreadState::Lifecycle::SUSPENDED:
      // deliver a kernel kill signal to the thread
      thread_kill(core_thread_);

      // enter the dying state
      SetStateLocked(ThreadState::Lifecycle::DYING);
      break;
    case ThreadState::Lifecycle::DYING:
    case ThreadState::Lifecycle::DEAD:
      // already going down
      break;
  }
}

zx_status_t ThreadDispatcher::Suspend() {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;

  Guard<fbl::Mutex> guard{get_lock()};

  LTRACEF("%p: state %s\n", this, ThreadLifecycleToString(state_.lifecycle()));

  // Update |suspend_count_| in all cases so that we can always verify a sane value - it's
  // possible both Suspend() and Resume() get called while the thread is DYING.
  DEBUG_ASSERT(suspend_count_ >= 0);
  suspend_count_++;

  switch (state_.lifecycle()) {
    case ThreadState::Lifecycle::INITIAL:
      // Unreachable, thread leaves INITIAL state before Create() returns.
      DEBUG_ASSERT(false);
      __UNREACHABLE;
    case ThreadState::Lifecycle::INITIALIZED:
      // If the thread hasn't started yet, don't actually try to suspend it. We need to let
      // Start() run first to set up userspace entry data, which will then suspend if the count
      // is still >0 at that time.
      return ZX_OK;
    case ThreadState::Lifecycle::RUNNING:
    case ThreadState::Lifecycle::SUSPENDED:
      if (suspend_count_ == 1)
        return thread_suspend(core_thread_);
      return ZX_OK;
    case ThreadState::Lifecycle::DYING:
    case ThreadState::Lifecycle::DEAD:
      return ZX_ERR_BAD_STATE;
  }

  DEBUG_ASSERT(false);
  return ZX_ERR_BAD_STATE;
}

void ThreadDispatcher::Resume() {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;

  Guard<fbl::Mutex> guard{get_lock()};

  LTRACEF("%p: state %s\n", this, ThreadLifecycleToString(state_.lifecycle()));

  DEBUG_ASSERT(suspend_count_ > 0);
  suspend_count_--;

  switch (state_.lifecycle()) {
    case ThreadState::Lifecycle::INITIAL:
      // Unreachable, thread leaves INITIAL state before Create() returns.
      DEBUG_ASSERT(false);
      __UNREACHABLE;
    case ThreadState::Lifecycle::INITIALIZED:
      break;
    case ThreadState::Lifecycle::RUNNING:
    case ThreadState::Lifecycle::SUSPENDED:
      // It's possible the thread never transitioned from RUNNING -> SUSPENDED.
      if (suspend_count_ == 0)
        thread_resume(core_thread_);
      break;
    case ThreadState::Lifecycle::DYING:
    case ThreadState::Lifecycle::DEAD:
      // If it's dying or dead then bail.
      break;
  }
}

bool ThreadDispatcher::IsDyingOrDead() const {
  Guard<fbl::Mutex> guard{get_lock()};
  return IsDyingOrDeadLocked();
}

bool ThreadDispatcher::IsDyingOrDeadLocked() const {
  return state_.lifecycle() == ThreadState::Lifecycle::DYING ||
         state_.lifecycle() == ThreadState::Lifecycle::DEAD;
}

static void ThreadCleanupDpc(dpc_t* d) {
  LTRACEF("dpc %p\n", d);

  ThreadDispatcher* t = reinterpret_cast<ThreadDispatcher*>(d->arg);
  DEBUG_ASSERT(t);

  delete t;
}

void ThreadDispatcher::Exiting() {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;

  // Notify a debugger if attached. Do this before marking the thread as
  // dead: the debugger expects to see the thread in the DYING state, it may
  // try to read thread registers. The debugger still has to handle the case
  // where the process is also dying (and thus the thread could transition
  // DYING->DEAD from underneath it), but that's life (or death :-)).
  //
  // Thread exit exceptions don't currently provide an iframe.
  arch_exception_context_t context{};
  HandleSingleShotException(process_->exceptionate(Exceptionate::Type::kDebug),
                            ZX_EXCP_THREAD_EXITING, context);

  // Mark the thread as dead. Do this before removing the thread from the
  // process because if this is the last thread then the process will be
  // marked dead, and we don't want to have a state where the process is
  // dead but one thread is not.
  {
    Guard<fbl::Mutex> guard{get_lock()};

    // put ourselves into the dead state
    SetStateLocked(ThreadState::Lifecycle::DEAD);
  }

  // Drop our exception channel endpoint so any userspace listener
  // gets the PEER_CLOSED signal.
  exceptionate_.Shutdown();

  // remove ourselves from our parent process's view
  process_->RemoveThread(this);

  // drop LK's reference
  if (Release()) {
    // We're the last reference, so will need to destruct ourself while running, which is not
    // possible Use a dpc to pull this off
    cleanup_dpc_.func = ThreadCleanupDpc;
    cleanup_dpc_.arg = this;

    // disable interrupts before queuing the dpc to prevent starving the DPC thread if it starts
    // running before we're completed. disabling interrupts effectively raises us to maximum
    // priority on this cpu. note this is only safe because we're about to exit the thread
    // permanently so the context switch will effectively reenable interrupts in the new thread.
    arch_disable_ints();

    // queue without reschedule since us exiting is a reschedule event already
    dpc_queue(&cleanup_dpc_, false);
  }

  // after this point the thread will stop permanently
  LTRACE_EXIT_OBJ;
}

void ThreadDispatcher::Suspending() {
  LTRACE_ENTRY_OBJ;

  // Update the state before sending any notifications out. We want the
  // receiver to see the new state.
  {
    Guard<fbl::Mutex> guard{get_lock()};

    // Don't suspend if we are racing with our own death.
    if (state_.lifecycle() != ThreadState::Lifecycle::DYING) {
      SetStateLocked(ThreadState::Lifecycle::SUSPENDED);
    }
  }

  LTRACE_EXIT_OBJ;
}

void ThreadDispatcher::Resuming() {
  LTRACE_ENTRY_OBJ;

  // Update the state before sending any notifications out. We want the
  // receiver to see the new state.
  {
    Guard<fbl::Mutex> guard{get_lock()};

    // Don't resume if we are racing with our own death.
    if (state_.lifecycle() != ThreadState::Lifecycle::DYING) {
      SetStateLocked(ThreadState::Lifecycle::RUNNING);
    }
  }

  LTRACE_EXIT_OBJ;
}

// low level LK entry point for the thread
int ThreadDispatcher::StartRoutine(void* arg) {
  LTRACE_ENTRY;

  ThreadDispatcher* t = (ThreadDispatcher*)arg;

  // IWBN to dump the values just before calling |arch_enter_uspace()|
  // but at that point they're in |iframe| and may have been modified by
  // the debugger user, and fetching them out of the iframe will require
  // architecture-specific code. Instead just print them here. This is just
  // for tracing which is generally off, and then only time the values will
  // have changed is if a debugger user changes them. KISS.
  LTRACEF("arch_enter_uspace SP: %#" PRIxPTR " PC: %#" PRIxPTR ", ARG1: %#" PRIxPTR
          ", ARG2: %#" PRIxPTR "\n",
          t->user_entry_.sp, t->user_entry_.pc, t->user_entry_.arg1, t->user_entry_.arg2);

  // Initialize an iframe for entry into userspace.
  // We need all registers accessible from the ZX_EXCP_THREAD_STARTING
  // exception handler (the debugger wants the thread to look as if the
  // thread is at the first instruction). For architectural exceptions the
  // general regs are left in the iframe for speed and simplicity. To keep
  // things simple we use the same scheme.
  iframe_t iframe{};
  arch_setup_uspace_iframe(&iframe, t->user_entry_.pc, t->user_entry_.sp, t->user_entry_.arg1,
                           t->user_entry_.arg2);

  arch_exception_context_t context{};
  context.frame = &iframe;

  // Notify job debugger if attached.
  if (t->is_initial_thread_) {
    t->process_->OnProcessStartForJobDebugger(t, &context);
  }

  // Notify debugger if attached.
  t->HandleSingleShotException(t->process_->exceptionate(Exceptionate::Type::kDebug),
                               ZX_EXCP_THREAD_STARTING, context);

  arch_iframe_process_pending_signals(&iframe);

  // switch to user mode and start the process
  arch_enter_uspace(&iframe);
  __UNREACHABLE;
}

void ThreadDispatcher::SetStateLocked(ThreadState::Lifecycle lifecycle) {
  canary_.Assert();

  LTRACEF("thread %p: state %u (%s)\n", this, static_cast<unsigned int>(lifecycle),
          ThreadLifecycleToString(lifecycle));

  DEBUG_ASSERT(get_lock()->lock().IsHeld());

  state_.set(lifecycle);

  switch (lifecycle) {
    case ThreadState::Lifecycle::RUNNING:
      UpdateStateLocked(ZX_THREAD_SUSPENDED, ZX_THREAD_RUNNING);
      break;
    case ThreadState::Lifecycle::SUSPENDED:
      UpdateStateLocked(ZX_THREAD_RUNNING, ZX_THREAD_SUSPENDED);
      break;
    case ThreadState::Lifecycle::DEAD:
      UpdateStateLocked(ZX_THREAD_RUNNING | ZX_THREAD_SUSPENDED, ZX_THREAD_TERMINATED);
      break;
    default:
      // Nothing to do.
      // In particular, for the DYING state we don't modify the SUSPENDED
      // or RUNNING signals: For observer purposes they'll only be interested
      // in the transition from {SUSPENDED,RUNNING} to DEAD.
      break;
  }
}

bool ThreadDispatcher::InExceptionLocked() {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;
  DEBUG_ASSERT(get_lock()->lock().IsHeld());
  return exception_ != nullptr;
}

bool ThreadDispatcher::SuspendedOrInExceptionLocked() {
  canary_.Assert();
  return state_.lifecycle() == ThreadState::Lifecycle::SUSPENDED || InExceptionLocked();
}

static zx_thread_state_t ThreadLifecycleToState(ThreadState::Lifecycle lifecycle,
                                                ThreadDispatcher::Blocked blocked_reason) {
  switch (lifecycle) {
    case ThreadState::Lifecycle::INITIAL:
    case ThreadState::Lifecycle::INITIALIZED:
      return ZX_THREAD_STATE_NEW;
    case ThreadState::Lifecycle::RUNNING:
      // The thread may be "running" but be blocked in a syscall or
      // exception handler.
      switch (blocked_reason) {
        case ThreadDispatcher::Blocked::NONE:
          return ZX_THREAD_STATE_RUNNING;
        case ThreadDispatcher::Blocked::EXCEPTION:
          return ZX_THREAD_STATE_BLOCKED_EXCEPTION;
        case ThreadDispatcher::Blocked::SLEEPING:
          return ZX_THREAD_STATE_BLOCKED_SLEEPING;
        case ThreadDispatcher::Blocked::FUTEX:
          return ZX_THREAD_STATE_BLOCKED_FUTEX;
        case ThreadDispatcher::Blocked::PORT:
          return ZX_THREAD_STATE_BLOCKED_PORT;
        case ThreadDispatcher::Blocked::CHANNEL:
          return ZX_THREAD_STATE_BLOCKED_CHANNEL;
        case ThreadDispatcher::Blocked::WAIT_ONE:
          return ZX_THREAD_STATE_BLOCKED_WAIT_ONE;
        case ThreadDispatcher::Blocked::WAIT_MANY:
          return ZX_THREAD_STATE_BLOCKED_WAIT_MANY;
        case ThreadDispatcher::Blocked::INTERRUPT:
          return ZX_THREAD_STATE_BLOCKED_INTERRUPT;
        case ThreadDispatcher::Blocked::PAGER:
          return ZX_THREAD_STATE_BLOCKED_PAGER;
        default:
          DEBUG_ASSERT_MSG(false, "unexpected blocked reason: %d",
                           static_cast<int>(blocked_reason));
          return ZX_THREAD_STATE_BLOCKED;
      }
    case ThreadState::Lifecycle::SUSPENDED:
      return ZX_THREAD_STATE_SUSPENDED;
    case ThreadState::Lifecycle::DYING:
      return ZX_THREAD_STATE_DYING;
    case ThreadState::Lifecycle::DEAD:
      return ZX_THREAD_STATE_DEAD;
    default:
      DEBUG_ASSERT_MSG(false, "unexpected run state: %d", static_cast<int>(lifecycle));
      return ZX_THREAD_RUNNING;
  }
}

zx_status_t ThreadDispatcher::GetInfoForUserspace(zx_info_thread_t* info) {
  canary_.Assert();
  Guard<fbl::Mutex> guard{get_lock()};

  info->state = ThreadLifecycleToState(state_.lifecycle(), blocked_reason_);
  info->wait_exception_channel_type = exceptionate_type_;

  // Get CPU affinity.
  //
  // We assume that we can fit the entire mask in the first word of
  // cpu_affinity_mask.
  static_assert(SMP_MAX_CPUS <= sizeof(info->cpu_affinity_mask.mask[0]) * 8);
  info->cpu_affinity_mask.mask[0] = thread_get_soft_cpu_affinity(core_thread_);

  return ZX_OK;
}

zx_status_t ThreadDispatcher::GetStatsForUserspace(zx_info_thread_stats_t* info) {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;

  *info = {};

  info->total_runtime = runtime_ns();
  info->last_scheduled_cpu = last_cpu();
  return ZX_OK;
}

zx_status_t ThreadDispatcher::GetExceptionReport(zx_exception_report_t* report) {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;
  Guard<fbl::Mutex> guard{get_lock()};

  if (InExceptionLocked()) {
    // We always leave exception handling before the report gets wiped
    // so this must succeed.
    [[maybe_unused]] bool success = exception_->FillReport(report);
    DEBUG_ASSERT(success);
  } else {
    return ZX_ERR_BAD_STATE;
  }

  return ZX_OK;
}

Exceptionate* ThreadDispatcher::exceptionate() {
  canary_.Assert();
  return &exceptionate_;
}

zx_status_t ThreadDispatcher::HandleException(Exceptionate* exceptionate,
                                              fbl::RefPtr<ExceptionDispatcher> exception,
                                              bool* sent) {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;

  *sent = false;

  // Mark as blocked before sending the exception, otherwise a handler could
  // potentially query our state before we've marked ourself blocked.
  ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::EXCEPTION);

  {
    Guard<fbl::Mutex> guard{get_lock()};

    // We send the exception while locked so that if it succeeds we can
    // atomically update our state.
    zx_status_t status = exceptionate->SendException(exception);
    if (status != ZX_OK) {
      return status;
    }
    *sent = true;

    // This state is needed by GetInfoForUserspace().
    exception_ = exception;
    exceptionate_type_ = exceptionate->type();
    state_.set(ThreadState::Exception::UNPROCESSED);
  }

  LTRACEF("blocking on exception response\n");

  zx_status_t status = exception->WaitForHandleClose();

  LTRACEF("received exception response %d\n", status);

  Guard<fbl::Mutex> guard{get_lock()};

  // If both the thread was killed and the exception was resumed before we
  // started waiting, the exception resume status (ZX_OK) might be returned,
  // but we want thread kill to always take priority and stop exception
  // handling immediately.
  //
  // Note that this logic will always trigger for ZX_EXCP_THREAD_EXITING
  // whether the thread was killed or is exiting normally, but that's fine
  // because that exception only ever goes to a single handler so we ignore
  // the handler return value anyway.
  if (IsDyingOrDeadLocked()) {
    status = ZX_ERR_INTERNAL_INTR_KILLED;
  }

  exception_.reset();
  state_.set(ThreadState::Exception::IDLE);

  return status;
}

bool ThreadDispatcher::HandleSingleShotException(Exceptionate* exceptionate,
                                                 zx_excp_type_t exception_type,
                                                 const arch_exception_context_t& context) {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;

  // Do a quick check for valid channel first. It's still possible that the
  // channel will become invalid immediately after this check, but that will
  // be caught when we try to send the exception. This is just an
  // optimization to avoid unnecessary setup/teardown in the common case.
  if (!exceptionate->HasValidChannel()) {
    return false;
  }

  arch_install_context_regs(core_thread_, &context);
  auto auto_call = fbl::MakeAutoCall([this]() { arch_remove_context_regs(core_thread_); });

  zx_exception_report_t report = ExceptionDispatcher::BuildArchReport(exception_type, context);

  fbl::RefPtr<ExceptionDispatcher> exception =
      ExceptionDispatcher::Create(fbl::RefPtr(this), exception_type, &report, &context);
  if (!exception) {
    printf("KERN: failed to allocate memory for exception type %u in thread %lu.%lu\n",
           exception_type, process_->get_koid(), get_koid());
    return false;
  }

  bool sent = false;
  HandleException(exceptionate, exception, &sent);

  exception->Clear();

  return sent;
}

// T is the state type to read.
// F is a function that gets state T and has signature |zx_status_t (F)(thread_t*, T*)|.
template <typename T, typename F>
zx_status_t ThreadDispatcher::ReadStateGeneric(F get_state_func, thread_t* thread,
                                               user_out_ptr<void> buffer, size_t buffer_size) {
  if (buffer_size < sizeof(T)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  T state{};

  {
    Guard<fbl::Mutex> guard{get_lock()};
    // We can't be reading regs while the thread transitions from SUSPENDED to RUNNING.
    if (!SuspendedOrInExceptionLocked()) {
      return ZX_ERR_BAD_STATE;
    }
    zx_status_t status = get_state_func(thread, &state);
    if (status != ZX_OK) {
      return status;
    }
  }

  // Since copy may fault, copy only after releasing the lock.
  return buffer.reinterpret<T>().copy_to_user(state);
}

zx_status_t ThreadDispatcher::ReadState(zx_thread_state_topic_t state_kind,
                                        user_out_ptr<void> buffer, size_t buffer_size) {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;

  switch (state_kind) {
    case ZX_THREAD_STATE_GENERAL_REGS:
      return ReadStateGeneric<zx_thread_state_general_regs_t>(arch_get_general_regs, core_thread_,
                                                              buffer, buffer_size);
    case ZX_THREAD_STATE_FP_REGS:
      return ReadStateGeneric<zx_thread_state_fp_regs_t>(arch_get_fp_regs, core_thread_, buffer,
                                                         buffer_size);
    case ZX_THREAD_STATE_VECTOR_REGS:
      return ReadStateGeneric<zx_thread_state_vector_regs_t>(arch_get_vector_regs, core_thread_,
                                                             buffer, buffer_size);
    case ZX_THREAD_STATE_DEBUG_REGS:
      return ReadStateGeneric<zx_thread_state_debug_regs_t>(arch_get_debug_regs, core_thread_,
                                                            buffer, buffer_size);
    case ZX_THREAD_STATE_SINGLE_STEP:
      return ReadStateGeneric<zx_thread_state_single_step_t>(arch_get_single_step, core_thread_,
                                                             buffer, buffer_size);
    default:
      return ZX_ERR_INVALID_ARGS;
  }
}

// T is the state type to write.
// F is a function that sets state T and has signature |zx_status_t (F)(thread_t*, const T*)|.
template <typename T, typename F>
zx_status_t ThreadDispatcher::WriteStateGeneric(F set_state_func, thread_t* thread,
                                                user_in_ptr<const void> buffer,
                                                size_t buffer_size) {
  if (buffer_size < sizeof(T)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  T state{};
  zx_status_t status = buffer.reinterpret<const T>().copy_from_user(&state);
  if (status != ZX_OK) {
    return status;
  }

  Guard<fbl::Mutex> guard{get_lock()};

  // We can't be reading regs while the thread transitions from SUSPENDED to RUNNING.
  if (!SuspendedOrInExceptionLocked()) {
    return ZX_ERR_BAD_STATE;
  }

  return set_state_func(thread, &state);
}

zx_status_t ThreadDispatcher::WriteState(zx_thread_state_topic_t state_kind,
                                         user_in_ptr<const void> buffer, size_t buffer_size) {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;

  switch (state_kind) {
    case ZX_THREAD_STATE_GENERAL_REGS:
      return WriteStateGeneric<zx_thread_state_general_regs_t>(arch_set_general_regs, core_thread_,
                                                               buffer, buffer_size);
    case ZX_THREAD_STATE_FP_REGS:
      return WriteStateGeneric<zx_thread_state_fp_regs_t>(arch_set_fp_regs, core_thread_, buffer,
                                                          buffer_size);
    case ZX_THREAD_STATE_VECTOR_REGS:
      return WriteStateGeneric<zx_thread_state_vector_regs_t>(arch_set_vector_regs, core_thread_,
                                                              buffer, buffer_size);
    case ZX_THREAD_STATE_DEBUG_REGS:
      return WriteStateGeneric<zx_thread_state_debug_regs_t>(arch_set_debug_regs, core_thread_,
                                                             buffer, buffer_size);
    case ZX_THREAD_STATE_SINGLE_STEP:
      return WriteStateGeneric<zx_thread_state_single_step_t>(arch_set_single_step, core_thread_,
                                                              buffer, buffer_size);
    default:
      return ZX_ERR_INVALID_ARGS;
  }
}

zx_status_t ThreadDispatcher::SetPriority(int32_t priority) {
  Guard<fbl::Mutex> guard{get_lock()};
  if ((state_.lifecycle() == ThreadState::Lifecycle::INITIAL) ||
      (state_.lifecycle() == ThreadState::Lifecycle::DYING) ||
      (state_.lifecycle() == ThreadState::Lifecycle::DEAD)) {
    return ZX_ERR_BAD_STATE;
  }
  // The priority was already validated by the Profile dispatcher.
  thread_set_priority(core_thread_, priority);
  return ZX_OK;
}

zx_status_t ThreadDispatcher::SetDeadline(const zx_sched_deadline_params_t& params) {
  Guard<fbl::Mutex> guard{get_lock()};
  if ((state_.lifecycle() == ThreadState::Lifecycle::INITIAL) ||
      (state_.lifecycle() == ThreadState::Lifecycle::DYING) ||
      (state_.lifecycle() == ThreadState::Lifecycle::DEAD)) {
    return ZX_ERR_BAD_STATE;
  }
  // The deadline parameters are already validated by the Profile dispatcher.
  thread_set_deadline(core_thread_, params);
  return ZX_OK;
}

zx_status_t ThreadDispatcher::SetSoftAffinity(cpu_mask_t mask) {
  Guard<fbl::Mutex> guard{get_lock()};
  if ((state_.lifecycle() == ThreadState::Lifecycle::INITIAL) ||
      (state_.lifecycle() == ThreadState::Lifecycle::DYING) ||
      (state_.lifecycle() == ThreadState::Lifecycle::DEAD)) {
    return ZX_ERR_BAD_STATE;
  }
  // The mask was already validated by the Profile dispatcher.
  thread_set_soft_cpu_affinity(core_thread_, mask);
  return ZX_OK;
}

const char* ThreadLifecycleToString(ThreadState::Lifecycle lifecycle) {
  switch (lifecycle) {
    case ThreadState::Lifecycle::INITIAL:
      return "initial";
    case ThreadState::Lifecycle::INITIALIZED:
      return "initialized";
    case ThreadState::Lifecycle::RUNNING:
      return "running";
    case ThreadState::Lifecycle::SUSPENDED:
      return "suspended";
    case ThreadState::Lifecycle::DYING:
      return "dying";
    case ThreadState::Lifecycle::DEAD:
      return "dead";
  }
  return "unknown";
}

zx_koid_t ThreadDispatcher::get_related_koid() const {
  canary_.Assert();

  return process_->get_koid();
}
