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
#include <magenta/policy_manager.h>
#include <magenta/state_tracker.h>
#include <magenta/syscalls/object.h>
#include <magenta/types.h>
#include <magenta/user_thread.h>

#include <mxtl/array.h>
#include <mxtl/canary.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/name.h>
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

    // Traits to belong in the parent job's weak list.
    struct JobListTraitsWeak {
        static mxtl::DoublyLinkedListNodeState<ProcessDispatcher*>& node_state(
            ProcessDispatcher& obj) {
            return obj.dll_job_weak_;
        }
    };

    // Traits to belong in the parent job's list.
    struct JobListTraits {
        static mxtl::SinglyLinkedListNodeState<mxtl::RefPtr<ProcessDispatcher>>& node_state(
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
    mx_koid_t get_related_koid() const final;

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

    // Maps a |handle| to an integer which can be given to usermode as a
    // handle value. Uses Handle->base_value() plus additional mixing.
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

    // Get the dispatcher corresponding to this handle value.
    template <typename T>
    mx_status_t GetDispatcher(mx_handle_t handle_value,
                              mxtl::RefPtr<T>* dispatcher) {
        return GetDispatcherAndRights(handle_value, dispatcher, nullptr);
    }

    // Get the dispatcher and the rights corresponding to this handle value.
    template <typename T>
    mx_status_t GetDispatcherAndRights(mx_handle_t handle_value,
                                       mxtl::RefPtr<T>* dispatcher,
                                       mx_rights_t* out_rights) {
        mxtl::RefPtr<Dispatcher> generic_dispatcher;
        auto status = GetDispatcherInternal(handle_value, &generic_dispatcher, out_rights);
        if (status != NO_ERROR)
            return status;
        *dispatcher = DownCastDispatcher<T>(&generic_dispatcher);
        if (!*dispatcher)
            return ERR_WRONG_TYPE;
        return NO_ERROR;
    }

    // Get the dispatcher corresponding to this handle value, after
    // checking that this handle has the desired rights.
    // Returns the rights the handle currently has.
    template <typename T>
    mx_status_t GetDispatcherWithRights(mx_handle_t handle_value,
                                        mx_rights_t desired_rights,
                                        mxtl::RefPtr<T>* dispatcher,
                                        mx_rights_t* out_rights) {
        mxtl::RefPtr<Dispatcher> generic_dispatcher;
        auto status = GetDispatcherWithRightsInternal(handle_value,
                                                      desired_rights,
                                                      &generic_dispatcher,
                                                      out_rights);
        if (status != NO_ERROR)
            return status;
        *dispatcher = DownCastDispatcher<T>(&generic_dispatcher);
        if (!*dispatcher)
            return ERR_WRONG_TYPE;
        return NO_ERROR;
    }

    // Get the dispatcher corresponding to this handle value, after
    // checking that this handle has the desired rights.
    template <typename T>
    mx_status_t GetDispatcherWithRights(mx_handle_t handle_value,
                                        mx_rights_t desired_rights,
                                        mxtl::RefPtr<T>* dispatcher) {
        return GetDispatcherWithRights(handle_value, desired_rights, dispatcher, nullptr);
    }

    mx_koid_t GetKoidForHandle(mx_handle_t handle_value);

    bool IsHandleValid(mx_handle_t handle_value);

    // accessors
    Mutex* handle_table_lock() TA_RET_CAP(handle_table_lock_) { return &handle_table_lock_; }
    FutexContext* futex_context() { return &futex_context_; }
    State state() const;
    mxtl::RefPtr<VmAspace> aspace() { return aspace_; }
    mxtl::RefPtr<JobDispatcher> job();

    void get_name(char out_name[MX_MAX_NAME_LEN]) const final;
    status_t set_name(const char* name, size_t len) final;

    void Exit(int retcode) __NO_RETURN;
    void Kill();

    // Syscall helpers
    status_t GetInfo(mx_info_process_t* info);
    status_t GetStats(mx_info_task_stats_t* stats);
    // NOTE: Code outside of the syscall layer should not typically know about
    // user_ptrs; do not use this pattern as an example.
    status_t GetAspaceMaps(user_ptr<mx_info_maps_t> maps, size_t max,
                           size_t* actual, size_t* available);

    status_t CreateUserThread(mxtl::StringPiece name, uint32_t flags,
                              mxtl::RefPtr<Dispatcher>* out_dispatcher,
                              mx_rights_t* out_rights);

    status_t GetThreads(mxtl::Array<mx_koid_t>* threads);

    // exception handling support
    status_t SetExceptionPort(mxtl::RefPtr<ExceptionPort> eport);
    // Returns true if a port had been set.
    bool ResetExceptionPort(bool debugger, bool quietly);
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

    uintptr_t get_debug_addr() const;
    mx_status_t set_debug_addr(uintptr_t addr);

    // Checks the |condition| against the parent job's policy.
    //
    // Must be called by syscalls before performing an action represented by an
    // MX_POL_xxxxx condition. If the return value is NO_ERROR the action can
    // proceed; otherwise, the process is not allowed to perform the action,
    // and the status value should be returned to the usermode caller.
    //
    // E.g., in sys_channel_create:
    //
    //     auto up = ProcessDispatcher::GetCurrent();
    //     mx_status_t res = up->QueryPolicy(MX_POL_NEW_CHANNEL);
    //     if (res != NO_ERROR) {
    //         // Channel creation denied by the calling process's
    //         // parent job's policy.
    //         return res;
    //     }
    //     // Ok to create a channel.
    mx_status_t QueryPolicy(uint32_t condition) const;

    // return a cached copy of the vdso code address or compute a new one
    uintptr_t vdso_code_address() {
        if (unlikely(vdso_code_address_ == 0)) {
            return cache_vdso_code_address();
        }
        return vdso_code_address_;
    }

private:
    // compute the vdso code address and store in vdso_code_address_
    uintptr_t cache_vdso_code_address();

    // The diagnostic code is allow to know about the internals of this code.
    friend void DumpProcessList();
    friend uint32_t BuildHandleStats(const ProcessDispatcher&, uint32_t*, size_t);
    friend void DumpProcessHandles(mx_koid_t id);
    friend void DumpProcessVmObjects(mx_koid_t id);
    friend void KillProcess(mx_koid_t id);
    friend void DumpProcessMemoryUsage(const char* prefix, size_t min_pages);

    ProcessDispatcher(mxtl::RefPtr<JobDispatcher> job, mxtl::StringPiece name, uint32_t flags);

    ProcessDispatcher(const ProcessDispatcher&) = delete;
    ProcessDispatcher& operator=(const ProcessDispatcher&) = delete;


    mx_status_t GetDispatcherInternal(mx_handle_t handle_value, mxtl::RefPtr<Dispatcher>* dispatcher,
                                      mx_rights_t* rights);

    mx_status_t GetDispatcherWithRightsInternal(mx_handle_t handle_value, mx_rights_t desired_rights,
                                                mxtl::RefPtr<Dispatcher>* dispatcher_out,
                                                mx_rights_t* out_rights);

    // Thread lifecycle support
    friend class UserThread;
    status_t AddThread(UserThread* t, bool initial_thread);
    void RemoveThread(UserThread* t);

    void SetStateLocked(State) TA_REQ(state_lock_);

    // Kill all threads
    void KillAllThreadsLocked() TA_REQ(state_lock_);

    mxtl::Canary<mxtl::magic("PROC")> canary_;

    // the enclosing job
    const mxtl::RefPtr<JobDispatcher> job_;

    // Policy set by the Job during Create().
    const pol_cookie_t policy_;

    // The process can belong to either of these lists independently.
    mxtl::DoublyLinkedListNodeState<ProcessDispatcher*> dll_job_weak_;
    mxtl::SinglyLinkedListNodeState<mxtl::RefPtr<ProcessDispatcher>> dll_job_;

    mx_handle_t handle_rand_ = 0;

    // list of threads in this process
    mxtl::DoublyLinkedList<UserThread*> thread_list_ TA_GUARDED(state_lock_);

    // our address space
    mxtl::RefPtr<VmAspace> aspace_;

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

    // Exception ports bound to the process.
    mxtl::RefPtr<ExceptionPort> exception_port_ TA_GUARDED(exception_lock_);
    mxtl::RefPtr<ExceptionPort> debugger_exception_port_ TA_GUARDED(exception_lock_);
    Mutex exception_lock_;

    // This is the value of _dl_debug_addr from ld.so.
    // See third_party/ulib/musl/ldso/dynlink.c.
    uintptr_t debug_addr_ TA_GUARDED(state_lock_) = 0;

    // This is a cache of aspace()->vdso_code_address().
    uintptr_t vdso_code_address_ = 0;

    // The user-friendly process name. For debug purposes only. That
    // is, there is no mechanism to mint a handle to a process via this name.
    mxtl::Name<MX_MAX_NAME_LEN> name_;
};

const char* StateToString(ProcessDispatcher::State state);
