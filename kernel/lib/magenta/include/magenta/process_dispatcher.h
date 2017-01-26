// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/event.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <kernel/vm/vm_aspace.h>

#include <magenta/dispatcher.h>
#include <magenta/futex_context.h>
#include <magenta/handle_owner.h>
#include <magenta/magenta.h>
#include <magenta/state_tracker.h>
#include <magenta/syscalls/object.h>
#include <magenta/types.h>
#include <magenta/user_thread.h>

#include <mxtl/array.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/string_piece.h>

class JobDispatcher;

class ProcessDispatcher : public Dispatcher {
public:
    static mx_status_t Create(
        mxtl::RefPtr<JobDispatcher> job, mxtl::StringPiece name, uint32_t flags,
        mxtl::RefPtr<Dispatcher>* dispatcher, mx_rights_t* rights,
        mxtl::RefPtr<VmAddressRegionDispatcher>* root_vmar_disp,
        mx_rights_t* root_vmar_rights);

    // Traits to belong in the global list of processes.
    struct ProcessListTraits {
        static mxtl::DoublyLinkedListNodeState<ProcessDispatcher*>& node_state(
            ProcessDispatcher& obj) {
            return obj.dll_process_;
        }
    };

    // Traits to belong in the parent job's list.
    struct JobListTraits {
        static mxtl::DoublyLinkedListNodeState<ProcessDispatcher*>& node_state(
            ProcessDispatcher& obj) {
            return obj.dll_job_;
        }
    };

    static ProcessDispatcher* GetCurrent() {
        UserThread* current = UserThread::GetCurrent();
        DEBUG_ASSERT(current);
        return current->process();
    }

    // Dispatcher implementation
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_PROCESS; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }
    void on_zero_handles() final;
    mx_koid_t get_inner_koid() const final;

    ~ProcessDispatcher() final;

    // state of the process
    enum class State {
        INITIAL, // initial state, no thread present in process
        RUNNING, // first thread has started and is running
        DYING,   // process has delivered kill signal to all threads
        DEAD,    // all threads have entered DEAD state and potentially dropped refs on process
    };

    // Performs initialization on a newly constructed ProcessDispatcher
    // If this fails, then the object is invalid and should be deleted
    status_t Initialize();

    // Map a |handle| to an integer which can be given to usermode as a
    // handle value. Uses MapHandleToU32() plus additional mixing.
    mx_handle_t MapHandleToValue(const Handle* handle) const;
    mx_handle_t MapHandleToValue(const HandleOwner& handle) const;

    // Maps a handle value into a Handle as long we can verify that
    // it belongs to this process.
    Handle* GetHandleLocked(mx_handle_t handle_value) TA_REQ(handle_table_lock_);

    // Adds |handle| to this process handle list. The handle->process_id() is
    // set to this process id().
    void AddHandle(HandleOwner handle);
    void AddHandleLocked(HandleOwner handle) TA_REQ(handle_table_lock_);

    // Removes the Handle corresponding to |handle_value| from this process
    // handle list.
    HandleOwner RemoveHandle(mx_handle_t handle_value);
    HandleOwner RemoveHandleLocked(mx_handle_t handle_value) TA_REQ(handle_table_lock_);

    // Puts back the |handle_value| which has not yet been given to another process
    // back into this process.
    void UndoRemoveHandleLocked(mx_handle_t handle_value) TA_REQ(handle_table_lock_);

    bool GetDispatcher(mx_handle_t handle_value, mxtl::RefPtr<Dispatcher>* dispatcher,
                       uint32_t* rights);

    template <typename T>
    mx_status_t GetDispatcher(mx_handle_t handle_value,
                              mxtl::RefPtr<T>* dispatcher,
                              mx_rights_t* out_rights) {
        mxtl::RefPtr<Dispatcher> generic_dispatcher;
        if (!GetDispatcher(handle_value, &generic_dispatcher, out_rights))
            return BadHandle(handle_value, ERR_BAD_HANDLE);
        *dispatcher = DownCastDispatcher<T>(&generic_dispatcher);
        if (!*dispatcher)
            return BadHandle(handle_value, ERR_WRONG_TYPE);
        return NO_ERROR;
    }

    template <typename T>
    mx_status_t GetDispatcher(mx_handle_t handle_value,
                              mxtl::RefPtr<T>* dispatcher,
                              mx_rights_t check_rights = 0) {
        mx_rights_t rights;
        mx_status_t status = GetDispatcher(handle_value, dispatcher, &rights);
        if (status == NO_ERROR && check_rights &&
            !magenta_rights_check(rights, check_rights)) {
            dispatcher->reset();
            status = BadHandle(handle_value, ERR_ACCESS_DENIED);
        }
        return status;
    }

    // Called when this process attempts to use an invalid handle,
    // a handle of the wrong type, or a handle with insufficient rights.
    mx_status_t BadHandle(mx_handle_t handle_value, mx_status_t error);

    // accessors
    Mutex& handle_table_lock() TA_RET_CAP(handle_table_lock_) { return handle_table_lock_; }
    FutexContext* futex_context() { return &futex_context_; }
    StateTracker* state_tracker() { return &state_tracker_; }
    State state() const;
    mxtl::RefPtr<VmAspace> aspace() { return aspace_; }
    mxtl::RefPtr<JobDispatcher> job();

    void get_name(char out_name[MX_MAX_NAME_LEN]) const final;
    status_t set_name(const char* name, size_t len) final;

    void Exit(int retcode);
    void Kill();

    status_t GetInfo(mx_info_process_t* info);

    status_t CreateUserThread(mxtl::StringPiece name, uint32_t flags, mxtl::RefPtr<UserThread>* user_thread);

    status_t GetThreads(mxtl::Array<mx_koid_t>* threads);

    // exception handling support
    status_t SetExceptionPort(mxtl::RefPtr<ExceptionPort> eport, bool debugger);
    void ResetExceptionPort(bool debugger);
    mxtl::RefPtr<ExceptionPort> exception_port();
    mxtl::RefPtr<ExceptionPort> debugger_exception_port();

    // The following two methods can be slow and innacurrate and should only be
    // called from diagnostics code.
    uint32_t ThreadCount() const;
    size_t PageCount() const;

    // Look up a process given its koid.
    // Returns nullptr if not found.
    static mxtl::RefPtr<ProcessDispatcher> LookupProcessById(mx_koid_t koid);

    // Look up a thread in this process given its koid.
    // Returns nullptr if not found.
    mxtl::RefPtr<UserThread> LookupThreadById(mx_koid_t koid);

    uint32_t get_bad_handle_policy() const { return bad_handle_policy_; }
    mx_status_t set_bad_handle_policy(uint32_t new_policy);

private:
    // The diagnostic code is allow to know about the internals of this code.
    friend void DumpProcessList();
    friend uint32_t BuildHandleStats(const ProcessDispatcher&, uint32_t*, size_t);
    friend void DumpProcessHandles(mx_koid_t id);
    friend void KillProcess(mx_koid_t id);
    friend void DumpProcessMemoryUsage(const char* prefix, size_t min_pages);

    ProcessDispatcher(mxtl::RefPtr<JobDispatcher> job, mxtl::StringPiece name, uint32_t flags);

    ProcessDispatcher(const ProcessDispatcher&) = delete;
    ProcessDispatcher& operator=(const ProcessDispatcher&) = delete;

    // Thread lifecycle support
    friend class UserThread;
    status_t AddThread(UserThread* t, bool initial_thread);
    void RemoveThread(UserThread* t);

    void SetStateLocked(State) TA_REQ(state_lock_);

    // Kill all threads
    void KillAllThreads() TA_REQ(state_lock_);

    // Add a process to the global process list.  Allocate a new process ID from
    // the global pool at the same time, and assign it to the process.
    static void AddProcess(ProcessDispatcher* process);

    // Remove a process from the global process list.
    static void RemoveProcess(ProcessDispatcher* process);

    mxtl::DoublyLinkedListNodeState<ProcessDispatcher*> dll_process_;
    mxtl::DoublyLinkedListNodeState<ProcessDispatcher*> dll_job_;

    mx_handle_t handle_rand_ = 0;

    // list of threads in this process
    mxtl::DoublyLinkedList<UserThread*> thread_list_ TA_GUARDED(state_lock_);

    // our address space
    mxtl::RefPtr<VmAspace> aspace_;

    // the enclosing job
    const mxtl::RefPtr<JobDispatcher> job_;

    // our list of handles
    mutable Mutex handle_table_lock_; // protects |handles_|.
    mxtl::DoublyLinkedList<Handle*> handles_ TA_GUARDED(handle_table_lock_);

    StateTracker state_tracker_;

    FutexContext futex_context_;

    // our state
    State state_ TA_GUARDED(state_lock_) = State::INITIAL;
    mutable Mutex state_lock_;

    // process return code
    int retcode_ = 0;

    mxtl::RefPtr<ExceptionPort> exception_port_ TA_GUARDED(exception_lock_);
    mxtl::RefPtr<ExceptionPort> debugger_exception_port_ TA_GUARDED(exception_lock_);
    Mutex exception_lock_;

    uint32_t bad_handle_policy_ = MX_POLICY_BAD_HANDLE_IGNORE;

    // Used to protect name read/writes
    mutable SpinLock name_lock_;

    // The user-friendly process name. For debug purposes only.
    // This includes the trailing NUL.
    char name_[MX_MAX_NAME_LEN] TA_GUARDED(name_lock_) = {};

    using ProcessList = mxtl::DoublyLinkedList<ProcessDispatcher*, ProcessListTraits>;
    static mutex_t global_process_list_mutex_;
    static ProcessList global_process_list_ TA_GUARDED(global_process_list_mutex_);
};

const char* StateToString(ProcessDispatcher::State state);
