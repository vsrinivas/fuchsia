// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/user_thread.h>

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <new.h>
#include <platform.h>
#include <string.h>
#include <trace.h>

#include <lib/dpc.h>

#include <arch/debugger.h>

#include <kernel/auto_lock.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>

#include <magenta/exception.h>
#include <magenta/excp_port.h>
#include <magenta/io_port_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/thread_dispatcher.h>

#define LOCAL_TRACE 0

UserThread::UserThread(mxtl::RefPtr<ProcessDispatcher> process,
                       uint32_t flags)
    : koid_(MX_KOID_INVALID),
      process_(mxtl::move(process)),
      state_tracker_(true, mx_signals_state_t{0u, MX_SIGNAL_SIGNALED}) {
    LTRACE_ENTRY_OBJ;
}

// This is called during initialization after both us and our dispatcher
// have been created.
// N.B. Use of dispatcher_ is potentially racy.
// See UserThread::DispatcherClosed.

void UserThread::set_dispatcher(ThreadDispatcher* dispatcher) {
    dispatcher_ = dispatcher;
    koid_ = dispatcher->get_koid();
 }

UserThread::~UserThread() {
    LTRACE_ENTRY_OBJ;

    DEBUG_ASSERT_MSG(state_ == State::DEAD, "state is %s\n", StateToString(state_));
    DEBUG_ASSERT(&thread_ != get_current_thread());

    // join the LK thread before doing anything else to clean up LK state and ensure
    // the thread we're destroying has stopped.
    LTRACEF("joining LK thread to clean up state\n");
    __UNUSED auto ret = thread_join(&thread_, nullptr, INFINITE_TIME);
    LTRACEF("done joining LK thread\n");
    DEBUG_ASSERT_MSG(ret == NO_ERROR, "thread_join returned something other than NO_ERROR\n");

    cond_destroy(&exception_wait_cond_);
}

// complete initialization of the thread object outside of the constructor
status_t UserThread::Initialize(mxtl::StringPiece name) {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(state_lock_);

    DEBUG_ASSERT(state_ == State::INITIAL);

    // Make sure we can hold process name and thread name combined.
    static_assert((MX_MAX_NAME_LEN * 2) == THREAD_NAME_LENGTH, "name length issue");

    char full_name[THREAD_NAME_LENGTH + 1];
    auto pname = process_->name();
    if ((pname.length() > 0) && (pname.length() < THREAD_NAME_LENGTH)) {
        snprintf(full_name, sizeof(full_name), "%s:%s", pname.data(), name.data());
    } else {
        snprintf(full_name, sizeof(full_name), "<unnamed>:%s", name.data());
    }

    // create an underlying LK thread
    thread_t* lkthread = thread_create_etc(&thread_, full_name, StartRoutine, this, LOW_PRIORITY,
                                           NULL, DEFAULT_STACK_SIZE, NULL);

    if (!lkthread) {
        TRACEF("error creating thread\n");
        return ERR_NO_MEMORY;
    }
    DEBUG_ASSERT(lkthread == &thread_);

    // bump the ref on this object that the LK thread state will now own until the lk thread has exited
    AddRef();

    // register an exit handler with the LK kernel
    thread_set_exit_callback(&thread_, &ThreadExitCallback, reinterpret_cast<void*>(this));

    // set the per-thread pointer
    lkthread->user_thread = reinterpret_cast<void*>(this);

    // associate the proc's address space with this thread
    process_->aspace()->AttachToThread(lkthread);

    // we've entered the initialized state
    SetState(State::INITIALIZED);

    return NO_ERROR;
}

// start a thread
status_t UserThread::Start(uintptr_t entry, uintptr_t sp,
                           uintptr_t arg1, uintptr_t arg2) {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(state_lock_);

    if (state_ != State::INITIALIZED)
        return ERR_BAD_STATE;

    // save the user space entry state
    user_entry_ = entry;
    user_sp_ = sp;
    user_arg1_ = arg1;
    user_arg2_ = arg2;

    // add ourselves to the process, which may fail if the process is in a dead state
    auto ret = process_->AddThread(this);
    if (ret < 0)
        return ret;

    // mark ourselves as running and resume the kernel thread
    SetState(State::RUNNING);

#if WITH_LIB_KTRACE
    thread_.user_tid = dispatcher_->get_koid();
    thread_.user_pid = process_->get_koid();
#endif
    thread_resume(&thread_);

    return NO_ERROR;
}

// called in the context of our thread
void UserThread::Exit() {
    LTRACE_ENTRY_OBJ;

    // only valid to call this on the current thread
    DEBUG_ASSERT(get_current_thread() == &thread_);

    {
        AutoLock lock(state_lock_);

        DEBUG_ASSERT(state_ == State::RUNNING || state_ == State::DYING);

        SetState(State::DYING);
    }

    // exit here
    // this will recurse back to us in ::Exiting()
    thread_exit(0);

    __UNREACHABLE;
}

void UserThread::Kill() {
    LTRACE_ENTRY_OBJ;

    // see if we're already going down.
    // check these ahead of time in case we're recursing from inside an already exiting situation
    // the recursion path is UserThread::Exiting -> ProcessDispatcher::RemoveThread -> clean up handle table ->
    // UserThread::DispatcherClosed -> UserThread::Kill
    if (state_ == State::DYING || state_ == State::DEAD)
        return;

    AutoLock lock(state_lock_);

    // double check that the above check wasn't a race
    if (state_ == State::DYING || state_ == State::DEAD)
        return;

    // deliver a kernel kill signal to the thread
    thread_kill(&thread_, false);

    // enter the dying state
    SetState(State::DYING);
}

void UserThread::DispatcherClosed() {
    LTRACE_ENTRY_OBJ;
    dispatcher_ = nullptr;
    Kill();
}

static void ThreadCleanupDpc(dpc_t *d) {
    LTRACEF("dpc %p\n", d);

    UserThread *t = reinterpret_cast<UserThread *>(d->arg);
    DEBUG_ASSERT(t);

    delete t;
}

void UserThread::Exiting() {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(state_lock_);

    DEBUG_ASSERT(state_ == State::DYING);

    // signal any waiters
    state_tracker_.UpdateSatisfied(0u, MX_SIGNAL_SIGNALED);

    {
        AutoLock lock(exception_lock_);
        if (exception_port_)
            exception_port_->OnThreadExit(this);
    }

    // remove ourselves from our parent process's view
    process_->RemoveThread(this);

    // put ourselves into to dead state
    SetState(State::DEAD);

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

        // queue without reschdule since us exiting is a reschedule event already
        dpc_queue(&cleanup_dpc_, false);
    }

    // after this point the thread will stop permanently
    LTRACE_EXIT_OBJ;
}

// low level LK callback in thread's context just before exiting
void UserThread::ThreadExitCallback(void* arg) {
    UserThread* t = reinterpret_cast<UserThread*>(arg);

    t->Exiting();
}

// low level LK entry point for the thread
int UserThread::StartRoutine(void* arg) {
    LTRACE_ENTRY;

    UserThread* t = (UserThread*)arg;

    // check that the entry point makes sense and we haven't forgotten to set it
    DEBUG_ASSERT(t->user_entry_);

    LTRACEF("arch_enter_uspace SP: %#" PRIxPTR " PC: %#" PRIxPTR
            ", ARG1: %#" PRIxPTR ", ARG2: %#" PRIxPTR "\n",
            t->user_sp_, t->user_entry_, t->user_arg1_, t->user_arg2_);

    // switch to user mode and start the process
    arch_enter_uspace(t->user_entry_, t->user_sp_,
                      t->user_arg1_, t->user_arg2_);

    __UNREACHABLE;
}

void UserThread::SetState(State state) {
    LTRACEF("thread %p: state %u (%s)\n", this, static_cast<unsigned int>(state), StateToString(state));

    DEBUG_ASSERT(state_lock_.IsHeld());

    state_ = state;
}

status_t UserThread::SetExceptionPort(ThreadDispatcher* td, mxtl::RefPtr<ExceptionPort> eport) {
    // Lock both |state_lock_| and |exception_lock_| to ensure the thread
    // doesn't transition to dead while we're setting the exception handler.
    AutoLock state_lock(state_lock_);
    AutoLock excp_lock(exception_lock_);
    if (state_ == State::DEAD)
        return ERR_NOT_FOUND; // TODO(dje): ?
    if (exception_port_)
        return ERR_BAD_STATE; // TODO(dje): ?
    exception_port_ = eport;
    return NO_ERROR;
}

void UserThread::ResetExceptionPort() {
    AutoLock lock(exception_lock_);
    exception_port_.reset();
}

mxtl::RefPtr<ExceptionPort> UserThread::exception_port() {
    AutoLock lock(exception_lock_);
    return exception_port_;
}

status_t UserThread::ExceptionHandlerExchange(mxtl::RefPtr<ExceptionPort> eport,
                                              const mx_exception_report_t* report,
                                              const arch_exception_context_t* arch_context) {
    LTRACE_ENTRY_OBJ;
    AutoLock lock(exception_wait_lock_);

    // So the handler can read/write our general registers.
    thread_.exception_context = arch_context;

    // So various bits know we're stopped in an exception.
    thread_.flags |= THREAD_FLAG_STOPPED_FOR_EXCEPTION;

    exception_status_ = MX_EXCEPTION_STATUS_WAITING;

    // Send message, wait for reply.
    // Note that there is a "race" that we need handle: We need to send the
    // exception report before going to sleep, but what if the receiver of the
    // report gets it and processes it before we are asleep? This is handled by
    // locking exception_wait_lock_ in places where the handler can see/modify
    // thread state.

    status_t status = eport->SendReport(report);
    if (status != NO_ERROR) {
        LTRACEF("SendReport returned %d\n", status);
        exception_status_ = MX_EXCEPTION_STATUS_NOT_HANDLED;
        thread_.exception_context = NULL;
        thread_.flags &= ~THREAD_FLAG_STOPPED_FOR_EXCEPTION;
        return status;
    }
    status = cond_wait_timeout(&exception_wait_cond_, exception_wait_lock_.GetInternal(), INFINITE_TIME);
    DEBUG_ASSERT(status == NO_ERROR);
    DEBUG_ASSERT(exception_status_ != MX_EXCEPTION_STATUS_WAITING);

    thread_.exception_context = NULL;
    thread_.flags &= ~THREAD_FLAG_STOPPED_FOR_EXCEPTION;
    if (exception_status_ != MX_EXCEPTION_STATUS_RESUME)
        return ERR_BAD_STATE;
    return NO_ERROR;
}

status_t UserThread::MarkExceptionHandled(mx_exception_status_t status) {
    LTRACE_ENTRY_OBJ;
    AutoLock lock(exception_wait_lock_);
    if (exception_status_ != MX_EXCEPTION_STATUS_WAITING)
        return ERR_BAD_STATE;
    exception_status_ = status;
    cond_signal(&exception_wait_cond_);
    return NO_ERROR;
}

uint32_t UserThread::get_num_state_kinds() const {
    return arch_num_regsets();
}

// Note: buffer must be sufficiently aligned

status_t UserThread::ReadState(uint32_t state_kind, void* buffer, uint32_t* buffer_len) {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(exception_wait_lock_);

    if (thread_.state != THREAD_BLOCKED ||
        (thread_.flags & THREAD_FLAG_STOPPED_FOR_EXCEPTION) == 0)
        return ERR_BAD_STATE;

    switch (state_kind)
    {
    case MX_THREAD_STATE_REGSET0 ... MX_THREAD_STATE_REGSET9:
        return arch_get_regset(&thread_, state_kind - MX_THREAD_STATE_REGSET0, buffer, buffer_len);
    default:
        return ERR_INVALID_ARGS;
    }
}

// Note: buffer must be sufficiently aligned

status_t UserThread::WriteState(uint32_t state_kind, const void* buffer, uint32_t buffer_len, bool priv) {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(exception_wait_lock_);

    if (thread_.state != THREAD_BLOCKED ||
        (thread_.flags & THREAD_FLAG_STOPPED_FOR_EXCEPTION) == 0)
        return ERR_BAD_STATE;

    switch (state_kind)
    {
    case MX_THREAD_STATE_REGSET0 ... MX_THREAD_STATE_REGSET9:
        return arch_set_regset(&thread_, state_kind - MX_THREAD_STATE_REGSET0, buffer, buffer_len, priv);
    default:
        return ERR_INVALID_ARGS;
    }
}

const char* StateToString(UserThread::State state) {
    switch (state) {
    case UserThread::State::INITIAL:
        return "initial";
    case UserThread::State::INITIALIZED:
        return "initialized";
    case UserThread::State::RUNNING:
        return "running";
    case UserThread::State::DYING:
        return "dying";
    case UserThread::State::DEAD:
        return "dead";
    }
    return "unknown";
}
