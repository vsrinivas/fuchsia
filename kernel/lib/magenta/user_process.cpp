// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/user_process.h>

#include <list.h>
#include <rand.h>
#include <string.h>
#include <trace.h>

#include <kernel/auto_lock.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>

#include <magenta/dispatcher.h>
#include <magenta/futex_context.h>
#include <magenta/magenta.h>
#include <magenta/user_copy.h>

#define LOCAL_TRACE 0

UserProcess::UserProcess(utils::StringPiece name) {
    LTRACE_ENTRY_OBJ;

    id_ = AddProcess(this);

    // Generate handle XOR mask with top bit and bottom two bits cleared
    handle_rand_ = (rand() << 2) & INT_MAX;

    if (name.length() > 0 && (name.length() < sizeof(name_)))
        strlcpy(name_, name.data(), sizeof(name_));
}

UserProcess::~UserProcess() {
    LTRACE_ENTRY_OBJ;

    DEBUG_ASSERT(state_ == State::INITIAL || state_ == State::DEAD);

    // assert that we have no handles, should have been cleaned up in the -> DEAD transition
    DEBUG_ASSERT(handles_.is_empty());

    // remove ourself from the global process list
    RemoveProcess(this);

    mutex_destroy(&state_lock_);
    mutex_destroy(&handle_table_lock_);
    mutex_destroy(&thread_list_lock_);

    LTRACE_EXIT_OBJ;
}

status_t UserProcess::Initialize() {
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

status_t UserProcess::Start(void* arg, mx_vaddr_t entry) {
    LTRACE_ENTRY_OBJ;

    // grab and hold the state lock across this entire routine, since we're
    // effectively transitioning from INITIAL to RUNNING
    AutoLock lock(state_lock_);

    DEBUG_ASSERT(state_ == State::INITIAL);

    // make sure we're in the right state
    if (state_ != State::INITIAL) {
        TRACEF("UserProcess has not been loaded\n");
        return ERR_BAD_STATE;
    }

    if (entry) {
        entry_ = (thread_start_routine)entry;
    }

    // TODO: move the creation of the initial thread to user space
    status_t result;
    // create the first thread
    auto t = utils::AdoptRef(new UserThread(utils::RefPtr<UserProcess>(this), entry_, arg));
    if (!t) {
        result = ERR_NO_MEMORY;
    } else {
        result = t->Initialize(utils::StringPiece("main thread"));
    }

    if (result == NO_ERROR) {
        // we're ready to run now
        SetState(State::RUNNING);
    } else {
        // to be safe, assume process is dead after failed start
        SetState(State::DEAD);
        return result;
    }

    // save a ref to this thread so it doesn't go out of scope
    main_thread_ = t;

    LTRACEF("starting main thread\n");
    t->Start();

    return NO_ERROR;
}

void UserProcess::Exit(int retcode) {
    LTRACE_ENTRY_OBJ;

    {
        AutoLock lock(state_lock_);

        DEBUG_ASSERT(state_ == State::RUNNING);

        retcode_ = retcode;

        // enter the dying state, which should kill all threads
        SetState(State::DYING);
    }

    UserThread::GetCurrent()->Exit();
}

void UserProcess::Kill() {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(state_lock_);

    // we're already dead
    if (state_ == State::DEAD)
        return;

    // if we have no threads, enter the dead state directly
    if (thread_list_.is_empty()) {
        SetState(State::DEAD);
    } else {
        // enter the dying state, which should trigger a thread kill.
        // the last thread exiting will transition us to DEAD
        SetState(State::DYING);
    }
}

// the dispatcher has closed its handle on us, so kill ourselves
void UserProcess::DispatcherClosed() {
    LTRACE_ENTRY_OBJ;

    Kill();
}

void UserProcess::KillAllThreads() {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(&thread_list_lock_);

    for_each(&thread_list_, [](UserThread* thread) {
        LTRACEF("killing thread %p\n", thread);
        thread->Kill();
    });
}

status_t UserProcess::AddThread(UserThread* t) {
    LTRACE_ENTRY_OBJ;

    // cannot add thread to dying/dead state
    if (state_ == State::DYING || state_ == State::DEAD) {
        return ERR_BAD_STATE;
    }

    // add the thread to our list
    AutoLock lock(&thread_list_lock_);
    thread_list_.push_back(t);

    DEBUG_ASSERT(t->process() == this);

    return NO_ERROR;
}

void UserProcess::RemoveThread(UserThread* t) {
    LTRACE_ENTRY_OBJ;

    // we're going to check for state and possibly transition below
    AutoLock state_lock(&state_lock_);

    // remove the thread from our list
    AutoLock lock(&thread_list_lock_);
    thread_list_.remove(t);

    // drop the ref from the main_thread_ pointer if its being removed
    if (t == main_thread_.get()) {
        main_thread_.reset();
    }

    // if this was the last thread, transition directly to DEAD state
    if (thread_list_.is_empty()) {
        LTRACEF("last thread left the process %p, entering DEAD state\n", this);
        SetState(State::DEAD);
    }
}

void UserProcess::SetState(State s) {
    LTRACEF("process %p: state %u (%s)\n", this, static_cast<unsigned int>(s), StateToString(s));

    DEBUG_ASSERT(is_mutex_held(&state_lock_));

    // look for some invalid state transitions
    if (state_ == State::DEAD && s != State::DEAD) {
        panic("UserProcess::SetState invalid state transition from DEAD to !DEAD\n");
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
            while (handles_.first()) {
                auto handle = handles_.pop_front();
                DeleteHandle(handle);
            };
        }
        LTRACEF_LEVEL(2, "done cleaning up handle table on proc %p\n", this);

        // tear down the address space
        aspace_->Destroy();

        // signal waiter
        LTRACEF_LEVEL(2, "signalling waiters\n");
        waiter_.Signal(MX_SIGNAL_SIGNALED);
    }
}

// process handle manipulation routines
mx_handle_t UserProcess::MapHandleToValue(Handle* handle) {
    auto handle_index = MapHandleToU32(handle) + 1;
    return handle_index ^ handle_rand_;
}

Handle* UserProcess::GetHandle_NoLock(mx_handle_t handle_value) {
    auto handle_index = (handle_value ^ handle_rand_) - 1;
    Handle* handle = MapU32ToHandle(handle_index);
    if (!handle)
        return nullptr;
    return (handle->process_id() == id_) ? handle : nullptr;
}

void UserProcess::AddHandle(HandleUniquePtr handle) {
    AutoLock lock(&handle_table_lock_);
    AddHandle_NoLock(utils::move(handle));
}

void UserProcess::AddHandle_NoLock(HandleUniquePtr handle) {
    handle->set_process_id(id_);
    handles_.push_front(handle.release());
}

HandleUniquePtr UserProcess::RemoveHandle(mx_handle_t handle_value) {
    AutoLock lock(&handle_table_lock_);
    return RemoveHandle_NoLock(handle_value);
}

HandleUniquePtr UserProcess::RemoveHandle_NoLock(mx_handle_t handle_value) {
    auto handle = GetHandle_NoLock(handle_value);
    if (!handle)
        return nullptr;
    handles_.remove(handle);
    handle->set_process_id(0u);

    return HandleUniquePtr(handle);
}

void UserProcess::UndoRemoveHandle_NoLock(mx_handle_t handle_value) {
    auto handle_index = (handle_value ^ handle_rand_) - 1;
    Handle* handle = MapU32ToHandle(handle_index);
    AddHandle_NoLock(HandleUniquePtr(handle));
}

bool UserProcess::GetDispatcher(mx_handle_t handle_value, utils::RefPtr<Dispatcher>* dispatcher,
                                uint32_t* rights) {
    AutoLock lock(&handle_table_lock_);
    Handle* handle = GetHandle_NoLock(handle_value);
    if (!handle)
        return false;

    *rights = handle->rights();
    *dispatcher = handle->dispatcher();
    return true;
}

status_t UserProcess::GetInfo(mx_process_info_t* info) {
    info->len = sizeof(mx_process_info_t);

    info->return_code = retcode_;

    return NO_ERROR;
}

mx_tid_t UserProcess::GetNextThreadId() {
    return atomic_add(&next_thread_id_, 1);
}

status_t UserProcess::SetExceptionHandler(utils::RefPtr<Dispatcher> handler, mx_exception_behaviour_t behaviour) {
    AutoLock lock(&exception_lock_);

    exception_handler_ = handler;
    exception_behaviour_ = behaviour;

    return NO_ERROR;
}

utils::RefPtr<Dispatcher> UserProcess::exception_handler() {
    AutoLock lock(&exception_lock_);
    return exception_handler_;
}

uint32_t UserProcess::HandleCount() {
    AutoLock lock(&handle_table_lock_);
    return static_cast<uint32_t>(handles_.size_slow());
}

uint32_t UserProcess::ThreadCount() {
    AutoLock lock(&thread_list_lock_);
    return static_cast<uint32_t>(thread_list_.size_slow());
}

char UserProcess::StateChar() const {
    State s = state();

    switch (s) {
    case State::INITIAL:
        return 'I';
    case State::RUNNING:
        return 'R';
    case State::DYING:
        return 'Y';
    case State::DEAD:
        return 'D';
    }
    return '?';
}

const char* StateToString(UserProcess::State state) {
    switch (state) {
    case UserProcess::State::INITIAL:
        return "initial";
    case UserProcess::State::RUNNING:
        return "running";
    case UserProcess::State::DYING:
        return "dying";
    case UserProcess::State::DEAD:
        return "dead";
    }
    return "unknown";
}
