// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/process_dispatcher.h>

#include <assert.h>
#include <inttypes.h>
#include <list.h>
#include <new.h>
#include <rand.h>
#include <string.h>
#include <trace.h>

#include <kernel/auto_lock.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>

#include <lib/crypto/global_prng.h>

#include <magenta/futex_context.h>
#include <magenta/job_dispatcher.h>
#include <magenta/magenta.h>
#include <magenta/thread_dispatcher.h>
#include <magenta/user_copy.h>
#include <magenta/vm_object_dispatcher.h>

#define LOCAL_TRACE 0


static constexpr mx_rights_t kDefaultProcessRights =
        MX_RIGHT_READ  | MX_RIGHT_WRITE | MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER |
        MX_RIGHT_GET_PROPERTY | MX_RIGHT_SET_PROPERTY | MX_RIGHT_ENUMERATE;



mutex_t ProcessDispatcher::global_process_list_mutex_ =
    MUTEX_INITIAL_VALUE(global_process_list_mutex_);

mxtl::DoublyLinkedList<ProcessDispatcher*, ProcessDispatcher::ProcessListTraits>
    ProcessDispatcher::global_process_list_;

mx_handle_t map_handle_to_value(const Handle* handle, mx_handle_t mixer) {
    // Ensure that the last bit of the result is not zero and that
    // we don't lose upper bits.
    DEBUG_ASSERT((mixer & 0x1) == 0);
    DEBUG_ASSERT((MapHandleToU32(handle) & 0xe0000000) == 0);

    auto handle_id = (MapHandleToU32(handle) << 2) | 0x1;
    return mixer ^ handle_id;
}

Handle* map_value_to_handle(mx_handle_t value, mx_handle_t mixer) {
    auto handle_id = (value ^ mixer) >> 2;
    return MapU32ToHandle(handle_id);
}

mx_status_t ProcessDispatcher::Create(mxtl::RefPtr<JobDispatcher> job,
                                      mxtl::StringPiece name,
                                      mxtl::RefPtr<Dispatcher>* dispatcher,
                                      mx_rights_t* rights, uint32_t flags) {
    AllocChecker ac;
    auto process = new (&ac) ProcessDispatcher(mxtl::move(job), name, flags);
    if (!ac.check())
        return ERR_NO_MEMORY;

    status_t result = process->Initialize();
    if (result != NO_ERROR)
        return result;

    *rights = kDefaultProcessRights;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(process);
    return NO_ERROR;
}

ProcessDispatcher::ProcessDispatcher(mxtl::RefPtr<JobDispatcher> job,
                                     mxtl::StringPiece name,
                                     uint32_t flags)
    : job_(mxtl::move(job)), state_tracker_(0u) {
    LTRACE_ENTRY_OBJ;

    // Add ourself to the global process list.
    AddProcess(this);

    if (job_)
        job_->AddChildProcess(this);

    // Generate handle XOR mask with top bit and bottom two bits cleared
    uint32_t secret;
    auto prng = crypto::GlobalPRNG::GetInstance();
    prng->Draw(&secret, sizeof(secret));

    // Handle values cannot be negative values, so we mask the high bit.
    handle_rand_ = (secret << 2) & INT_MAX;

    if (name.length() > 0 && (name.length() < sizeof(name_)))
        strlcpy(name_, name.data(), sizeof(name_));
}

ProcessDispatcher::~ProcessDispatcher() {
    LTRACE_ENTRY_OBJ;

    DEBUG_ASSERT(state_ == State::INITIAL || state_ == State::DEAD);

    // assert that we have no handles, should have been cleaned up in the -> DEAD transition
    DEBUG_ASSERT(handles_.is_empty());

    if (job_)
        job_->RemoveChildProcess(this);

    // remove ourself from the global process list
    RemoveProcess(this);

    LTRACE_EXIT_OBJ;
}

void ProcessDispatcher::get_name(char out_name[MX_MAX_NAME_LEN]) const {
    AutoSpinLock lock(name_lock_);
    memcpy(out_name, name_, MX_MAX_NAME_LEN);
}

status_t ProcessDispatcher::set_name(const char* name, size_t len) {
    if (len >= MX_MAX_NAME_LEN)
        len = MX_MAX_NAME_LEN - 1;

    AutoSpinLock lock(name_lock_);
    memcpy(name_, name, len);
    memset(name_ + len, 0, MX_MAX_NAME_LEN - len);
    return NO_ERROR;
}

status_t ProcessDispatcher::Initialize() {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(state_lock_);

    DEBUG_ASSERT(state_ == State::INITIAL);

    // create an address space for this process.
    aspace_ = VmAspace::Create(0, nullptr);
    if (!aspace_) {
        TRACEF("error creating address space\n");
        return ERR_NO_MEMORY;
    }

    return NO_ERROR;
}

void ProcessDispatcher::Exit(int retcode) {
    LTRACE_ENTRY_OBJ;

    DEBUG_ASSERT(ProcessDispatcher::GetCurrent() == this);

    {
        AutoLock lock(state_lock_);

        DEBUG_ASSERT(state_ == State::RUNNING);

        retcode_ = retcode;

        // enter the dying state, which should kill all threads
        SetState(State::DYING);
    }

    UserThread::GetCurrent()->Exit();
}

void ProcessDispatcher::Kill() {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(state_lock_);

    // we're already dead
    if (state_ == State::DEAD)
        return;

    if (state_ != State::DYING) {
        // If there isn't an Exit already in progress, set a nonzero exit
        // status so e.g. crashing tests don't appear to have succeeded.
        DEBUG_ASSERT(retcode_ == 0);
        retcode_ = -1;
    }

    // if we have no threads, enter the dead state directly
    if (thread_list_.is_empty()) {
        SetState(State::DEAD);
    } else {
        // enter the dying state, which should trigger a thread kill.
        // the last thread exiting will transition us to DEAD
        SetState(State::DYING);
    }
}

void ProcessDispatcher::KillAllThreads() {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(&thread_list_lock_);

    for (auto& thread : thread_list_) {
        LTRACEF("killing thread %p\n", &thread);
        thread.Kill();
    }

    // Unblock any futexes.
    // This is issued after all threads are marked as DYING so there
    // is no chance of a thread calling FutexWait.
    futex_context_.WakeAll();
}

status_t ProcessDispatcher::AddThread(UserThread* t, bool initial_thread) {
    LTRACE_ENTRY_OBJ;

    AutoLock state_lock(&state_lock_);

    if (initial_thread) {
        if (state_ != State::INITIAL)
            return ERR_BAD_STATE;
    } else {
        // We must not add a thread when in the DYING or DEAD states.
        // Also, we want to ensure that this is not the first thread.
        if (state_ != State::RUNNING)
            return ERR_BAD_STATE;
    }

    // add the thread to our list
    AutoLock lock(&thread_list_lock_);
    DEBUG_ASSERT(thread_list_.is_empty() == initial_thread);
    thread_list_.push_back(t);

    DEBUG_ASSERT(t->process() == this);

    if (initial_thread)
        SetState(State::RUNNING);

    return NO_ERROR;
}

// This is called within thread T's context when it is exiting.

void ProcessDispatcher::RemoveThread(UserThread* t) {
    LTRACE_ENTRY_OBJ;

    {
        AutoLock lock(&exception_lock_);
        if (debugger_exception_port_)
            debugger_exception_port_->OnThreadExit(t);
    }

    // we're going to check for state and possibly transition below
    AutoLock state_lock(&state_lock_);

    // remove the thread from our list
    AutoLock lock(&thread_list_lock_);
    DEBUG_ASSERT(t != nullptr);
    thread_list_.erase(*t);

    // if this was the last thread, transition directly to DEAD state
    if (thread_list_.is_empty()) {
        LTRACEF("last thread left the process %p, entering DEAD state\n", this);
        SetState(State::DEAD);
    }
}


void ProcessDispatcher::AllHandlesClosed() {
    LTRACE_ENTRY_OBJ;

    // check that we're not already entering a dead state
    // note this is checked outside of a mutex to avoid a reentrant case where the
    // process is already being destroyed, the handle table is being cleaned up, and
    // the last ref to itself is being dropped. In that case it recurses into this function
    // and would wedge up if Kill() is called
    if (state_ == State::DYING || state_ == State::DEAD)
        return;

    // last handle going away acts as a kill to the process object
    Kill();
}

mx_koid_t ProcessDispatcher::get_inner_koid() const {
    return job_ ? job_->get_koid() : 0ull;
}

mxtl::RefPtr<JobDispatcher> ProcessDispatcher::job() {
    return job_;
}

void ProcessDispatcher::SetState(State s) {
    LTRACEF("process %p: state %u (%s)\n", this, static_cast<unsigned int>(s), StateToString(s));

    DEBUG_ASSERT(state_lock_.IsHeld());

    // look for some invalid state transitions
    if (state_ == State::DEAD && s != State::DEAD) {
        panic("ProcessDispatcher::SetState invalid state transition from DEAD to !DEAD\n");
        return;
    }

    // transitions to your own state are okay
    if (s == state_)
        return;

    state_ = s;

    if (s == State::DYING) {
        // send kill to all of our threads
        KillAllThreads();
    } else if (s == State::DEAD) {
        // clean up the handle table
        LTRACEF_LEVEL(2, "cleaning up handle table on proc %p\n", this);
        {
            AutoLock lock(&handle_table_lock_);
            Handle* handle;
            while ((handle = handles_.pop_front()) != nullptr) {
                DeleteHandle(handle);
            }
        }
        LTRACEF_LEVEL(2, "done cleaning up handle table on proc %p\n", this);

        // tear down the address space
        aspace_->Destroy();

        // signal waiter
        LTRACEF_LEVEL(2, "signaling waiters\n");
        state_tracker_.UpdateState(0u, MX_TASK_TERMINATED);

        {
            AutoLock lock(&exception_lock_);
            if (exception_port_)
                exception_port_->OnProcessExit(this);
            if (debugger_exception_port_)
                debugger_exception_port_->OnProcessExit(this);
        }
    }
}

// process handle manipulation routines
mx_handle_t ProcessDispatcher::MapHandleToValue(const Handle* handle) const {
    return map_handle_to_value(handle, handle_rand_);
}

Handle* ProcessDispatcher::GetHandle_NoLock(mx_handle_t handle_value) {
    auto handle = map_value_to_handle(handle_value, handle_rand_);
    if (!handle)
        return nullptr;
    return (handle->process_id() == get_koid()) ? handle : nullptr;
}

void ProcessDispatcher::AddHandle(HandleUniquePtr handle) {
    AutoLock lock(&handle_table_lock_);
    AddHandle_NoLock(mxtl::move(handle));
}

void ProcessDispatcher::AddHandle_NoLock(HandleUniquePtr handle) {
    handle->set_process_id(get_koid());
    handles_.push_front(handle.release());
}

HandleUniquePtr ProcessDispatcher::RemoveHandle(mx_handle_t handle_value) {
    AutoLock lock(&handle_table_lock_);
    return RemoveHandle_NoLock(handle_value);
}

HandleUniquePtr ProcessDispatcher::RemoveHandle_NoLock(mx_handle_t handle_value) {
    auto handle = GetHandle_NoLock(handle_value);
    if (!handle)
        return nullptr;
    handles_.erase(*handle);
    handle->set_process_id(0u);

    return HandleUniquePtr(handle);
}

void ProcessDispatcher::UndoRemoveHandle_NoLock(mx_handle_t handle_value) {
    auto handle = map_value_to_handle(handle_value, handle_rand_);
    AddHandle_NoLock(HandleUniquePtr(handle));
}

bool ProcessDispatcher::GetDispatcher(mx_handle_t handle_value,
                                      mxtl::RefPtr<Dispatcher>* dispatcher,
                                      uint32_t* rights) {
    AutoLock lock(&handle_table_lock_);
    Handle* handle = GetHandle_NoLock(handle_value);
    if (!handle)
        return false;

    *rights = handle->rights();
    *dispatcher = handle->dispatcher();
    return true;
}

status_t ProcessDispatcher::GetInfo(mx_info_process_t* info) {
    info->return_code = retcode_;

    return NO_ERROR;
}

status_t ProcessDispatcher::CreateUserThread(mxtl::StringPiece name, uint32_t flags, mxtl::RefPtr<UserThread>* user_thread) {
    AllocChecker ac;
    auto ut = mxtl::AdoptRef(new (&ac) UserThread(mxtl::WrapRefPtr(this),
                                                  flags));
    if (!ac.check())
        return ERR_NO_MEMORY;

    status_t result = ut->Initialize(name.data(), name.length());
    if (result != NO_ERROR)
        return result;

    *user_thread = mxtl::move(ut);
    return NO_ERROR;
}

// Fill in |info| with the current set of threads.
// |num_info_threads| is the number of threads |info| can hold.
// Return the actual number of threads, which may be more than |num_info_threads|.

status_t ProcessDispatcher::GetThreads(mxtl::Array<mx_koid_t>* out_threads) {
    AutoLock lock(&thread_list_lock_);
    size_t n = thread_list_.size_slow();
    mxtl::Array<mx_koid_t> threads;
    AllocChecker ac;
    threads.reset(new (&ac) mx_koid_t[n], n);
    if (!ac.check())
        return ERR_NO_MEMORY;
    size_t i = 0;
    for (auto& thread : thread_list_) {
        threads[i] = thread.get_koid();
        ++i;
    }
    DEBUG_ASSERT(i == n);
    *out_threads = mxtl::move(threads);
    return NO_ERROR;
}

status_t ProcessDispatcher::SetExceptionPort(mxtl::RefPtr<ExceptionPort> eport, bool debugger) {
    // Lock both |state_lock_| and |exception_lock_| to ensure the process
    // doesn't transition to dead while we're setting the exception handler.
    AutoLock state_lock(&state_lock_);
    AutoLock excp_lock(&exception_lock_);
    if (state_ == State::DEAD)
        return ERR_NOT_FOUND; // TODO(dje): ?
    if (debugger) {
        if (debugger_exception_port_)
            return ERR_BAD_STATE; // TODO(dje): ?
        debugger_exception_port_ = eport;
    } else {
        if (exception_port_)
            return ERR_BAD_STATE; // TODO(dje): ?
        exception_port_ = eport;
    }
    return NO_ERROR;
}

void ProcessDispatcher::ResetExceptionPort(bool debugger) {
    AutoLock lock(&exception_lock_);
    if (debugger) {
        debugger_exception_port_.reset();
    } else {
        exception_port_.reset();
    }
}

mxtl::RefPtr<ExceptionPort> ProcessDispatcher::exception_port() {
    AutoLock lock(&exception_lock_);
    return exception_port_;
}

mxtl::RefPtr<ExceptionPort> ProcessDispatcher::debugger_exception_port() {
    AutoLock lock(&exception_lock_);
    return debugger_exception_port_;
}

void ProcessDispatcher::AddProcess(ProcessDispatcher* process) {
    // Don't call any method of |process|, it is not yet fully constructed.
    AutoLock lock(&global_process_list_mutex_);

    global_process_list_.push_back(process);

    LTRACEF("Adding process %p : koid = %" PRIu64 "\n",
            process, process->get_koid());
}

void ProcessDispatcher::RemoveProcess(ProcessDispatcher* process) {
    AutoLock lock(&global_process_list_mutex_);

    DEBUG_ASSERT(process != nullptr);
    global_process_list_.erase(*process);
    LTRACEF("Removing process %p : koid = %" PRIu64 "\n",
            process, process->get_koid());
}

// static
mxtl::RefPtr<ProcessDispatcher> ProcessDispatcher::LookupProcessById(mx_koid_t koid) {
    LTRACE_ENTRY;
    AutoLock lock(&global_process_list_mutex_);
    auto iter = global_process_list_.find_if([koid](const ProcessDispatcher& p) {
                                                return p.get_koid() == koid;
                                             });
    return mxtl::WrapRefPtr(iter.CopyPointer());
}

mxtl::RefPtr<UserThread> ProcessDispatcher::LookupThreadById(mx_koid_t koid) {
    LTRACE_ENTRY_OBJ;
    AutoLock lock(&thread_list_lock_);

    auto iter = thread_list_.find_if([koid](const UserThread& t) { return t.get_koid() == koid; });
    return mxtl::WrapRefPtr(iter.CopyPointer());
}

mx_status_t ProcessDispatcher::set_bad_handle_policy(uint32_t new_policy) {
    if (new_policy > MX_POLICY_BAD_HANDLE_EXIT)
        return ERR_NOT_SUPPORTED;
    bad_handle_policy_ = new_policy;
    return NO_ERROR;
}

const char* StateToString(ProcessDispatcher::State state) {
    switch (state) {
    case ProcessDispatcher::State::INITIAL:
        return "initial";
    case ProcessDispatcher::State::RUNNING:
        return "running";
    case ProcessDispatcher::State::DYING:
        return "dying";
    case ProcessDispatcher::State::DEAD:
        return "dead";
    }
    return "unknown";
}

mx_status_t ProcessDispatcher::BadHandle(mx_handle_t handle_value,
                                         mx_status_t error) {
    // TODO(mcgrathr): Maybe treat other errors the same?
    // This also gets ERR_WRONG_TYPE and ERR_ACCESS_DENIED (for rights checks).
    if (error != ERR_BAD_HANDLE)
        return error;

    // TODO(cpu): Generate an exception when exception handling lands.
    if (get_bad_handle_policy() == MX_POLICY_BAD_HANDLE_EXIT) {
        char name[MX_MAX_NAME_LEN];
        get_name(name);
        printf("\n[fatal: %s used a bad handle]\n", name);
        Exit(error);
    }
    return error;
}
