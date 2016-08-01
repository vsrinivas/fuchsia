// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/user_thread.h>

#include <assert.h>
#include <err.h>
#include <new.h>
#include <platform.h>
#include <string.h>
#include <trace.h>

#include <lib/dpc.h>

#include <kernel/auto_lock.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>

#include <magenta/excp_port.h>
#include <magenta/io_port_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>

#define LOCAL_TRACE 0

UserThread::UserThread(mx_koid_t koid,
                       utils::RefPtr<ProcessDispatcher> process,
                       uint32_t flags)
    : koid_(koid),
      process_(utils::move(process)),
      state_tracker_(true, mx_signals_state_t{0u, MX_SIGNAL_SIGNALED}) {
    LTRACE_ENTRY_OBJ;
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
status_t UserThread::Initialize(utils::StringPiece name) {
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
    thread_.tls[TLS_ENTRY_LKUSER] = reinterpret_cast<uintptr_t>(this);

    // associate the proc's address space with this thread
    process_->aspace()->AttachToThread(lkthread);

    // we've entered the initialized state
    SetState(State::INITIALIZED);

    return NO_ERROR;
}

// start a thread
status_t UserThread::Start(uintptr_t entry, uintptr_t stack, uintptr_t arg) {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(state_lock_);

    if (state_ != State::INITIALIZED)
        return ERR_BAD_STATE;

    // save the user space entry state
    user_entry_ = entry;
    user_stack_ = stack;
    user_arg_ = arg;

    // add ourselves to the process, which may fail if the process is in a dead state
    auto ret = process_->AddThread(this);
    if (ret < 0)
        return ret;

    // mark ourselves as running and resume the kernel thread
    SetState(State::RUNNING);

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

    LTRACEF("arch_enter_uspace SP: 0x%lx PC: 0x%lx, ARG: 0x%lx\n", t->user_stack_, t->user_entry_, t->user_arg_);

    // switch to user mode and start the process
    arch_enter_uspace(reinterpret_cast<vaddr_t>(t->user_entry_),
                      reinterpret_cast<uintptr_t>(t->user_stack_),
                      reinterpret_cast<void *>(t->user_arg_)); // TODO: change enter_uspace api to use uinptr_ts

    __UNREACHABLE;
}

void UserThread::SetState(State state) {
    LTRACEF("thread %p: state %u (%s)\n", this, static_cast<unsigned int>(state), StateToString(state));

    DEBUG_ASSERT(state_lock_.IsHeld());

    state_ = state;
}

status_t UserThread::SetExceptionPort(ThreadDispatcher* td, utils::RefPtr<ExceptionPort> eport) {
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

utils::RefPtr<ExceptionPort> UserThread::exception_port() {
    AutoLock lock(exception_lock_);
    return exception_port_;
}

status_t UserThread::ExceptionHandlerExchange(utils::RefPtr<ExceptionPort> eport, const mx_exception_report_t* report) {
    LTRACE_ENTRY_OBJ;
    AutoLock lock(exception_wait_lock_);
    exception_status_ = MX_EXCEPTION_STATUS_WAITING;
    // Send message, wait for reply.
    status_t status = eport->SendReport(report);
    if (status != NO_ERROR) {
        LTRACEF("SendReport returned %d\n", status);
        exception_status_ = MX_EXCEPTION_STATUS_NOT_HANDLED;
        return status;
    }
    status = cond_wait_timeout(&exception_wait_cond_, exception_wait_lock_.GetInternal(), INFINITE_TIME);
    DEBUG_ASSERT(status == NO_ERROR);
    DEBUG_ASSERT(exception_status_ != MX_EXCEPTION_STATUS_WAITING);
    if (exception_status_ != MX_EXCEPTION_STATUS_RESUME)
        return ERR_BUSY; // TODO(dje): what to use here???
    return NO_ERROR;
}

status_t UserThread::MarkExceptionHandled(mx_exception_status_t status) {
    LTRACE_ENTRY_OBJ;
    AutoLock lock(exception_wait_lock_);
    if (exception_status_ != MX_EXCEPTION_STATUS_WAITING)
        return ERR_NOT_BLOCKED;
    exception_status_ = status;
    cond_signal(&exception_wait_cond_);
    return NO_ERROR;
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
