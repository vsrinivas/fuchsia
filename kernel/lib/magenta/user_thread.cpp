// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/user_thread.h>

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <platform.h>
#include <string.h>
#include <trace.h>

#include <lib/dpc.h>

#include <arch/debugger.h>

#include <kernel/auto_lock.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_address_region.h>
#include <kernel/vm/vm_object_paged.h>

#include <magenta/c_user_thread.h>
#include <magenta/exception.h>
#include <magenta/excp_port.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/syscalls/debug.h>
#include <magenta/thread_dispatcher.h>

#include <mxtl/algorithm.h>
#include <mxtl/auto_call.h>

#define LOCAL_TRACE 0

UserThread::UserThread(mxtl::RefPtr<ProcessDispatcher> process,
                       uint32_t flags)
    : koid_(MX_KOID_INVALID),
      process_(mxtl::move(process)),
      state_tracker_(0u) {
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

    DEBUG_ASSERT(&thread_ != get_current_thread());

    switch (state_) {
    case State::DEAD: {
        // join the LK thread before doing anything else to clean up LK state and ensure
        // the thread we're destroying has stopped.
        LTRACEF("joining LK thread to clean up state\n");
        __UNUSED auto ret = thread_join(&thread_, nullptr, INFINITE_TIME);
        LTRACEF("done joining LK thread\n");
        DEBUG_ASSERT_MSG(ret == NO_ERROR, "thread_join returned something other than NO_ERROR\n");
        break;
    }
    case State::INITIAL:
        // this gets a pass, we can destruct a partially constructed thread
        break;
    default:
        DEBUG_ASSERT_MSG(false, "bad state %s, this %p\n", StateToString(state_), this);
    }

    // free the kernel stack
    kstack_mapping_.reset();
    if (kstack_vmar_) {
        kstack_vmar_->Destroy();
        kstack_vmar_.reset();
    }
#if __has_feature(safe_stack)
    unsafe_kstack_mapping_.reset();
    if (unsafe_kstack_vmar_) {
        unsafe_kstack_vmar_->Destroy();
        unsafe_kstack_vmar_.reset();
    }
#endif

    event_destroy(&exception_event_);
}

namespace {

status_t allocate_stack(const mxtl::RefPtr<VmAddressRegion>& vmar, bool unsafe,
                        mxtl::RefPtr<VmMapping>* out_kstack_mapping,
                        mxtl::RefPtr<VmAddressRegion>* out_kstack_vmar) {
    LTRACEF("allocating %s stack\n", unsafe ? "unsafe" : "safe");

    // Create a VMO for our stack
    auto stack_vmo = VmObjectPaged::Create(0, DEFAULT_STACK_SIZE);
    if (!stack_vmo) {
        TRACEF("error allocating %s stack for thread\n",
               unsafe ? "unsafe" : "safe");
        return ERR_NO_MEMORY;
    }

    // create a vmar with enough padding for a page before and after the stack
    const size_t padding_size = PAGE_SIZE;

    mxtl::RefPtr<VmAddressRegion> kstack_vmar;
    auto status = vmar->CreateSubVmar(
        0, 2 * padding_size + DEFAULT_STACK_SIZE, 0,
        VMAR_FLAG_CAN_MAP_SPECIFIC |
        VMAR_FLAG_CAN_MAP_READ |
        VMAR_FLAG_CAN_MAP_WRITE,
        unsafe ? "unsafe_kstack_vmar" : "kstack_vmar",
        &kstack_vmar);
    if (status != NO_ERROR)
        return status;

    // destroy the vmar if we early abort
    // this will also clean up any mappings that may get placed on the vmar
    auto vmar_cleanup = mxtl::MakeAutoCall([&kstack_vmar]() {
            kstack_vmar->Destroy();
        });

    LTRACEF("%s stack vmar at %#" PRIxPTR "\n",
            unsafe ? "unsafe" : "safe", kstack_vmar->base());

    // create a mapping offset padding_size into the vmar we created
    mxtl::RefPtr<VmMapping> kstack_mapping;
    status = kstack_vmar->CreateVmMapping(padding_size, DEFAULT_STACK_SIZE, 0,
                                          VMAR_FLAG_SPECIFIC,
                                          mxtl::move(stack_vmo), 0,
                                          ARCH_MMU_FLAG_PERM_READ |
                                          ARCH_MMU_FLAG_PERM_WRITE,
                                          unsafe ? "unsafe_kstack" : "kstack",
                                          &kstack_mapping);
    if (status != NO_ERROR)
        return status;

    LTRACEF("%s stack mapping at %#" PRIxPTR "\n",
            unsafe ? "unsafe" : "safe", kstack_mapping->base());

    // fault in all the pages so we dont demand fault in the stack
    status = kstack_mapping->MapRange(0, DEFAULT_STACK_SIZE, true);
    if (status != NO_ERROR)
        return status;

    // Cancel the cleanup handler on the vmar since we're about to save a
    // reference to it.
    vmar_cleanup.cancel();
    *out_kstack_mapping = mxtl::move(kstack_mapping);
    *out_kstack_vmar = mxtl::move(kstack_vmar);
    return NO_ERROR;
}

};

// complete initialization of the thread object outside of the constructor
status_t UserThread::Initialize(const char* name, size_t len) {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(&state_lock_);

    DEBUG_ASSERT(state_ == State::INITIAL);

    // Make sure LK's max name length agrees with ours.
    static_assert(THREAD_NAME_LENGTH == MX_MAX_NAME_LEN, "name length issue");

    if (len >= MX_MAX_NAME_LEN)
        len = MX_MAX_NAME_LEN - 1;

    char thread_name[THREAD_NAME_LENGTH];
    memset(thread_name + len, 0, MX_MAX_NAME_LEN - len);
    memcpy(thread_name, name, len);

    // Map the kernel stack somewhere
    auto vmar = VmAspace::kernel_aspace()->RootVmar()->as_vm_address_region();
    DEBUG_ASSERT(!!vmar);

    auto status = allocate_stack(vmar, false, &kstack_mapping_, &kstack_vmar_);
    if (status != NO_ERROR)
        return status;
#if __has_feature(safe_stack)
    status = allocate_stack(vmar, true,
                            &unsafe_kstack_mapping_, &unsafe_kstack_vmar_);
    if (status != NO_ERROR)
        return status;
#endif

    // create an underlying LK thread
    thread_t* lkthread = thread_create_etc(
        &thread_, thread_name, StartRoutine, this, LOW_PRIORITY,
        reinterpret_cast<void *>(kstack_mapping_->base()),
#if __has_feature(safe_stack)
        reinterpret_cast<void *>(unsafe_kstack_mapping_->base()),
#else
        nullptr,
#endif
        DEFAULT_STACK_SIZE, nullptr);

    if (!lkthread) {
        TRACEF("error creating thread\n");
        return ERR_NO_MEMORY;
    }
    DEBUG_ASSERT(lkthread == &thread_);

    // bump the ref on this object that the LK thread state will now own until the lk thread has exited
    AddRef();

    // register an event handler with the LK kernel
    thread_set_user_callback(&thread_, &ThreadUserCallback);

    // set the per-thread pointer
    lkthread->user_thread = reinterpret_cast<void*>(this);

    // associate the proc's address space with this thread
    process_->aspace()->AttachToThread(lkthread);

    // we've entered the initialized state
    SetState(State::INITIALIZED);

    return NO_ERROR;
}

status_t UserThread::set_name(const char* name, size_t len) {
    canary_.Assert();

    if (len >= MX_MAX_NAME_LEN)
        len = MX_MAX_NAME_LEN - 1;

    AutoSpinLock lock(name_lock_);
    memcpy(thread_.name, name, len);
    memset(thread_.name + len, 0, MX_MAX_NAME_LEN - len);
    return NO_ERROR;
}

void UserThread::get_name(char out_name[MX_MAX_NAME_LEN]) {
    canary_.Assert();

    AutoSpinLock lock(name_lock_);
    memcpy(out_name, thread_.name, MX_MAX_NAME_LEN);
}

// start a thread
status_t UserThread::Start(uintptr_t entry, uintptr_t sp,
                           uintptr_t arg1, uintptr_t arg2,
                           bool initial_thread) {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;

    AutoLock lock(&state_lock_);

    if (state_ != State::INITIALIZED)
        return ERR_BAD_STATE;

    // save the user space entry state
    user_entry_ = entry;
    user_sp_ = sp;
    user_arg1_ = arg1;
    user_arg2_ = arg2;

    // add ourselves to the process, which may fail if the process is in a dead state
    auto ret = process_->AddThread(this, initial_thread);
    if (ret < 0)
        return ret;

    // mark ourselves as running and resume the kernel thread
    SetState(State::RUNNING);

    thread_.user_tid = dispatcher_->get_koid();
    thread_.user_pid = process_->get_koid();
    thread_resume(&thread_);

    return NO_ERROR;
}

// called in the context of our thread
void UserThread::Exit() {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;

    // only valid to call this on the current thread
    DEBUG_ASSERT(get_current_thread() == &thread_);

    {
        AutoLock lock(&state_lock_);

        DEBUG_ASSERT(state_ == State::RUNNING || state_ == State::DYING);

        SetState(State::DYING);
    }

    // exit here
    // this will recurse back to us in ::Exiting()
    thread_exit(0);

    __UNREACHABLE;
}

void UserThread::Kill() {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;

    AutoLock lock(&state_lock_);

    // see if we're already going down.
    if (state_ == State::DYING || state_ == State::DEAD)
        return;
    // if we've never been started, then release ourselves.
    if (state_ == State::INITIALIZED) {
        // as we've been initialized previously, forget the LK thread.
        thread_forget(&thread_);
        // reset our state, so that the destructor will properly shut down.
        SetState(State::INITIAL);
        // drop the ref, as the LK thread will not own this object.
        __UNUSED auto ret = Release();
        return;
    }

    // deliver a kernel kill signal to the thread
    thread_kill(&thread_, false);

    // enter the dying state
    SetState(State::DYING);
}

status_t UserThread::Suspend() {
    LTRACE_ENTRY_OBJ;

    return thread_suspend(&thread_);
}

status_t UserThread::Resume() {
    LTRACE_ENTRY_OBJ;

    return thread_resume(&thread_);
}

void UserThread::DispatcherClosed() {
    canary_.Assert();

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
    canary_.Assert();

    LTRACE_ENTRY_OBJ;

    // signal any waiters
    state_tracker_.UpdateState(0u, MX_TASK_TERMINATED);

    {
        AutoLock lock(&exception_lock_);
        if (exception_port_)
            exception_port_->OnThreadExit(this);
        // Note: If an eport is bound, it will have a reference to the
        // ThreadDispatcher and thus keep the object, and the underlying
        // UserThread object, around until someone unbinds the port or closes
        // all handles to its underling PortDispatcher.
    }

    // Notify a debugger if attached. Do this before marking the thread as
    // dead: the debugger expects to see the thread in the DYING state, it may
    // try to read thread registers. The debugger still has to handle the case
    // where the process is also dying (and thus the thread could transition
    // DYING->DEAD from underneath it), but that's life (or death :-)).
    // N.B. OnThreadExitForDebugger will block in ExceptionHandlerExchange, so
    // don't hold the process's |exception_lock_| across the call.
    {
        mxtl::RefPtr<ExceptionPort> eport(process_->debugger_exception_port());
        if (eport) {
            eport->OnThreadExitForDebugger(this);
        }
    }

    // Mark the thread as dead. Do this before removing the thread from the
    // process because if this is the last thread then the process will be
    // marked dead, and we don't want to have a state where the process is
    // dead but one thread is not.
    {
        AutoLock lock(&state_lock_);

        DEBUG_ASSERT(state_ == State::DYING);

        // put ourselves into the dead state
        SetState(State::DEAD);
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

        // queue without reschdule since us exiting is a reschedule event already
        dpc_queue(&cleanup_dpc_, false);
    }

    // after this point the thread will stop permanently
    LTRACE_EXIT_OBJ;
}

void UserThread::Suspending() {
    LTRACE_ENTRY_OBJ;

    // Update the state before sending any notifications out. We want the
    // receiver to see the new state.
    {
        AutoLock lock(&state_lock_);

        DEBUG_ASSERT(state_ == State::RUNNING || state_ == State::DYING);
        if (state_ == State::RUNNING) {
            SetState(State::SUSPENDED);
        }
    }

    // Notify debugger if attached.
    // This is done by first obtaining our own reference to the port so the
    // test can be done safely.
    // TODO(dje): Allow debugger to say whether it wants these.
    {
        mxtl::RefPtr<ExceptionPort> debugger_port(process_->debugger_exception_port());
        if (debugger_port) {
            debugger_port->OnThreadSuspending(this);
        }
    }

    LTRACE_EXIT_OBJ;
}

void UserThread::Resuming() {
    LTRACE_ENTRY_OBJ;

    // Update the state before sending any notifications out. We want the
    // receiver to see the new state.
    {
        AutoLock lock(&state_lock_);

        DEBUG_ASSERT(state_ == State::SUSPENDED || state_ == State::DYING);
        if (state_ == State::SUSPENDED) {
            SetState(State::RUNNING);
        }
    }

    // Notify debugger if attached.
    // This is done by first obtaining our own reference to the port so the
    // test can be done safely.
    // TODO(dje): Allow debugger to say whether it wants these.
    {
        mxtl::RefPtr<ExceptionPort> debugger_port(process_->debugger_exception_port());
        if (debugger_port) {
            debugger_port->OnThreadResuming(this);
        }
    }

    LTRACE_EXIT_OBJ;
}

// low level LK callback in thread's context just before exiting
void UserThread::ThreadUserCallback(enum thread_user_state_change new_state, void* arg) {
    UserThread* t = reinterpret_cast<UserThread*>(arg);

    switch (new_state) {
        case THREAD_USER_STATE_EXIT: t->Exiting(); return;
        case THREAD_USER_STATE_SUSPEND: t->Suspending(); return;
        case THREAD_USER_STATE_RESUME: t->Resuming(); return;
    }
}

// low level LK entry point for the thread
int UserThread::StartRoutine(void* arg) {
    LTRACE_ENTRY;

    UserThread* t = (UserThread*)arg;

    // Notify debugger if attached.
    // This is done by first obtaining our own reference to the port so the
    // test can be done safely. Note that this function doesn't return so we
    // need the reference to go out of scope before then.
    // TODO(dje): Allow debugger to say whether it wants these.
    // A process might want these as well.
    {
        mxtl::RefPtr<ExceptionPort> debugger_port(t->process_->debugger_exception_port());
        if (debugger_port) {
            debugger_port->OnThreadStart(t);
        }
    }

    LTRACEF("arch_enter_uspace SP: %#" PRIxPTR " PC: %#" PRIxPTR
            ", ARG1: %#" PRIxPTR ", ARG2: %#" PRIxPTR "\n",
            t->user_sp_, t->user_entry_, t->user_arg1_, t->user_arg2_);

    // switch to user mode and start the process
    arch_enter_uspace(t->user_entry_, t->user_sp_,
                      t->user_arg1_, t->user_arg2_);

    __UNREACHABLE;
}

void UserThread::SetState(State state) {
    canary_.Assert();

    LTRACEF("thread %p: state %u (%s)\n", this, static_cast<unsigned int>(state), StateToString(state));

    DEBUG_ASSERT(state_lock_.IsHeld());

    state_ = state;
}

status_t UserThread::SetExceptionPort(ThreadDispatcher* td, mxtl::RefPtr<ExceptionPort> eport) {
    canary_.Assert();

    DEBUG_ASSERT(eport->type() == ExceptionPort::Type::THREAD);

    // Lock both |state_lock_| and |exception_lock_| to ensure the thread
    // doesn't transition to dead while we're setting the exception handler.
    AutoLock state_lock(&state_lock_);
    AutoLock excp_lock(&exception_lock_);
    if (state_ == State::DEAD)
        return ERR_NOT_FOUND; // TODO(dje): ?
    if (exception_port_)
        return ERR_BAD_STATE; // TODO(dje): ?
    exception_port_ = eport;

    return NO_ERROR;
}

bool UserThread::ResetExceptionPort(bool quietly) {
    canary_.Assert();

    mxtl::RefPtr<ExceptionPort> eport;

    // Remove the exception handler first. If the thread resumes execution
    // we don't want it to hit another exception and get back into
    // ExceptionHandlerExchange.
    {
        AutoLock lock(&exception_lock_);
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

    if (!quietly)
        OnExceptionPortRemoval(eport);
    return true;
}

mxtl::RefPtr<ExceptionPort> UserThread::exception_port() {
    canary_.Assert();

    AutoLock lock(&exception_lock_);
    return exception_port_;
}

status_t UserThread::ExceptionHandlerExchange(
        mxtl::RefPtr<ExceptionPort> eport,
        const mx_exception_report_t* report,
        const arch_exception_context_t* arch_context,
        ExceptionStatus *out_estatus) {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;

    {
        AutoLock lock(&exception_wait_lock_);

        // Send message, wait for reply.
        // Note that there is a "race" that we need handle: We need to send the
        // exception report before going to sleep, but what if the receiver of the
        // report gets it and processes it before we are asleep? This is handled by
        // locking exception_wait_lock_ in places where the handler can see/modify
        // thread state.

        status_t status = eport->SendReport(report);
        if (status != NO_ERROR) {
            LTRACEF("SendReport returned %d\n", status);
            // Treat the exception as unhandled.
            *out_estatus = ExceptionStatus::TRY_NEXT;
            return NO_ERROR;
        }

        // Mark that we're in an exception.
        thread_.exception_context = arch_context;

        // For GetExceptionReport.
        exception_report_ = report;

        // For OnExceptionPortRemoval in case the port is unbound.
        DEBUG_ASSERT(exception_wait_port_ == nullptr);
        exception_wait_port_ = eport;

        exception_status_ = ExceptionStatus::UNPROCESSED;
    }

    // Continue to wait for the exception response if we get suspended.
    // If it is suspended, the suspension will be processed after the
    // exception response is received (requiring a second resume).
    // Exceptions and suspensions are essentially treated orthogonally.

    status_t status;
    do {
        status = event_wait_deadline(&exception_event_, INFINITE_TIME, true);
    } while (status == ERR_INTERRUPTED_RETRY);

    AutoLock lock(&exception_wait_lock_);

    // Note: If |status| != NO_ERROR, then |exception_status_| is still
    // ExceptionStatus::UNPROCESSED.
    switch (status) {
    case NO_ERROR:
        // It's critical that at this point the event no longer be armed.
        // Otherwise the next time we get an exception we'll fall right through
        // without waiting for an exception response.
        // Note: The event could be signaled after event_wait_deadline returns
        // if the thread was killed while the event was signaled.
        DEBUG_ASSERT(!event_signaled(&exception_event_));
        DEBUG_ASSERT(exception_status_ != ExceptionStatus::IDLE &&
                     exception_status_ != ExceptionStatus::UNPROCESSED);
        break;
    case ERR_INTERRUPTED:
        // Thread was killed.
        break;
    default:
        ASSERT_MSG(false, "unexpected exception result: %d\n", status);
        __UNREACHABLE;
    }

    exception_wait_port_.reset();
    exception_report_ = nullptr;
    thread_.exception_context = nullptr;

    *out_estatus = exception_status_;
    exception_status_ = ExceptionStatus::IDLE;

    LTRACEF("returning status %d, estatus %d\n",
            status, static_cast<int>(*out_estatus));
    return status;
}

status_t UserThread::MarkExceptionHandled(ExceptionStatus estatus) {
    canary_.Assert();

    LTRACEF("obj %p, estatus %d\n", this, static_cast<int>(estatus));
    DEBUG_ASSERT(estatus != ExceptionStatus::IDLE &&
                 estatus != ExceptionStatus::UNPROCESSED);

    AutoLock lock(&exception_wait_lock_);
    if (!InExceptionLocked())
        return ERR_BAD_STATE;

    // The thread can be in several states at this point. Alas this is a bit
    // complicated because there is a window in the middle of
    // ExceptionHandlerExchange between the thread going to sleep and after
    // the thread waking up where we can obtain the lock. Things are further
    // complicated by the fact that OnExceptionPortRemoval could get there
    // first, or we might get called a second time for the same exception.
    // It's critical that we don't re-arm the event after the thread wakes up.
    // To keep things simple we take a first-one-wins approach.
    DEBUG_ASSERT(exception_status_ != ExceptionStatus::IDLE);
    if (exception_status_ != ExceptionStatus::UNPROCESSED)
        return ERR_BAD_STATE;

    exception_status_ = estatus;
    event_signal(&exception_event_, true);
    return NO_ERROR;
}

void UserThread::OnExceptionPortRemoval(const mxtl::RefPtr<ExceptionPort>& eport) {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;
    AutoLock lock(&exception_wait_lock_);
    if (!InExceptionLocked())
        return;
    DEBUG_ASSERT(exception_status_ != ExceptionStatus::IDLE);
    if (exception_wait_port_ == eport) {
        // Leave things alone if already processed. See MarkExceptionHandled.
        if (exception_status_ == ExceptionStatus::UNPROCESSED) {
            exception_status_ = ExceptionStatus::TRY_NEXT;
            event_signal(&exception_event_, true);
        }
    }
}

bool UserThread::InExceptionLocked() {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;
    DEBUG_ASSERT(exception_wait_lock_.IsHeld());
    return thread_stopped_in_exception(&thread_);
}

void UserThread::GetInfoForUserspace(mx_info_thread_t* info) {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;

    *info = {};

    UserThread::State state;
    enum thread_state lk_state;
    ExceptionPort::Type excp_port_type;
    // We need to fetch all these values under lock, but once we have them
    // we no longer need the lock.
    {
        // N.B. Keep the order of obtaining these locks consistent.
        AutoLock state_lock(&state_lock_);
        AutoLock lock(&exception_wait_lock_);
        state = state_;
        lk_state = thread_.state;
        if (InExceptionLocked() &&
                // A port type of !NONE here indicates to the caller that the
                // thread is waiting for an exception response. So don't return
                // !NONE if the thread just woke up but hasn't reacquired
                // |exception_wait_lock_|.
                exception_status_ == ExceptionStatus::UNPROCESSED) {
            DEBUG_ASSERT(exception_wait_port_ != nullptr);
            excp_port_type = exception_wait_port_->type();
        } else {
            DEBUG_ASSERT(exception_wait_port_ == nullptr);
            excp_port_type = ExceptionPort::Type::NONE;
        }
    }

    switch (state) {
    case UserThread::State::INITIAL:
    case UserThread::State::INITIALIZED:
        info->state = MX_THREAD_STATE_NEW;
        break;
    case UserThread::State::RUNNING:
        // The thread may be "running" but be blocked in a syscall or
        // exception handler.
        switch (lk_state) {
        case THREAD_BLOCKED:
            info->state = MX_THREAD_STATE_BLOCKED;
            break;
        default:
            info->state = MX_THREAD_STATE_RUNNING;
            break;
        }
        break;
    case UserThread::State::SUSPENDED:
        info->state = MX_THREAD_STATE_SUSPENDED;
        break;
    case UserThread::State::DYING:
        info->state = MX_THREAD_STATE_DYING;
        break;
    case UserThread::State::DEAD:
        info->state = MX_THREAD_STATE_DEAD;
        break;
    default:
        DEBUG_ASSERT_MSG(false, "unexpected run state: %d",
                         static_cast<int>(state));
        break;
    }

    switch (excp_port_type) {
    case ExceptionPort::Type::NONE:
        info->wait_exception_port_type = MX_EXCEPTION_PORT_TYPE_NONE;
        break;
    case ExceptionPort::Type::DEBUGGER:
        info->wait_exception_port_type = MX_EXCEPTION_PORT_TYPE_DEBUGGER;
        break;
    case ExceptionPort::Type::THREAD:
        info->wait_exception_port_type = MX_EXCEPTION_PORT_TYPE_THREAD;
        break;
    case ExceptionPort::Type::PROCESS:
        info->wait_exception_port_type = MX_EXCEPTION_PORT_TYPE_PROCESS;
        break;
    case ExceptionPort::Type::SYSTEM:
        info->wait_exception_port_type = MX_EXCEPTION_PORT_TYPE_SYSTEM;
        break;
    default:
        DEBUG_ASSERT_MSG(false, "unexpected exception port type: %d",
                         static_cast<int>(excp_port_type));
        break;
    }
}

void UserThread::GetStatsForUserspace(mx_info_thread_stats_t* info) {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;

    *info = {};

    info->total_runtime = runtime_ns();
}

status_t UserThread::GetExceptionReport(mx_exception_report_t* report) {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;
    AutoLock lock(&exception_wait_lock_);
    if (!InExceptionLocked())
        return ERR_BAD_STATE;
    DEBUG_ASSERT(exception_report_ != nullptr);
    *report = *exception_report_;
    return NO_ERROR;
}

uint32_t UserThread::get_num_state_kinds() const {
    return arch_num_regsets();
}

// Note: buffer must be sufficiently aligned

status_t UserThread::ReadState(uint32_t state_kind, void* buffer, uint32_t* buffer_len) {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;

    // We can't be reading regs while the thread transitions from
    // SUSPENDED to RUNNING.
    // TODO(dje): Is it ok to use this lock here?
    AutoLock state_lock(&state_lock_);

    // OTOH, the thread may be in an exception.
    // TODO(dje): Can we reduce this to just one lock?
    AutoLock exception_wait_lock(&exception_wait_lock_);

    if (state_ != State::SUSPENDED && !InExceptionLocked())
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
    canary_.Assert();

    LTRACE_ENTRY_OBJ;

    // We can't be reading regs while the thread transitions from
    // SUSPENDED to RUNNING.
    // TODO(dje): Is it ok to use this lock here?
    AutoLock state_lock(&state_lock_);

    // OTOH, the thread may be in an exception.
    // TODO(dje): Can we reduce this to just one lock?
    AutoLock exception_wait_lock(&exception_wait_lock_);

    if (state_ != State::SUSPENDED && !InExceptionLocked())
        return ERR_BAD_STATE;

    switch (state_kind)
    {
    case MX_THREAD_STATE_REGSET0 ... MX_THREAD_STATE_REGSET9:
        return arch_set_regset(&thread_, state_kind - MX_THREAD_STATE_REGSET0, buffer, buffer_len, priv);
    default:
        return ERR_INVALID_ARGS;
    }
}

void magenta_thread_process_name(void* user_thread, char out_name[MX_MAX_NAME_LEN]) {
    UserThread* ut = reinterpret_cast<UserThread*>(user_thread);
    ut->process()->get_name(out_name);
}

const char* StateToString(UserThread::State state) {
    switch (state) {
    case UserThread::State::INITIAL:
        return "initial";
    case UserThread::State::INITIALIZED:
        return "initialized";
    case UserThread::State::RUNNING:
        return "running";
    case UserThread::State::SUSPENDED:
        return "suspended";
    case UserThread::State::DYING:
        return "dying";
    case UserThread::State::DEAD:
        return "dead";
    }
    return "unknown";
}
