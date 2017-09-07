// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/event.h>
#include <kernel/thread.h>
#include <vm/vm_aspace.h>
#include <object/dispatcher.h>
#include <object/futex_context.h>
#include <object/handle_owner.h>
#include <object/policy_manager.h>
#include <object/state_tracker.h>
#include <object/thread_dispatcher.h>

#include <magenta/syscalls/object.h>
#include <magenta/types.h>
#include <fbl/array.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/name.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_piece.h>

class JobDispatcher;

class ProcessDispatcher final : public Dispatcher {
public:
    static mx_status_t Create(
        fbl::RefPtr<JobDispatcher> job, fbl::StringPiece name, uint32_t flags,
        fbl::RefPtr<Dispatcher>* dispatcher, mx_rights_t* rights,
        fbl::RefPtr<VmAddressRegionDispatcher>* root_vmar_disp,
        mx_rights_t* root_vmar_rights);

    // Traits to belong in the parent job's raw list.
    struct JobListTraitsRaw {
        static fbl::DoublyLinkedListNodeState<ProcessDispatcher*>& node_state(
            ProcessDispatcher& obj) {
            return obj.dll_job_raw_;
        }
    };

    // Traits to belong in the parent job's list.
    struct JobListTraits {
        static fbl::SinglyLinkedListNodeState<fbl::RefPtr<ProcessDispatcher>>& node_state(
            ProcessDispatcher& obj) {
            return obj.dll_job_;
        }
    };

    static ProcessDispatcher* GetCurrent() {
        ThreadDispatcher* current = ThreadDispatcher::GetCurrent();
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
    mx_status_t Initialize();

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
                              fbl::RefPtr<T>* dispatcher) {
        return GetDispatcherAndRights(handle_value, dispatcher, nullptr);
    }

    // Get the dispatcher and the rights corresponding to this handle value.
    template <typename T>
    mx_status_t GetDispatcherAndRights(mx_handle_t handle_value,
                                       fbl::RefPtr<T>* dispatcher,
                                       mx_rights_t* out_rights) {
        fbl::RefPtr<Dispatcher> generic_dispatcher;
        auto status = GetDispatcherInternal(handle_value, &generic_dispatcher, out_rights);
        if (status != MX_OK)
            return status;
        *dispatcher = DownCastDispatcher<T>(&generic_dispatcher);
        if (!*dispatcher)
            return MX_ERR_WRONG_TYPE;
        return MX_OK;
    }

    // Get the dispatcher corresponding to this handle value, after
    // checking that this handle has the desired rights.
    // Returns the rights the handle currently has.
    template <typename T>
    mx_status_t GetDispatcherWithRights(mx_handle_t handle_value,
                                        mx_rights_t desired_rights,
                                        fbl::RefPtr<T>* dispatcher,
                                        mx_rights_t* out_rights) {
        fbl::RefPtr<Dispatcher> generic_dispatcher;
        auto status = GetDispatcherWithRightsInternal(handle_value,
                                                      desired_rights,
                                                      &generic_dispatcher,
                                                      out_rights);
        if (status != MX_OK)
            return status;
        *dispatcher = DownCastDispatcher<T>(&generic_dispatcher);
        if (!*dispatcher)
            return MX_ERR_WRONG_TYPE;
        return MX_OK;
    }

    // Get the dispatcher corresponding to this handle value, after
    // checking that this handle has the desired rights.
    template <typename T>
    mx_status_t GetDispatcherWithRights(mx_handle_t handle_value,
                                        mx_rights_t desired_rights,
                                        fbl::RefPtr<T>* dispatcher) {
        return GetDispatcherWithRights(handle_value, desired_rights, dispatcher, nullptr);
    }

    mx_koid_t GetKoidForHandle(mx_handle_t handle_value);

    bool IsHandleValid(mx_handle_t handle_value);

    // Calls the provided
    // |mx_status_t func(mx_handle_t, mx_rights_t, fbl::RefPtr<Dispatcher>)|
    // on every handle owned by the process. Stops if |func| returns an error,
    // returning the error value.
    template <typename T>
    mx_status_t ForEachHandle(T func) const {
        fbl::AutoLock lock(&handle_table_lock_);
        for (const auto& handle : handles_) {
            // It would be nice to only pass a const Dispatcher* to the
            // callback, but many callers will use DownCastDispatcher()
            // which requires a (necessarily non-const) RefPtr<Dispatcher>.
            mx_status_t s = func(MapHandleToValue(&handle), handle.rights(),
                                 fbl::move(handle.dispatcher()));
            if (s != MX_OK) {
                return s;
            }
        }
        return MX_OK;
    }

    // accessors
    fbl::Mutex* handle_table_lock() TA_RET_CAP(handle_table_lock_) { return &handle_table_lock_; }
    FutexContext* futex_context() { return &futex_context_; }
    State state() const;
    fbl::RefPtr<VmAspace> aspace() { return aspace_; }
    fbl::RefPtr<JobDispatcher> job();

    void get_name(char out_name[MX_MAX_NAME_LEN]) const final;
    mx_status_t set_name(const char* name, size_t len) final;

    void Exit(int retcode) __NO_RETURN;
    void Kill();

    // Syscall helpers
    mx_status_t GetInfo(mx_info_process_t* info);
    mx_status_t GetStats(mx_info_task_stats_t* stats);
    // NOTE: Code outside of the syscall layer should not typically know about
    // user_ptrs; do not use this pattern as an example.
    mx_status_t GetAspaceMaps(user_ptr<mx_info_maps_t> maps, size_t max,
                           size_t* actual, size_t* available);
    mx_status_t GetVmos(user_ptr<mx_info_vmo_t> vmos, size_t max,
                     size_t* actual, size_t* available);

    mx_status_t GetThreads(fbl::Array<mx_koid_t>* threads);

    // exception handling support
    mx_status_t SetExceptionPort(fbl::RefPtr<ExceptionPort> eport);
    // Returns true if a port had been set.
    bool ResetExceptionPort(bool debugger, bool quietly);
    fbl::RefPtr<ExceptionPort> exception_port();
    fbl::RefPtr<ExceptionPort> debugger_exception_port();
    // |eport| can either be the process's eport or that of any parent job.
    void OnExceptionPortRemoval(const fbl::RefPtr<ExceptionPort>& eport);

    // The following two methods can be slow and inaccurate and should only be
    // called from diagnostics code.
    uint32_t ThreadCount() const;
    size_t PageCount() const;

    // Look up a process given its koid.
    // Returns nullptr if not found.
    static fbl::RefPtr<ProcessDispatcher> LookupProcessById(mx_koid_t koid);

    // Look up a thread in this process given its koid.
    // Returns nullptr if not found.
    fbl::RefPtr<ThreadDispatcher> LookupThreadById(mx_koid_t koid);

    uintptr_t get_debug_addr() const;
    mx_status_t set_debug_addr(uintptr_t addr);

    // Checks the |condition| against the parent job's policy.
    //
    // Must be called by syscalls before performing an action represented by an
    // MX_POL_xxxxx condition. If the return value is MX_OK the action can
    // proceed; otherwise, the process is not allowed to perform the action,
    // and the status value should be returned to the usermode caller.
    //
    // E.g., in sys_channel_create:
    //
    //     auto up = ProcessDispatcher::GetCurrent();
    //     mx_status_t res = up->QueryPolicy(MX_POL_NEW_CHANNEL);
    //     if (res != MX_OK) {
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
    friend void KillProcess(mx_koid_t id);
    friend void DumpProcessMemoryUsage(const char* prefix, size_t min_pages);

    ProcessDispatcher(fbl::RefPtr<JobDispatcher> job, fbl::StringPiece name, uint32_t flags);

    ProcessDispatcher(const ProcessDispatcher&) = delete;
    ProcessDispatcher& operator=(const ProcessDispatcher&) = delete;


    mx_status_t GetDispatcherInternal(mx_handle_t handle_value, fbl::RefPtr<Dispatcher>* dispatcher,
                                      mx_rights_t* rights);

    mx_status_t GetDispatcherWithRightsInternal(mx_handle_t handle_value, mx_rights_t desired_rights,
                                                fbl::RefPtr<Dispatcher>* dispatcher_out,
                                                mx_rights_t* out_rights);

    // Thread lifecycle support
    friend class ThreadDispatcher;
    mx_status_t AddThread(ThreadDispatcher* t, bool initial_thread);
    void RemoveThread(ThreadDispatcher* t);

    void SetStateLocked(State) TA_REQ(state_lock_);

    // Kill all threads
    void KillAllThreadsLocked() TA_REQ(state_lock_);

    fbl::Canary<fbl::magic("PROC")> canary_;

    // the enclosing job
    const fbl::RefPtr<JobDispatcher> job_;

    // Policy set by the Job during Create().
    const pol_cookie_t policy_;

    // The process can belong to either of these lists independently.
    fbl::DoublyLinkedListNodeState<ProcessDispatcher*> dll_job_raw_;
    fbl::SinglyLinkedListNodeState<fbl::RefPtr<ProcessDispatcher>> dll_job_;

    mx_handle_t handle_rand_ = 0;

    // list of threads in this process
    using ThreadList = fbl::DoublyLinkedList<ThreadDispatcher*, ThreadDispatcher::ThreadListTraits>;
    ThreadList thread_list_ TA_GUARDED(state_lock_);

    // our address space
    fbl::RefPtr<VmAspace> aspace_;

    // our list of handles
    mutable fbl::Mutex handle_table_lock_; // protects |handles_|.
    fbl::DoublyLinkedList<Handle*> handles_ TA_GUARDED(handle_table_lock_);

    StateTracker state_tracker_;

    FutexContext futex_context_;

    // our state
    State state_ TA_GUARDED(state_lock_) = State::INITIAL;
    mutable fbl::Mutex state_lock_;

    // process return code
    int retcode_ = 0;

    // Exception ports bound to the process.
    fbl::RefPtr<ExceptionPort> exception_port_ TA_GUARDED(exception_lock_);
    fbl::RefPtr<ExceptionPort> debugger_exception_port_ TA_GUARDED(exception_lock_);
    fbl::Mutex exception_lock_;

    // This is the value of _dl_debug_addr from ld.so.
    // See third_party/ulib/musl/ldso/dynlink.c.
    uintptr_t debug_addr_ TA_GUARDED(state_lock_) = 0;

    // This is a cache of aspace()->vdso_code_address().
    uintptr_t vdso_code_address_ = 0;

    // The user-friendly process name. For debug purposes only. That
    // is, there is no mechanism to mint a handle to a process via this name.
    fbl::Name<MX_MAX_NAME_LEN> name_;
};

const char* StateToString(ProcessDispatcher::State state);
