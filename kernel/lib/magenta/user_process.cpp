// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <rand.h>
#include <string.h>
#include <trace.h>

#include <kernel/auto_lock.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>

#include <list.h>

#include <magenta/dispatcher.h>
#include <magenta/futex_context.h>
#include <magenta/magenta.h>
#include <magenta/user_copy.h>
#include <magenta/user_process.h>

#define LOCAL_TRACE 0

UserProcess::UserProcess(utils::StringPiece name)
    : next_thread_id_(1),
      aspace_(nullptr),
      state_(PROC_STATE_INITIAL),
      retcode_(NO_ERROR),
      exception_behaviour_(MX_EXCEPTION_BEHAVIOUR_DEFAULT),
      prev_(nullptr),
      next_(nullptr),
      name_() {
    LTRACE_ENTRY_OBJ;

    id_ = AddProcess(this);

    list_initialize(&thread_list_);
    mutex_init(&thread_list_lock_);
    mutex_init(&handle_table_lock_);
    mutex_init(&state_lock_);
    mutex_init(&exception_lock_);

    // Generate handle XOR mask with top bit and bottom two bits cleared
    handle_rand_ = (rand() << 2) & INT_MAX;

    if (name.length() > 0 && (name.length() < sizeof(name_)))
        strlcpy(name_, name.data(), sizeof(name_));
}

UserProcess::~UserProcess() {
    LTRACE_ENTRY_OBJ;

    RemoveProcess(this);

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

    mutex_destroy(&state_lock_);
    mutex_destroy(&handle_table_lock_);
    mutex_destroy(&thread_list_lock_);

    LTRACE_EXIT_OBJ;
}

status_t UserProcess::Initialize() {
    LTRACE_ENTRY_OBJ;

    // create an address space for this process.
    aspace_ = VmAspace::Create(0, nullptr);
    if (!aspace_) {
        TRACEF("error creating address space\n");
        return ERR_NO_MEMORY;
    }

    return NO_ERROR;
}

void UserProcess::Close() {
    LTRACE_ENTRY_OBJ;

    // TODO - we should kill all of our threads here, but no mechanism exists yet for that.
    // So for now we block until all the threads have exited.
    // After which, it will be safe to delete this object.
    mutex_acquire(&thread_list_lock_);

    // Join all our running threads
    list_node* node;
    while ((node = list_remove_head(&thread_list_)) != nullptr) {
        // need to release thread_list_lock_ before calling thread_join
        mutex_release(&thread_list_lock_);

        UserThread* thread = containerof(node, UserThread, node_);

        LTRACEF_LEVEL(2, "processs %p waiting on thread %p\n", this, thread);
        thread_join(&thread->thread_, nullptr, INFINITE_TIME);
        LTRACEF_LEVEL(2, "processs %p done waiting on thread %p\n", this, thread);

        mutex_acquire(&thread_list_lock_);

        // need to reacquire before looking at joined_ and detached_
        thread->joined_ = true;
        if (thread->detached_) delete thread;
    }

    mutex_release(&thread_list_lock_);
}

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

status_t UserProcess::Start(void* arg, mx_vaddr_t entry) {
    LTRACE_ENTRY_OBJ;

    {
        AutoLock lock(state_lock_);

        // make sure we're in the right state
        if ((state_ != PROC_STATE_INITIAL) && (state_ != PROC_STATE_LOADED)) {
            TRACEF("UserProcess has not been loaded\n");
            return ERR_BAD_STATE;
        }
        state_ = PROC_STATE_STARTING;
    }

    if (entry) {
        entry_ = (thread_start_routine)entry;
    }

    status_t result;
    // create the first thread
    UserThread* t = new UserThread(this, entry_, arg);
    if (!t) {
        result = ERR_NO_MEMORY;
    } else {
        result = t->Initialize(utils::StringPiece("main thread"));
    }

    {
        AutoLock lock(state_lock_);
        if (result == NO_ERROR) {
            // we're ready to run now
            state_ = PROC_STATE_RUNNING;
        } else {
            // to be safe, assume process is dead after failed start
            Dead_NoLock();
            delete t;
            return result;
        }
    }

    LTRACEF("starting main thread\n");
    t->Start();

    // main thread has no handle, so it can be freed on join
    ThreadDetached(t);

    return NO_ERROR;
}

void UserProcess::Exit(int retcode) {
    LTRACE_ENTRY_OBJ;

    {
        AutoLock lock(state_lock_);

        retcode_ = retcode;
        Dead_NoLock();
    }

    // TODO - kill our other threads here?

    UserThread::GetCurrent()->Exit();
}

void UserProcess::Kill() {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(state_lock_);

    Dead_NoLock();

    // TODO - kill our other threads here
}


void UserProcess::AddThread(UserThread* t) {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(&thread_list_lock_);
    list_add_head(&thread_list_, &t->node_);
}

void UserProcess::ThreadDetached(UserThread* t) {
    LTRACE_ENTRY_OBJ;

    AutoLock lock(&thread_list_lock_);
    t->detached_ = true;

    if (t->joined_)
        delete t;
}

void UserProcess::Dead_NoLock() {
    state_ = PROC_STATE_DEAD;
    waiter_.Signal(MX_SIGNAL_SIGNALED);
}

status_t UserProcess::GetInfo(mx_process_info_t *info) {
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
    return static_cast<uint32_t>(list_length(&thread_list_));
}
