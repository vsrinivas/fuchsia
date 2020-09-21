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
#include <arch/vm.h>
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
                                     ktl::string_view name,
                                     KernelHandle<ThreadDispatcher>* out_handle,
                                     zx_rights_t* out_rights) {
  // Create the user-mode thread and attach it to the process and lower level thread.
  fbl::AllocChecker ac;
  auto user_thread = fbl::AdoptRef(new (&ac) ThreadDispatcher(process, flags));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // Create the lower level thread and attach it to the scheduler.
  Thread* core_thread =
      Thread::Create(name.data(), StartRoutine, user_thread.get(), DEFAULT_PRIORITY);
  if (!core_thread) {
    return ZX_ERR_NO_MEMORY;
  }

  // We haven't yet compeleted initialization of |user_thread|, and
  // references to it haven't possibly escaped this thread. We can
  // safely set |core_thread_| outside the lock.
  [&user_thread, &core_thread]()
      TA_NO_THREAD_SAFETY_ANALYSIS { user_thread->core_thread_ = core_thread; }();

  // The syscall layer will call Initialize(), which used to be called here.

  *out_rights = default_rights();
  *out_handle = KernelHandle(ktl::move(user_thread));
  return ZX_OK;
}

ThreadDispatcher::ThreadDispatcher(fbl::RefPtr<ProcessDispatcher> process, uint32_t flags)
    : process_(ktl::move(process)), exceptionate_(ZX_EXCEPTION_CHANNEL_TYPE_THREAD) {
  LTRACE_ENTRY_OBJ;
  kcounter_add(dispatcher_thread_create_count, 1);
}

ThreadDispatcher::~ThreadDispatcher() {
  LTRACE_ENTRY_OBJ;

  kcounter_add(dispatcher_thread_destroy_count, 1);

  DEBUG_ASSERT_MSG(!HasStartedLocked() || IsDyingOrDeadLocked(),
                   "Thread %p killed in bad state: %s\n", this,
                   ThreadLifecycleToString(state_.lifecycle()));

  if (state_.lifecycle() != ThreadState::Lifecycle::INITIAL) {
    // We grew the pool in Initialize(), which transitioned the thread from its
    // inintial state.
    process_->futex_context().ShrinkFutexStatePool();
  }

  // If MakeRunnable hasn't been called, then our core_thread_ has never run and
  // we need to be the ones to remove it.
  if (!HasStartedLocked() && core_thread_ != nullptr) {
    // Since the Thread is in an initialized state we can directly destruct it.
    core_thread_->Forget();
    core_thread_ = nullptr;
  }
}

// complete initialization of the thread object outside of the constructor
zx_status_t ThreadDispatcher::Initialize() {
  LTRACE_ENTRY_OBJ;

  // Make sure we contribute a FutexState object to our process's futex state.
  auto result = process_->futex_context().GrowFutexStatePool();
  if (result != ZX_OK) {
    return result;
  }

  Guard<Mutex> guard{get_lock()};
  // Associate the proc's address space with this thread.
  process_->aspace()->AttachToThread(core_thread_);
  // we've entered the initialized state
  SetStateLocked(ThreadState::Lifecycle::INITIALIZED);
  return ZX_OK;
}

zx_status_t ThreadDispatcher::set_name(const char* name, size_t len) {
  canary_.Assert();

  Guard<Mutex> thread_guard{get_lock()};
  if (core_thread_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  core_thread_->set_name({name, len});
  return ZX_OK;
}

void ThreadDispatcher::get_name(char out_name[ZX_MAX_NAME_LEN]) const {
  canary_.Assert();

  memset(out_name, 0, ZX_MAX_NAME_LEN);

  Guard<Mutex> thread_guard{get_lock()};
  if (core_thread_ == nullptr) {
    return;
  }
  strlcpy(out_name, core_thread_->name(), ZX_MAX_NAME_LEN);
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
  if (!arch_is_valid_user_pc(entry.pc)) {
    return ZX_ERR_INVALID_ARGS;
  }
  Guard<Mutex> guard{get_lock()};

  if (state_.lifecycle() != ThreadState::Lifecycle::INITIALIZED)
    return ZX_ERR_BAD_STATE;

  // save the user space entry state
  user_entry_ = entry;

  // update our suspend count to account for our parent state
  if (suspended)
    suspend_count_++;

  // Attach the ThreadDispatcher to the core thread. This takes out an additional
  // reference on the ThreadDispatcher.
  core_thread_->SetUsermodeThread(fbl::RefPtr<ThreadDispatcher>(this));

  // start the thread in RUNNING state, if we're starting suspended it will transition to
  // SUSPENDED when it checks thread signals before executing any user code
  SetStateLocked(ThreadState::Lifecycle::RUNNING);

  if (suspend_count_ == 0) {
    core_thread_->Resume();
  } else {
    // Thread::Suspend() only fails if the underlying thread is already dead, which we should
    // ignore here to match the behavior of Thread::Resume(); our Exiting() callback will run
    // shortly to clean us up
    core_thread_->Suspend();
  }

  return ZX_OK;
}

void ThreadDispatcher::Kill() {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;

  Guard<Mutex> guard{get_lock()};

  switch (state_.lifecycle()) {
    case ThreadState::Lifecycle::INITIAL:
    case ThreadState::Lifecycle::INITIALIZED:
      // thread was never started, leave in this state
      break;
    case ThreadState::Lifecycle::RUNNING:
    case ThreadState::Lifecycle::SUSPENDED:
      // deliver a kernel kill signal to the thread
      core_thread_->Kill();

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

  Guard<Mutex> guard{get_lock()};

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
        return core_thread_->Suspend();
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

  Guard<Mutex> guard{get_lock()};

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
        core_thread_->Resume();
      break;
    case ThreadState::Lifecycle::DYING:
    case ThreadState::Lifecycle::DEAD:
      // If it's dying or dead then bail.
      break;
  }
}

bool ThreadDispatcher::IsDyingOrDead() const {
  Guard<Mutex> guard{get_lock()};
  return IsDyingOrDeadLocked();
}

bool ThreadDispatcher::IsDyingOrDeadLocked() const {
  auto lifecycle = state_.lifecycle();
  return lifecycle == ThreadState::Lifecycle::DYING || lifecycle == ThreadState::Lifecycle::DEAD;
}

bool ThreadDispatcher::HasStarted() const {
  Guard<Mutex> guard{get_lock()};
  return HasStartedLocked();
}

bool ThreadDispatcher::HasStartedLocked() const {
  auto lifecycle = state_.lifecycle();
  return lifecycle != ThreadState::Lifecycle::INITIAL &&
         lifecycle != ThreadState::Lifecycle::INITIALIZED;
}

void ThreadDispatcher::ExitingCurrent() {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;

  // Set ourselves in the DYING state before calling the Debugger.
  {
    Guard<fbl::Mutex> guard{get_lock()};
    SetStateLocked(ThreadState::Lifecycle::DYING);
  }

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
    Guard<Mutex> guard{get_lock()};

    // put ourselves into the dead state
    SetStateLocked(ThreadState::Lifecycle::DEAD);
    core_thread_ = nullptr;
  }

  // Drop our exception channel endpoint so any userspace listener
  // gets the PEER_CLOSED signal.
  exceptionate_.Shutdown();

  // remove ourselves from our parent process's view
  process_->RemoveThread(this);

  LTRACE_EXIT_OBJ;
}

void ThreadDispatcher::Suspending() {
  LTRACE_ENTRY_OBJ;

  // Update the state before sending any notifications out. We want the
  // receiver to see the new state.
  {
    Guard<Mutex> guard{get_lock()};

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
    Guard<Mutex> guard{get_lock()};

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

  *info = {};

  Guard<Mutex> guard{get_lock()};
  info->state = ThreadLifecycleToState(state_.lifecycle(), blocked_reason_);
  info->wait_exception_channel_type = exceptionate_type_;

  // If we've exited then return with the lifecycle and exception type, and keep
  // the cpu_affinity_mask at 0.
  if (core_thread_ == nullptr) {
    return ZX_OK;
  }

  // Get CPU affinity.
  //
  // We assume that we can fit the entire mask in the first word of
  // cpu_affinity_mask.
  static_assert(SMP_MAX_CPUS <= sizeof(info->cpu_affinity_mask.mask[0]) * 8);
  info->cpu_affinity_mask.mask[0] = core_thread_->GetSoftCpuAffinity();

  return ZX_OK;
}

zx_status_t ThreadDispatcher::GetStatsForUserspace(zx_info_thread_stats_t* info) {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;

  *info = {};

  Guard<Mutex> guard{get_lock()};

  if (core_thread_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }

  info->total_runtime = core_thread_->Runtime();
  info->last_scheduled_cpu = core_thread_->LastCpu();
  return ZX_OK;
}

zx_status_t ThreadDispatcher::GetRuntimeStats(Thread::RuntimeStats* out) const {
  canary_.Assert();

  *out = {};

  // Repeatedly try to get a consistent snapshot out of runtime stats using the generation count.
  //
  // We attempt to get a snapshot forever, so it is theoretically possible for us to loop forever.
  // In practice, our context switching overhead is significantly higher than the runtime of this
  // loop, so it is unlikely to happen.
  //
  // If our context switch overhead drops very significantly, we may need to revisit this
  // algorithm and return an error after some number of loops.
  while (true) {
    uint64_t start_count;
    while ((start_count = stats_generation_count_.load(ktl::memory_order_acquire)) % 2) {
      // Loop until no write is happening concurrently.
    }

    *out = runtime_stats_;

    uint64_t end_count = stats_generation_count_.load(ktl::memory_order_acquire);
    if (start_count == end_count) {
      return ZX_OK;
    }
  }
}

zx_status_t ThreadDispatcher::GetExceptionReport(zx_exception_report_t* report) {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;
  Guard<Mutex> guard{get_lock()};

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
    Guard<Mutex> guard{get_lock()};

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

  Guard<Mutex> guard{get_lock()};

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
                                                 const arch_exception_context_t& arch_context) {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;

  // Do a quick check for valid channel first. It's still possible that the
  // channel will become invalid immediately after this check, but that will
  // be caught when we try to send the exception. This is just an
  // optimization to avoid unnecessary setup/teardown in the common case.
  if (!exceptionate->HasValidChannel()) {
    return false;
  }

  zx_exception_report_t report = ExceptionDispatcher::BuildArchReport(exception_type, arch_context);

  fbl::RefPtr<ExceptionDispatcher> exception =
      ExceptionDispatcher::Create(fbl::RefPtr(this), exception_type, &report, &arch_context);
  if (!exception) {
    printf("KERN: failed to allocate memory for exception type %u in thread %lu.%lu\n",
           exception_type, process_->get_koid(), get_koid());
    return false;
  }

  // We're about to handle the exception (|HandleException|).  Use a |ScopedThreadExceptionContext|
  // to make the thread's user register state available to debuggers and exception handlers while
  // the thread is "in exception".
  ScopedThreadExceptionContext context(&arch_context);

  bool sent = false;
  HandleException(exceptionate, exception, &sent);

  exception->Clear();

  return sent;
}

// T is the state type to read.
// F is a function that gets state T and has signature |zx_status_t (F)(Thread*, T*)|.
template <typename T, typename F>
zx_status_t ThreadDispatcher::ReadStateGeneric(F get_state_func, user_out_ptr<void> buffer,
                                               size_t buffer_size) {
  if (buffer_size < sizeof(T)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  T state{};

  {
    Guard<Mutex> guard{get_lock()};
    // We can't be reading regs while the thread transitions from SUSPENDED to RUNNING.
    if (!SuspendedOrInExceptionLocked()) {
      return ZX_ERR_BAD_STATE;
    }
    zx_status_t status = get_state_func(core_thread_, &state);
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
      return ReadStateGeneric<zx_thread_state_general_regs_t>(arch_get_general_regs, buffer,
                                                              buffer_size);
    case ZX_THREAD_STATE_FP_REGS:
      return ReadStateGeneric<zx_thread_state_fp_regs_t>(arch_get_fp_regs, buffer, buffer_size);
    case ZX_THREAD_STATE_VECTOR_REGS:
      return ReadStateGeneric<zx_thread_state_vector_regs_t>(arch_get_vector_regs, buffer,
                                                             buffer_size);
    case ZX_THREAD_STATE_DEBUG_REGS:
      return ReadStateGeneric<zx_thread_state_debug_regs_t>(arch_get_debug_regs, buffer,
                                                            buffer_size);
    case ZX_THREAD_STATE_SINGLE_STEP:
      return ReadStateGeneric<zx_thread_state_single_step_t>(arch_get_single_step, buffer,
                                                             buffer_size);
    default:
      return ZX_ERR_INVALID_ARGS;
  }
}

// T is the state type to write.
// F is a function that sets state T and has signature |zx_status_t (F)(Thread*, const T*)|.
template <typename T, typename F>
zx_status_t ThreadDispatcher::WriteStateGeneric(F set_state_func, user_in_ptr<const void> buffer,
                                                size_t buffer_size) {
  if (buffer_size < sizeof(T)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  T state{};
  zx_status_t status = buffer.reinterpret<const T>().copy_from_user(&state);
  if (status != ZX_OK) {
    return status;
  }

  Guard<Mutex> guard{get_lock()};

  // We can't be writing regs while the thread transitions from SUSPENDED to RUNNING.
  if (!SuspendedOrInExceptionLocked()) {
    return ZX_ERR_BAD_STATE;
  }

  return set_state_func(core_thread_, &state);
}

zx_status_t ThreadDispatcher::WriteState(zx_thread_state_topic_t state_kind,
                                         user_in_ptr<const void> buffer, size_t buffer_size) {
  canary_.Assert();

  LTRACE_ENTRY_OBJ;

  switch (state_kind) {
    case ZX_THREAD_STATE_GENERAL_REGS:
      return WriteStateGeneric<zx_thread_state_general_regs_t>(arch_set_general_regs, buffer,
                                                               buffer_size);
    case ZX_THREAD_STATE_FP_REGS:
      return WriteStateGeneric<zx_thread_state_fp_regs_t>(arch_set_fp_regs, buffer, buffer_size);
    case ZX_THREAD_STATE_VECTOR_REGS:
      return WriteStateGeneric<zx_thread_state_vector_regs_t>(arch_set_vector_regs, buffer,
                                                              buffer_size);
    case ZX_THREAD_STATE_DEBUG_REGS:
      return WriteStateGeneric<zx_thread_state_debug_regs_t>(arch_set_debug_regs, buffer,
                                                             buffer_size);
    case ZX_THREAD_STATE_SINGLE_STEP:
      return WriteStateGeneric<zx_thread_state_single_step_t>(arch_set_single_step, buffer,
                                                              buffer_size);
    default:
      return ZX_ERR_INVALID_ARGS;
  }
}

zx_status_t ThreadDispatcher::SetPriority(int32_t priority) {
  Guard<Mutex> guard{get_lock()};
  if ((state_.lifecycle() == ThreadState::Lifecycle::INITIAL) ||
      (state_.lifecycle() == ThreadState::Lifecycle::DYING) ||
      (state_.lifecycle() == ThreadState::Lifecycle::DEAD)) {
    return ZX_ERR_BAD_STATE;
  }
  // The priority was already validated by the Profile dispatcher.
  core_thread_->SetPriority(priority);
  return ZX_OK;
}

zx_status_t ThreadDispatcher::SetDeadline(const zx_sched_deadline_params_t& params) {
  Guard<Mutex> guard{get_lock()};
  if ((state_.lifecycle() == ThreadState::Lifecycle::INITIAL) ||
      (state_.lifecycle() == ThreadState::Lifecycle::DYING) ||
      (state_.lifecycle() == ThreadState::Lifecycle::DEAD)) {
    return ZX_ERR_BAD_STATE;
  }
  // The deadline parameters are already validated by the Profile dispatcher.
  core_thread_->SetDeadline(params);
  return ZX_OK;
}

zx_status_t ThreadDispatcher::SetSoftAffinity(cpu_mask_t mask) {
  Guard<Mutex> guard{get_lock()};
  if ((state_.lifecycle() == ThreadState::Lifecycle::INITIAL) ||
      (state_.lifecycle() == ThreadState::Lifecycle::DYING) ||
      (state_.lifecycle() == ThreadState::Lifecycle::DEAD)) {
    return ZX_ERR_BAD_STATE;
  }
  // The mask was already validated by the Profile dispatcher.
  core_thread_->SetSoftCpuAffinity(mask);
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
