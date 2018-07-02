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
#include <object/handle.h>
#include <object/policy_manager.h>
#include <object/thread_dispatcher.h>

#include <zircon/syscalls/object.h>
#include <zircon/types.h>
#include <fbl/array.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/name.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_piece.h>

class JobDispatcher;

class ProcessDispatcher final : public SoloDispatcher {
public:
    static zx_status_t Create(
        fbl::RefPtr<JobDispatcher> job, fbl::StringPiece name, uint32_t flags,
        fbl::RefPtr<Dispatcher>* dispatcher, zx_rights_t* rights,
        fbl::RefPtr<VmAddressRegionDispatcher>* root_vmar_disp,
        zx_rights_t* root_vmar_rights);

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
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_PROCESS; }
    bool has_state_tracker() const final { return true; }
    void on_zero_handles() final;
    zx_koid_t get_related_koid() const final;

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
    zx_status_t Initialize();

    // Maps a |handle| to an integer which can be given to usermode as a
    // handle value. Uses Handle->base_value() plus additional mixing.
    zx_handle_t MapHandleToValue(const Handle* handle) const;
    zx_handle_t MapHandleToValue(const HandleOwner& handle) const;

    // Maps a handle value into a Handle as long we can verify that
    // it belongs to this process. Use |skip_policy = true| for testing that
    // a handle is valid without potentially triggering a job policy exception.
    Handle* GetHandleLocked(
        zx_handle_t handle_value, bool skip_policy = false) TA_REQ(handle_table_lock_);

    // Adds |handle| to this process handle list. The handle->process_id() is
    // set to this process id().
    void AddHandle(HandleOwner handle);
    void AddHandleLocked(HandleOwner handle) TA_REQ(handle_table_lock_);

    // Removes the Handle corresponding to |handle_value| from this process
    // handle list.
    HandleOwner RemoveHandle(zx_handle_t handle_value);
    HandleOwner RemoveHandleLocked(zx_handle_t handle_value) TA_REQ(handle_table_lock_);

    // Remove all of an array of |user_handles| from the
    // process. Returns ZX_OK if all of the handles were removed, and
    // returns ZX_ERR_BAD_HANDLE if any were not.
    zx_status_t RemoveHandles(user_in_ptr<const zx_handle_t> user_handles,
                              size_t num_handles);

    // Puts back the |handle_value| which has not yet been given to another process
    // back into this process.
    void UndoRemoveHandleLocked(zx_handle_t handle_value) TA_REQ(handle_table_lock_);


    // Get the dispatcher corresponding to this handle value.
    template <typename T>
    zx_status_t GetDispatcher(zx_handle_t handle_value,
                              fbl::RefPtr<T>* dispatcher) {
        return GetDispatcherAndRights(handle_value, dispatcher, nullptr);
    }

    // Get the dispatcher and the rights corresponding to this handle value.
    template <typename T>
    zx_status_t GetDispatcherAndRights(zx_handle_t handle_value,
                                       fbl::RefPtr<T>* dispatcher,
                                       zx_rights_t* out_rights) {
        fbl::RefPtr<Dispatcher> generic_dispatcher;
        auto status = GetDispatcherInternal(handle_value, &generic_dispatcher, out_rights);
        if (status != ZX_OK)
            return status;
        *dispatcher = DownCastDispatcher<T>(&generic_dispatcher);
        if (!*dispatcher)
            return ZX_ERR_WRONG_TYPE;
        return ZX_OK;
    }

    // Get the dispatcher corresponding to this handle value, after
    // checking that this handle has the desired rights.
    // Returns the rights the handle currently has.
    template <typename T>
    zx_status_t GetDispatcherWithRights(zx_handle_t handle_value,
                                        zx_rights_t desired_rights,
                                        fbl::RefPtr<T>* dispatcher,
                                        zx_rights_t* out_rights) {
        fbl::RefPtr<Dispatcher> generic_dispatcher;
        auto status = GetDispatcherWithRightsInternal(handle_value,
                                                      desired_rights,
                                                      &generic_dispatcher,
                                                      out_rights);
        if (status != ZX_OK)
            return status;
        *dispatcher = DownCastDispatcher<T>(&generic_dispatcher);
        if (!*dispatcher)
            return ZX_ERR_WRONG_TYPE;
        return ZX_OK;
    }

    // Get the dispatcher corresponding to this handle value, after
    // checking that this handle has the desired rights.
    template <typename T>
    zx_status_t GetDispatcherWithRights(zx_handle_t handle_value,
                                        zx_rights_t desired_rights,
                                        fbl::RefPtr<T>* dispatcher) {
        return GetDispatcherWithRights(handle_value, desired_rights, dispatcher, nullptr);
    }

    zx_koid_t GetKoidForHandle(zx_handle_t handle_value);

    bool IsHandleValid(zx_handle_t handle_value);
    bool IsHandleValidNoPolicyCheck(zx_handle_t handle_value);

    // Calls the provided
    // |zx_status_t func(zx_handle_t, zx_rights_t, fbl::RefPtr<Dispatcher>)|
    // on every handle owned by the process. Stops if |func| returns an error,
    // returning the error value.
    template <typename T>
    zx_status_t ForEachHandle(T func) const {
        fbl::AutoLock lock(&handle_table_lock_);
        for (const auto& handle : handles_) {
            const Dispatcher* dispatcher = handle.dispatcher().get();
            zx_status_t s = func(MapHandleToValue(&handle), handle.rights(),
                                 dispatcher);
            if (s != ZX_OK) {
                return s;
            }
        }
        return ZX_OK;
    }

    // accessors
    fbl::Mutex* handle_table_lock() TA_RET_CAP(handle_table_lock_) { return &handle_table_lock_; }
    FutexContext* futex_context() { return &futex_context_; }
    State state() const;
    fbl::RefPtr<VmAspace> aspace() { return aspace_; }
    fbl::RefPtr<JobDispatcher> job();

    void get_name(char out_name[ZX_MAX_NAME_LEN]) const final;
    zx_status_t set_name(const char* name, size_t len) final;

    void Exit(int64_t retcode) __NO_RETURN;
    void Kill();

    // Syscall helpers
    zx_status_t GetInfo(zx_info_process_t* info);
    zx_status_t GetStats(zx_info_task_stats_t* stats);
    // NOTE: Code outside of the syscall layer should not typically know about
    // user_ptrs; do not use this pattern as an example.
    zx_status_t GetAspaceMaps(user_out_ptr<zx_info_maps_t> maps, size_t max,
                              size_t* actual, size_t* available);
    zx_status_t GetVmos(user_out_ptr<zx_info_vmo_t> vmos, size_t max,
                        size_t* actual, size_t* available);

    zx_status_t GetThreads(fbl::Array<zx_koid_t>* threads);

    // exception handling support
    zx_status_t SetExceptionPort(fbl::RefPtr<ExceptionPort> eport);
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
    static fbl::RefPtr<ProcessDispatcher> LookupProcessById(zx_koid_t koid);

    // Look up a thread in this process given its koid.
    // Returns nullptr if not found.
    fbl::RefPtr<ThreadDispatcher> LookupThreadById(zx_koid_t koid);

    uintptr_t get_debug_addr() const;
    zx_status_t set_debug_addr(uintptr_t addr);

    // Checks the |condition| against the parent job's policy.
    //
    // Must be called by syscalls before performing an action represented by an
    // ZX_POL_xxxxx condition. If the return value is ZX_OK the action can
    // proceed; otherwise, the process is not allowed to perform the action,
    // and the status value should be returned to the usermode caller.
    //
    // E.g., in sys_channel_create:
    //
    //     auto up = ProcessDispatcher::GetCurrent();
    //     zx_status_t res = up->QueryPolicy(ZX_POL_NEW_CHANNEL);
    //     if (res != ZX_OK) {
    //         // Channel creation denied by the calling process's
    //         // parent job's policy.
    //         return res;
    //     }
    //     // Ok to create a channel.
    zx_status_t QueryPolicy(uint32_t condition) const;

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
    friend void KillProcess(zx_koid_t id);
    friend void DumpProcessMemoryUsage(const char* prefix, size_t min_pages);

    ProcessDispatcher(fbl::RefPtr<JobDispatcher> job, fbl::StringPiece name, uint32_t flags);

    ProcessDispatcher(const ProcessDispatcher&) = delete;
    ProcessDispatcher& operator=(const ProcessDispatcher&) = delete;


    zx_status_t GetDispatcherInternal(zx_handle_t handle_value, fbl::RefPtr<Dispatcher>* dispatcher,
                                      zx_rights_t* rights);

    zx_status_t GetDispatcherWithRightsInternal(zx_handle_t handle_value, zx_rights_t desired_rights,
                                                fbl::RefPtr<Dispatcher>* dispatcher_out,
                                                zx_rights_t* out_rights);

    // Thread lifecycle support
    friend class ThreadDispatcher;
    zx_status_t AddThread(ThreadDispatcher* t, bool initial_thread);
    void RemoveThread(ThreadDispatcher* t);

    void SetStateLocked(State) TA_REQ(get_lock());
    void FinishDeadTransition();

    // Kill all threads
    void KillAllThreadsLocked() TA_REQ(get_lock());

    // TODO(dbort): Add "canary_.Assert()" calls to methods.
    fbl::Canary<fbl::magic("PROC")> canary_;

    // the enclosing job
    const fbl::RefPtr<JobDispatcher> job_;

    // Policy set by the Job during Create().
    const pol_cookie_t policy_;

    // The process can belong to either of these lists independently.
    fbl::DoublyLinkedListNodeState<ProcessDispatcher*> dll_job_raw_;
    fbl::SinglyLinkedListNodeState<fbl::RefPtr<ProcessDispatcher>> dll_job_;

    uint32_t handle_rand_ = 0;

    // list of threads in this process
    using ThreadList = fbl::DoublyLinkedList<ThreadDispatcher*, ThreadDispatcher::ThreadListTraits>;
    ThreadList thread_list_ TA_GUARDED(get_lock());

    // our address space
    fbl::RefPtr<VmAspace> aspace_;

    // our list of handles
    mutable fbl::Mutex handle_table_lock_; // protects |handles_|.
    fbl::DoublyLinkedList<Handle*> handles_ TA_GUARDED(handle_table_lock_);

    FutexContext futex_context_;

    // our state
    State state_ TA_GUARDED(get_lock()) = State::INITIAL;

    // True if FinishDeadTransition has been called.
    // This is used as a sanity check only.
    bool completely_dead_ = false;

    // process return code
    int64_t retcode_ = 0;

    // Exception ports bound to the process.
    fbl::RefPtr<ExceptionPort> exception_port_ TA_GUARDED(get_lock());
    fbl::RefPtr<ExceptionPort> debugger_exception_port_ TA_GUARDED(get_lock());

    // This is the value of _dl_debug_addr from ld.so.
    // See third_party/ulib/musl/ldso/dynlink.c.
    uintptr_t debug_addr_ TA_GUARDED(get_lock()) = 0;

    // This is a cache of aspace()->vdso_code_address().
    uintptr_t vdso_code_address_ = 0;

    // The user-friendly process name. For debug purposes only. That
    // is, there is no mechanism to mint a handle to a process via this name.
    fbl::Name<ZX_MAX_NAME_LEN> name_;
};

const char* StateToString(ProcessDispatcher::State state);
