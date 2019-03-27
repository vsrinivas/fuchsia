// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/thread_dispatcher.h>

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <platform.h>
#include <string.h>
#include <trace.h>

#include <arch/debugger.h>
#include <arch/exception.h>

#include <kernel/thread.h>
#include <vm/kstack.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_paged.h>

#include <zircon/rights.h>
#include <zircon/syscalls/debug.h>
#include <zircon/types.h>

#include <object/excp_port.h>
#include <object/handle.h>
#include <object/job_dispatcher.h>
#include <object/process_dispatcher.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include <lib/counters.h>

#define LOCAL_TRACE 0

KCOUNTER(dispatcher_thread_create_count, "dispatcher.thread.create");
KCOUNTER(dispatcher_thread_destroy_count, "dispatcher.thread.destroy");

// static
zx_status_t ThreadDispatcher::Create(fbl::RefPtr<ProcessDispatcher> process, uint32_t flags,
                                     fbl::StringPiece name,
                                     fbl::RefPtr<Dispatcher>* out_dispatcher,
                                     zx_rights_t* out_rights) {
    // Make sure we contribute a FutexState object to our process's futex state
    // pool before allocating the thread.  If we cannot grow the pool, then we
    // cannot make a thread.
    zx_status_t result = process->futex_context().GrowFutexStatePool();
    if (result != ZX_OK)
        return result;

    fbl::AllocChecker ac;
    auto disp = fbl::AdoptRef(new (&ac) ThreadDispatcher(process, flags));
    if (!ac.check()) {
        // We grew the state pool, but failed to allocate the thread.  Go ahead
        // and shrink the pool back down to size.  Otherwise, the thread class
        // we created will shrink the pool when the time comes
        process->futex_context().ShrinkFutexStatePool();
        return ZX_ERR_NO_MEMORY;
    }

    result = disp->Initialize(name.data(), name.length());
    if (result != ZX_OK)
        return result;

    *out_rights = default_rights();
    *out_dispatcher = ktl::move(disp);
    return ZX_OK;
}

ThreadDispatcher::ThreadDispatcher(fbl::RefPtr<ProcessDispatcher> process,
                                   uint32_t flags)
    : process_(ktl::move(process)) {
    LTRACE_ENTRY_OBJ;

    kcounter_add(dispatcher_thread_create_count, 1);
}

ThreadDispatcher::~ThreadDispatcher() {
    LTRACE_ENTRY_OBJ;

    kcounter_add(dispatcher_thread_destroy_count, 1);

    DEBUG_ASSERT(&thread_ != get_current_thread());

    switch (state_.lifecycle()) {
    case ThreadState::Lifecycle::DEAD: {
        // join the LK thread before doing anything else to clean up LK state and ensure
        // the thread we're destroying has stopped.
        LTRACEF("joining LK thread to clean up state\n");
        __UNUSED auto ret = thread_join(&thread_, nullptr, ZX_TIME_INFINITE);
        LTRACEF("done joining LK thread\n");
        DEBUG_ASSERT_MSG(ret == ZX_OK, "thread_join returned something other than ZX_OK\n");
        break;
    }
    case ThreadState::Lifecycle::INITIAL:
        // this gets a pass, we can destruct a partially constructed thread
        break;
    case ThreadState::Lifecycle::INITIALIZED:
        // as we've been initialized previously, forget the LK thread.
        // note that thread_forget is not called for self since the thread is not running
        thread_forget(&thread_);
        break;
    default:
        DEBUG_ASSERT_MSG(false, "bad state %s, this %p\n",
                         ThreadLifecycleToString(state_.lifecycle()), this);
    }

    event_destroy(&exception_event_);

    process_->futex_context().ShrinkFutexStatePool();
}

// complete initialization of the thread object outside of the constructor
zx_status_t ThreadDispatcher::Initialize(const char* name, size_t len) {
    LTRACE_ENTRY_OBJ;

    Guard<fbl::Mutex> guard{get_lock()};

    // Make sure LK's max name length agrees with ours.
    static_assert(THREAD_NAME_LENGTH == ZX_MAX_NAME_LEN, "name length issue");
    if (len >= ZX_MAX_NAME_LEN)
        len = ZX_MAX_NAME_LEN - 1;

    char thread_name[THREAD_NAME_LENGTH];
    memcpy(thread_name, name, len);
    memset(thread_name + len, 0, ZX_MAX_NAME_LEN - len);

    // create an underlying LK thread
    thread_t* lkthread = thread_create_etc(
        &thread_, thread_name, StartRoutine, this, DEFAULT_PRIORITY, nullptr);

    if (!lkthread) {
        TRACEF("error creating thread\n");
        return ZX_ERR_NO_MEMORY;
    }
    DEBUG_ASSERT(lkthread == &thread_);

    // register an event handler with the LK kernel
    thread_set_user_callback(&thread_, &ThreadUserCallback);

    // set the per-thread pointer
    lkthread->user_thread = this;

    // associate the proc's address space with this thread
    process_->aspace()->AttachToThread(lkthread);

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
    memcpy(thread_.name, name, len);
    memset(thread_.name + len, 0, ZX_MAX_NAME_LEN - len);
    return ZX_OK;
}

void ThreadDispatcher::get_name(char out_name[ZX_MAX_NAME_LEN]) const {
    canary_.Assert();

    Guard<SpinLock, IrqSave> guard{&name_lock_};
    memset(out_name, 0, ZX_MAX_NAME_LEN);
    strlcpy(out_name, thread_.name, ZX_MAX_NAME_LEN);
}

// start a thread
zx_status_t ThreadDispatcher::Start(uintptr_t entry, uintptr_t sp,
                                    uintptr_t arg1, uintptr_t arg2,
                                    bool initial_thread) {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;

    is_initial_thread_ = initial_thread;

    Guard<fbl::Mutex> guard{get_lock()};

    if (state_.lifecycle() != ThreadState::Lifecycle::INITIALIZED)
        return ZX_ERR_BAD_STATE;

    // save the user space entry state
    user_entry_ = entry;
    user_sp_ = sp;
    user_arg1_ = arg1;
    user_arg2_ = arg2;

    // add ourselves to the process, which may fail if the process is in a dead state
    bool suspended;
    auto ret = process_->AddThread(this, initial_thread, &suspended);
    if (ret < 0)
        return ret;

    // update our suspend count to account for our parent state
    if (suspended)
        suspend_count_++;

    // bump the ref on this object that the LK thread state will now own until the lk thread has exited
    AddRef();
    thread_.user_tid = get_koid();
    thread_.user_pid = process_->get_koid();

    // start the thread in RUNNING state, if we're starting suspended it will transition to
    // SUSPENDED when it checks thread signals before executing any user code
    SetStateLocked(ThreadState::Lifecycle::RUNNING);

    if (suspend_count_ == 0) {
        thread_resume(&thread_);
    } else {
        // thread_suspend() only fails if the underlying thread is already dead, which we should
        // ignore here to match the behavior of thread_resume(); our Exiting() callback will run
        // shortly to clean us up
        thread_suspend(&thread_);
    }

    return ZX_OK;
}

// called in the context of our thread
void ThreadDispatcher::Exit() {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;

    // only valid to call this on the current thread
    DEBUG_ASSERT(get_current_thread() == &thread_);

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
        thread_kill(&thread_);

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
            return thread_suspend(&thread_);
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
            thread_resume(&thread_);
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
    // N.B. OnThreadExitForDebugger will block in ExceptionHandlerExchange, so
    // don't hold the process's |state_lock_| across the call.
    {
        fbl::RefPtr<ExceptionPort> eport(process_->debugger_exception_port());
        if (eport) {
            eport->OnThreadExitForDebugger(this);
        }
    }

    // Mark the thread as dead. Do this before removing the thread from the
    // process because if this is the last thread then the process will be
    // marked dead, and we don't want to have a state where the process is
    // dead but one thread is not.
    {
        Guard<fbl::Mutex> guard{get_lock()};

        // put ourselves into the dead state
        SetStateLocked(ThreadState::Lifecycle::DEAD);

        // Drop our exception channel endpoint so any userspace listener
        // gets the PEER_CLOSED signal.
        exceptionate_.ClearChannel();
    }

    // remove ourselves from our parent process's view
    process_->RemoveThread(this);

    // drop LK's reference
    if (Release()) {
        // We're the last reference, so will need to destruct ourself while running, which is not possible
        // Use a dpc to pull this off
        cleanup_dpc_.func = ThreadCleanupDpc;
        cleanup_dpc_.arg = this;

        // disable interrupts before queuing the dpc to prevent starving the DPC thread if it starts running
        // before we're completed.
        // disabling interrupts effectively raises us to maximum priority on this cpu.
        // note this is only safe because we're about to exit the thread permanently so the context
        // switch will effectively reenable interrupts in the new thread.
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

// low level LK callback in thread's context just before exiting
void ThreadDispatcher::ThreadUserCallback(enum thread_user_state_change new_state, thread_t* arg) {
    ThreadDispatcher* t = arg->user_thread;
    DEBUG_ASSERT(t != nullptr);

    switch (new_state) {
    case THREAD_USER_STATE_EXIT:
        t->Exiting();
        return;
    case THREAD_USER_STATE_SUSPEND:
        t->Suspending();
        return;
    case THREAD_USER_STATE_RESUME:
        t->Resuming();
        return;
    }
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
    LTRACEF("arch_enter_uspace SP: %#" PRIxPTR " PC: %#" PRIxPTR
            ", ARG1: %#" PRIxPTR ", ARG2: %#" PRIxPTR "\n",
            t->user_sp_, t->user_entry_, t->user_arg1_, t->user_arg2_);

    // Initialize an iframe for entry into userspace.
    // We need all registers accessible from the ZX_EXCP_THREAD_STARTING
    // exception handler (the debugger wants the thread to look as if the
    // thread is at the first instruction). For architectural exceptions the
    // general regs are left in the iframe for speed and simplicity. To keep
    // things simple we use the same scheme.
    iframe_t iframe{};
    arch_setup_uspace_iframe(&iframe, t->user_entry_, t->user_sp_,
                             t->user_arg1_, t->user_arg2_);

    arch_exception_context_t context{};
    context.frame = &iframe;

    // Notify job debugger if attached.
    if (t->is_initial_thread_) {
      t->process_->OnProcessStartForJobDebugger(t, &context);
    }

    // Notify debugger if attached.
    // This is done by first obtaining our own reference to the port so the
    // test can be done safely. Note that this function doesn't return so we
    // need the reference to go out of scope before then.
    {
        fbl::RefPtr<ExceptionPort> debugger_port(t->process_->debugger_exception_port());
        if (debugger_port) {
            debugger_port->OnThreadStartForDebugger(t, &context);
        }
    }

    thread_process_pending_signals();

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

zx_status_t ThreadDispatcher::SetExceptionPort(fbl::RefPtr<ExceptionPort> eport) {
    canary_.Assert();

    DEBUG_ASSERT(eport->type() == ExceptionPort::Type::THREAD);

    // Lock |state_lock_| to ensure the thread doesn't transition to dead
    // while we're setting the exception handler.
    Guard<fbl::Mutex> guard{get_lock()};
    if (state_.lifecycle() == ThreadState::Lifecycle::DEAD)
        return ZX_ERR_NOT_FOUND;
    if (exception_port_)
        return ZX_ERR_ALREADY_BOUND;
    exception_port_ = eport;

    return ZX_OK;
}

bool ThreadDispatcher::ResetExceptionPort() {
    canary_.Assert();

    fbl::RefPtr<ExceptionPort> eport;

    // Remove the exception handler first. If the thread resumes execution
    // we don't want it to hit another exception and get back into
    // ExceptionHandlerExchange.
    {
        Guard<fbl::Mutex> guard{get_lock()};
        exception_port_.swap(eport);
        if (eport == nullptr) {
            // Attempted to unbind when no exception port is bound.
            return false;
        }
        // This method must guarantee that no caller will return until
        // OnTargetUnbind has been called on the port-to-unbind.
        // This becomes important when a manual unbind races with a
        // PortDispatcher::on_zero_handles auto-unbind.
        //
        // If OnTargetUnbind were called outside of the lock, it would lead to
        // a race (for threads A and B):
        //
        //   A: Calls ResetExceptionPort; acquires the lock
        //   A: Sees a non-null exception_port_, swaps it into the eport local.
        //      exception_port_ is now null.
        //   A: Releases the lock
        //
        //   B: Calls ResetExceptionPort; acquires the lock
        //   B: Sees a null exception_port_ and returns. But OnTargetUnbind()
        //      hasn't yet been called for the port.
        //
        // So, call it before releasing the lock
        eport->OnTargetUnbind();
    }

    OnExceptionPortRemoval(eport);
    return true;
}

fbl::RefPtr<ExceptionPort> ThreadDispatcher::exception_port() {
    canary_.Assert();

    Guard<fbl::Mutex> guard{get_lock()};
    return exception_port_;
}

zx_status_t ThreadDispatcher::ExceptionHandlerExchange(
    fbl::RefPtr<ExceptionPort> eport,
    const zx_exception_report_t* report,
    const arch_exception_context_t* arch_context,
    ThreadState::Exception* out_estatus) {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;

    // Note: As far as userspace is concerned there is no state change that we would notify state
    // tracker observers of, currently.
    //
    // Send message, wait for reply. Note that there is a "race" that we need handle: We need to
    // send the exception report before going to sleep, but what if the receiver of the report gets
    // it and processes it before we are asleep? This is handled by locking state_lock_ in places
    // where the handler can see/modify thread state.

    EnterException(eport, report, arch_context);

    zx_status_t status;

    {
        // The caller may have already done this, but do it again for the
        // one-off callers like the debugger synthetic exceptions.
        AutoBlocked by(Blocked::EXCEPTION);

        // There's no need to send the message under the lock, but we do need to make sure our
        // exception state and blocked state are up to date before sending the message. Otherwise, a
        // debugger could get the packet and observe them before we've updated them. Thus, send the
        // packet after updating both exception state and blocked state.
        status = eport->SendPacket(this, report->header.type);
        if (status != ZX_OK) {
            // Can't send the request to the exception handler. Report the error, which will
            // probably kill the process.
            LTRACEF("SendPacket returned %d\n", status);

            Guard<fbl::Mutex> guard{get_lock()};

            // The report didn't go out, but we're in an exception here so a
            // buggy handler could have tried to respond. Clear any event out.
            event_unsignal(&exception_event_);

            ExitExceptionLocked();
            return status;
        }

        // Continue to wait for the exception response if we get suspended.
        // If it is suspended, the suspension will be processed after the
        // exception response is received (requiring a second resume).
        // Exceptions and suspensions are essentially treated orthogonally.

        do {
            status = event_wait_with_mask(&exception_event_, THREAD_SIGNAL_SUSPEND);
        } while (status == ZX_ERR_INTERNAL_INTR_RETRY);
    }

    Guard<fbl::Mutex> guard{get_lock()};

    // Note: If |status| != ZX_OK, then |state_| is still
    // ThreadState::Exception::UNPROCESSED.
    switch (status) {
    case ZX_OK:
        // Fetch TRY_NEXT/RESUME status.
        *out_estatus = state_.exception();
        DEBUG_ASSERT(*out_estatus == ThreadState::Exception::TRY_NEXT ||
                     *out_estatus == ThreadState::Exception::RESUME);
        break;
    case ZX_ERR_INTERNAL_INTR_KILLED:
        // Thread was killed.
        // Ensure |exception_event_| is no longer signaled.
        event_unsignal(&exception_event_);
        break;
    default:
        ASSERT_MSG(false, "unexpected exception result: %d\n", status);
        __UNREACHABLE;
    }

    ExitExceptionLocked();

    LTRACEF("returning status %d, estatus %d\n",
            status, static_cast<int>(*out_estatus));
    return status;
}

void ThreadDispatcher::EnterException(fbl::RefPtr<ExceptionPort> eport,
                                      const zx_exception_report_t* report,
                                      const arch_exception_context_t* arch_context) {
    Guard<fbl::Mutex> guard{get_lock()};

    // |exception_event_| can't be signaled yet, we're not in an exception yet.
    DEBUG_ASSERT(!event_signaled(&exception_event_));

    // Mark that we're in an exception.
    thread_.exception_context = arch_context;

    // For GetExceptionReport.
    exception_report_ = report;

    // For OnExceptionPortRemoval in case the port is unbound.
    DEBUG_ASSERT(exception_wait_port_ == nullptr);
    exception_wait_port_ = eport;

    state_.set(ThreadState::Exception::UNPROCESSED);
}

void ThreadDispatcher::ExitExceptionLocked() {
    // It's critical that at this point the event no longer be armed.
    // Otherwise the next time we get an exception we'll fall right through
    // without waiting for an exception response.
    // Note: The event could be signaled after event_wait_with_mask returns
    // if the thread was killed while the event was signaled.
    DEBUG_ASSERT(!event_signaled(&exception_event_));

    exception_wait_port_.reset();
    exception_report_ = nullptr;
    thread_.exception_context = nullptr;
    state_.set(ThreadState::Exception::IDLE);
}

zx_status_t ThreadDispatcher::MarkExceptionHandledWorker(PortDispatcher* eport,
                                                         ThreadState::Exception handled_state) {
    canary_.Assert();

    LTRACEF("obj %p, handled_state %d\n", this, static_cast<int>(handled_state));

    Guard<fbl::Mutex> guard{get_lock()};
    if (!InExceptionLocked())
        return ZX_ERR_BAD_STATE;

    // The exception port isn't used directly but is instead proof that the caller has
    // permission to resume from the exception. So validate that it corresponds to the
    // task being resumed.
    if (!exception_wait_port_->PortMatches(eport, /* allow_null */ false))
        return ZX_ERR_ACCESS_DENIED;

    // The thread can be in several states at this point. Alas this is a bit complicated because
    // there is a window in the middle of ExceptionHandlerExchange between the thread going to sleep
    // and after the thread waking up where we can obtain the lock. Things are further complicated
    // by the fact that OnExceptionPortRemoval could get there first, or we might get called a
    // second time for the same exception. It's critical that we don't re-arm the event after the
    // thread wakes up. To keep things simple we take a first-one-wins approach.
    if (state_.exception() != ThreadState::Exception::UNPROCESSED)
        return ZX_ERR_BAD_STATE;

    state_.set(handled_state);
    event_signal(&exception_event_, true);
    return ZX_OK;
}

zx_status_t ThreadDispatcher::MarkExceptionHandled(PortDispatcher* eport) {
    return MarkExceptionHandledWorker(eport, ThreadState::Exception::RESUME);
}

zx_status_t ThreadDispatcher::MarkExceptionNotHandled(PortDispatcher* eport) {
    return MarkExceptionHandledWorker(eport, ThreadState::Exception::TRY_NEXT);
}

void ThreadDispatcher::OnExceptionPortRemoval(const fbl::RefPtr<ExceptionPort>& eport) {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;
    Guard<fbl::Mutex> guard{get_lock()};
    if (!InExceptionLocked())
        return;
    if (exception_wait_port_ == eport) {
        // Leave things alone if already processed. See MarkExceptionHandled.
        if (state_.exception() == ThreadState::Exception::UNPROCESSED) {
            state_.set(ThreadState::Exception::TRY_NEXT);
            event_signal(&exception_event_, true);
        }
    }
}

bool ThreadDispatcher::InExceptionLocked() {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;
    DEBUG_ASSERT(get_lock()->lock().IsHeld());
    return thread_stopped_in_exception(&thread_);
}

zx_status_t ThreadDispatcher::GetInfoForUserspace(zx_info_thread_t* info) {
    canary_.Assert();

    ThreadState state;
    Blocked blocked_reason;
    ExceptionPort::Type excp_port_type;
    // We need to fetch all these values under lock, but once we have them
    // we no longer need the lock.
    {
        Guard<fbl::Mutex> guard{get_lock()};
        state = state_;
        blocked_reason = blocked_reason_;
        if (in_channel_exception_) {
            excp_port_type = channel_exception_wait_type_;
        } else {
            if (InExceptionLocked() &&
                // A port type of !NONE here indicates to the caller that the
                // thread is waiting for an exception response. So don't return
                // !NONE if the thread just woke up but hasn't reacquired
                // |state_lock_|.
                state_.exception() == ThreadState::Exception::UNPROCESSED) {
                DEBUG_ASSERT(exception_wait_port_ != nullptr);
                excp_port_type = exception_wait_port_->type();
            } else {
                // Either we're not in an exception, or we're in the window where
                // event_wait_deadline has woken up but |state_lock_| has
                // not been reacquired.
                DEBUG_ASSERT(exception_wait_port_ == nullptr ||
                             state_.exception() != ThreadState::Exception::UNPROCESSED);
                excp_port_type = ExceptionPort::Type::NONE;
            }
        }
    }

    switch (state.lifecycle()) {
    case ThreadState::Lifecycle::INITIAL:
    case ThreadState::Lifecycle::INITIALIZED:
        info->state = ZX_THREAD_STATE_NEW;
        break;
    case ThreadState::Lifecycle::RUNNING:
        // The thread may be "running" but be blocked in a syscall or
        // exception handler.
        switch (blocked_reason) {
        case Blocked::NONE:
            info->state = ZX_THREAD_STATE_RUNNING;
            break;
        case Blocked::EXCEPTION:
            info->state = ZX_THREAD_STATE_BLOCKED_EXCEPTION;
            break;
        case Blocked::SLEEPING:
            info->state = ZX_THREAD_STATE_BLOCKED_SLEEPING;
            break;
        case Blocked::FUTEX:
            info->state = ZX_THREAD_STATE_BLOCKED_FUTEX;
            break;
        case Blocked::PORT:
            info->state = ZX_THREAD_STATE_BLOCKED_PORT;
            break;
        case Blocked::CHANNEL:
            info->state = ZX_THREAD_STATE_BLOCKED_CHANNEL;
            break;
        case Blocked::WAIT_ONE:
            info->state = ZX_THREAD_STATE_BLOCKED_WAIT_ONE;
            break;
        case Blocked::WAIT_MANY:
            info->state = ZX_THREAD_STATE_BLOCKED_WAIT_MANY;
            break;
        case Blocked::INTERRUPT:
            info->state = ZX_THREAD_STATE_BLOCKED_INTERRUPT;
            break;
        case Blocked::PAGER:
            info->state = ZX_THREAD_STATE_BLOCKED_PAGER;
            break;
        default:
            DEBUG_ASSERT_MSG(false, "unexpected blocked reason: %d",
                             static_cast<int>(blocked_reason));
            break;
        }
        break;
    case ThreadState::Lifecycle::SUSPENDED:
        info->state = ZX_THREAD_STATE_SUSPENDED;
        break;
    case ThreadState::Lifecycle::DYING:
        info->state = ZX_THREAD_STATE_DYING;
        break;
    case ThreadState::Lifecycle::DEAD:
        info->state = ZX_THREAD_STATE_DEAD;
        break;
    default:
        DEBUG_ASSERT_MSG(false, "unexpected run state: %d",
                         static_cast<int>(state.lifecycle()));
        break;
    }

    switch (excp_port_type) {
    case ExceptionPort::Type::NONE:
        info->wait_exception_port_type = ZX_EXCEPTION_PORT_TYPE_NONE;
        break;
    case ExceptionPort::Type::DEBUGGER:
        info->wait_exception_port_type = ZX_EXCEPTION_PORT_TYPE_DEBUGGER;
        break;
    case ExceptionPort::Type::JOB_DEBUGGER:
        info->wait_exception_port_type = ZX_EXCEPTION_PORT_TYPE_JOB_DEBUGGER;
        break;
    case ExceptionPort::Type::THREAD:
        info->wait_exception_port_type = ZX_EXCEPTION_PORT_TYPE_THREAD;
        break;
    case ExceptionPort::Type::PROCESS:
        info->wait_exception_port_type = ZX_EXCEPTION_PORT_TYPE_PROCESS;
        break;
    case ExceptionPort::Type::JOB:
        info->wait_exception_port_type = ZX_EXCEPTION_PORT_TYPE_JOB;
        break;
    default:
        DEBUG_ASSERT_MSG(false, "unexpected exception port type: %d",
                         static_cast<int>(excp_port_type));
        break;
    }

    return ZX_OK;
}

zx_status_t ThreadDispatcher::GetStatsForUserspace(zx_info_thread_stats_t* info) {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;

    *info = {};

    info->total_runtime = runtime_ns();
    return ZX_OK;
}

zx_status_t ThreadDispatcher::GetExceptionReport(zx_exception_report_t* report) {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;
    Guard<fbl::Mutex> guard{get_lock()};
    if (!InExceptionLocked())
        return ZX_ERR_BAD_STATE;
    DEBUG_ASSERT(exception_report_ != nullptr);
    *report = *exception_report_;
    return ZX_OK;
}

zx_status_t ThreadDispatcher::SetExceptionChannel(fbl::RefPtr<ChannelDispatcher> channel) {
    canary_.Assert();

    Guard<fbl::Mutex> guard{get_lock()};

    if (IsDyingOrDeadLocked()) {
        return ZX_ERR_BAD_STATE;
    }
    return exceptionate_.SetChannel(ktl::move(channel));
}

zx_status_t ThreadDispatcher::HandleException(fbl::RefPtr<ExceptionDispatcher> exception,
                                              bool* sent) {
    canary_.Assert();

    *sent = false;

    // Mark as blocked before sending the exception, otherwise a handler could
    // potentially query our state before we've marked ourself blocked.
    ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::EXCEPTION);

    {
        Guard<fbl::Mutex> guard{get_lock()};

        // We send the exception while locked so that if it succeeds we can
        // atomically update our state.
        zx_status_t status = exceptionate_.SendException(exception);
        if (status != ZX_OK) {
            return status;
        }
        *sent = true;

        // This state is needed by GetInfoForUserspace().
        // TODO(ZX-3072): add support for job/process/debug exception handling.
        in_channel_exception_ = true;
        channel_exception_wait_type_ = ExceptionPort::Type::THREAD;
        state_.set(ThreadState::Exception::UNPROCESSED);
    }

    zx_status_t status = exception->WaitForResponse();

    Guard<fbl::Mutex> guard{get_lock()};
    in_channel_exception_ = false;
    channel_exception_wait_type_ = ExceptionPort::Type::NONE;
    state_.set(ThreadState::Exception::IDLE);

    return status;
}

// Note: buffer must be sufficiently aligned

zx_status_t ThreadDispatcher::ReadState(zx_thread_state_topic_t state_kind,
                                        void* buffer, size_t buffer_len) {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;

    // We can't be reading regs while the thread transitions from
    // SUSPENDED to RUNNING.
    Guard<fbl::Mutex> guard{get_lock()};

    if (state_.lifecycle() != ThreadState::Lifecycle::SUSPENDED && !InExceptionLocked())
        return ZX_ERR_BAD_STATE;

    switch (state_kind) {
    case ZX_THREAD_STATE_GENERAL_REGS: {
        if (buffer_len != sizeof(zx_thread_state_general_regs_t))
            return ZX_ERR_INVALID_ARGS;
        return arch_get_general_regs(
            &thread_, static_cast<zx_thread_state_general_regs_t*>(buffer));
    }
    case ZX_THREAD_STATE_FP_REGS: {
        if (buffer_len != sizeof(zx_thread_state_fp_regs_t))
            return ZX_ERR_INVALID_ARGS;
        return arch_get_fp_regs(
            &thread_, static_cast<zx_thread_state_fp_regs_t*>(buffer));
    }
    case ZX_THREAD_STATE_VECTOR_REGS: {
        if (buffer_len != sizeof(zx_thread_state_vector_regs_t))
            return ZX_ERR_INVALID_ARGS;
        return arch_get_vector_regs(
            &thread_, static_cast<zx_thread_state_vector_regs_t*>(buffer));
    }
    case ZX_THREAD_STATE_DEBUG_REGS: {
        if (buffer_len != sizeof(zx_thread_state_debug_regs_t))
            return ZX_ERR_INVALID_ARGS;
        return arch_get_debug_regs(
            &thread_, static_cast<zx_thread_state_debug_regs_t*>(buffer));
    }
    case ZX_THREAD_STATE_SINGLE_STEP: {
        if (buffer_len != sizeof(zx_thread_state_single_step_t))
            return ZX_ERR_INVALID_ARGS;
        bool single_step;
        zx_status_t status = arch_get_single_step(&thread_, &single_step);
        if (status != ZX_OK)
            return status;
        *static_cast<zx_thread_state_single_step_t*>(buffer) =
            static_cast<zx_thread_state_single_step_t>(single_step);
        return ZX_OK;
    }
    default:
        return ZX_ERR_INVALID_ARGS;
    }
}

// Note: buffer must be sufficiently aligned

zx_status_t ThreadDispatcher::WriteState(zx_thread_state_topic_t state_kind,
                                         const void* buffer, size_t buffer_len) {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;

    // We can't be reading regs while the thread transitions from
    // SUSPENDED to RUNNING.
    Guard<fbl::Mutex> guard{get_lock()};

    if (state_.lifecycle() != ThreadState::Lifecycle::SUSPENDED && !InExceptionLocked())
        return ZX_ERR_BAD_STATE;

    switch (state_kind) {
    case ZX_THREAD_STATE_GENERAL_REGS: {
        if (buffer_len != sizeof(zx_thread_state_general_regs_t))
            return ZX_ERR_INVALID_ARGS;
        return arch_set_general_regs(
            &thread_, static_cast<const zx_thread_state_general_regs_t*>(buffer));
    }
    case ZX_THREAD_STATE_FP_REGS: {
        if (buffer_len != sizeof(zx_thread_state_fp_regs_t))
            return ZX_ERR_INVALID_ARGS;
        return arch_set_fp_regs(
            &thread_, static_cast<const zx_thread_state_fp_regs_t*>(buffer));
    }
    case ZX_THREAD_STATE_VECTOR_REGS: {
        if (buffer_len != sizeof(zx_thread_state_vector_regs_t))
            return ZX_ERR_INVALID_ARGS;
        return arch_set_vector_regs(
            &thread_, static_cast<const zx_thread_state_vector_regs_t*>(buffer));
    }
    case ZX_THREAD_STATE_DEBUG_REGS: {
        if (buffer_len != sizeof(zx_thread_state_debug_regs_t))
            return ZX_ERR_INVALID_ARGS;
        return arch_set_debug_regs(
            &thread_, static_cast<const zx_thread_state_debug_regs_t*>(buffer));
    }
    case ZX_THREAD_STATE_SINGLE_STEP: {
        if (buffer_len != sizeof(zx_thread_state_single_step_t))
            return ZX_ERR_INVALID_ARGS;
        const zx_thread_state_single_step_t* single_step =
            static_cast<const zx_thread_state_single_step_t*>(buffer);
        if (*single_step != 0 && *single_step != 1)
            return ZX_ERR_INVALID_ARGS;
        return arch_set_single_step(&thread_, !!*single_step);
    }
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
    thread_set_priority(&thread_, priority);
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
