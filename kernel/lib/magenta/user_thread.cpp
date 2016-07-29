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

#include <magenta/magenta.h>
#include <magenta/msg_pipe_dispatcher.h>
#include <magenta/process_dispatcher.h>

#define LOCAL_TRACE 0

UserThread::UserThread(mx_koid_t koid,
                       utils::RefPtr<ProcessDispatcher> process,
                       thread_start_routine entry, void* arg)
    : koid_(koid),
      process_(utils::move(process)),
      entry_(entry),
      arg_(arg),
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

    process_->aspace()->FreeRegion(reinterpret_cast<vaddr_t>(user_stack_));
    cond_destroy(&exception_wait_cond_);
    mutex_destroy(&exception_wait_lock_);
}

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

void UserThread::Start() {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(state_lock_);

    DEBUG_ASSERT(state_ == State::INITIALIZED);

    __UNUSED auto ret = process_->AddThread(this);

    // XXX deal with this case differently
    DEBUG_ASSERT(ret == NO_ERROR);

    SetState(State::RUNNING);

    thread_resume(&thread_);
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
    state_tracker_.UpdateSatisfied(MX_SIGNAL_SIGNALED, 0u);

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

    // create a user stack for the new thread
    auto err = t->process_->aspace()->Alloc("user stack", kDefaultStackSize, &t->user_stack_, PAGE_SIZE_SHIFT, 0,
                                            ARCH_MMU_FLAG_PERM_USER | ARCH_MMU_FLAG_PERM_NO_EXECUTE);
    LTRACEF("alloc returns %d, stack at %p\n", err, t->user_stack_);

    LTRACEF("arch_enter_uspace SP: %p PC: %p\n", t->user_stack_, t->entry_);
    // switch to user mode and start the process
    arch_enter_uspace(reinterpret_cast<vaddr_t>(t->entry_),
                      reinterpret_cast<uintptr_t>(t->user_stack_) + kDefaultStackSize, t->arg_);

    __UNREACHABLE;
}

void UserThread::SetState(State state) {
    LTRACEF("thread %p: state %u (%s)\n", this, static_cast<unsigned int>(state), StateToString(state));

    DEBUG_ASSERT(is_mutex_held(&state_lock_));

    state_ = state;
}

// Exception handling below

status_t UserThread::SetExceptionHandler(utils::RefPtr<Dispatcher> handler, mx_exception_behaviour_t behaviour) {
    AutoLock lock(&exception_lock_);

    exception_handler_ = handler;
    exception_behaviour_ = behaviour;

    return NO_ERROR;
}

utils::RefPtr<Dispatcher> UserThread::exception_handler() {
    AutoLock lock(&exception_lock_);
    return exception_handler_;
}

static status_t send_exception_report(utils::RefPtr<Dispatcher> dispatcher, const mx_exception_report_t* report) {
    LTRACE_ENTRY;

    utils::Array<uint8_t> data;
    utils::Array<Handle*> handles;

    AllocChecker ac;
    uint8_t* report_bytes = new (&ac) uint8_t[sizeof(*report)];
    if (!ac.check())
        return ERR_NO_MEMORY;

    memcpy(report_bytes, report, sizeof(*report));
    data.reset(report_bytes, sizeof(*report));

    // TODO(cpu): We should rather deal RefPtr<MessagePipeDispatcher> in the exception code.
    auto message_pipe = dispatcher->get_message_pipe_dispatcher();
    DEBUG_ASSERT(message_pipe);

    status_t status = message_pipe->Write(utils::move(data), utils::move(handles));
    if (status != NO_ERROR)
        LTRACEF("dispatcher->Write returned %d\n", status);
    return status;
}

status_t UserThread::WaitForExceptionHandler(utils::RefPtr<Dispatcher> dispatcher, const mx_exception_report_t* report) {
    LTRACE_ENTRY_OBJ;
    AutoLock lock(&exception_wait_lock_);
    // Status if handler disappears.
    exception_status_ = MX_EXCEPTION_STATUS_NOT_HANDLED;
    // Send message, wait for reply.
    status_t status = send_exception_report(dispatcher, report);
    if (status != NO_ERROR) {
        LTRACEF("send_exception_report returned %d\n", status);
        return status;
    }
    status = cond_wait_timeout(&exception_wait_cond_, &exception_wait_lock_, INFINITE_TIME);
    DEBUG_ASSERT(status == NO_ERROR);
    if (exception_status_ != MX_EXCEPTION_STATUS_RESUME)
        return ERR_INTERNAL;
    return NO_ERROR;
}

void UserThread::WakeFromExceptionHandler(mx_exception_status_t status) {
    LTRACE_ENTRY_OBJ;
    AutoLock lock(&exception_wait_lock_);
    exception_status_ = status;
    cond_signal(&exception_wait_cond_);
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
