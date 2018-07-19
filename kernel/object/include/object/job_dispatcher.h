// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <object/dispatcher.h>
#include <object/excp_port.h>
#include <object/policy_manager.h>
#include <object/process_dispatcher.h>

#include <zircon/types.h>
#include <fbl/array.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/name.h>
#include <fbl/ref_counted.h>
#include <kernel/lockdep.h>

class JobNode;

// Interface for walking a job/process tree.
class JobEnumerator {
public:
    // Visits a job. If OnJob returns false, the enumeration stops.
    virtual bool OnJob(JobDispatcher* job) { return true; }

    // Visits a process. If OnProcess returns false, the enumeration stops.
    virtual bool OnProcess(ProcessDispatcher* proc) { return true; }

protected:
    virtual ~JobEnumerator() = default;
};

// This class implements the Job object kernel interface. Each Job has a parent
// Job and zero or more child Jobs and zero or more Child processes. This
// creates a DAG (tree) that connects every living task in the system.
// This is critically important because of the bottoms up refcount nature of
// the system in which the scheduler keeps alive the thread and the thread keeeps
// alive the process, so without the Job it would not be possible to enumerate
// or control the tasks in the system for which there are no outstanding handles.
//
// The second important job of the Job is to apply policies that cannot otherwise
// be easily enforced by capabilities, for example kernel object creation.
//
// The third one is to support exception propagation from the leaf tasks to
// the root tasks.
//
// Obviously there is a special case for the 'root' Job which its parent is null
// and in the current implementation will call platform_halt() when its process
// and job count reaches zero. The root job is not exposed to user mode, instead
// the single child Job of the root job is given to the userboot process.
class JobDispatcher final : public SoloDispatcher<JobDispatcher> {
public:
    // Traits to belong to the parent's raw job list.
    struct ListTraitsRaw {
        static fbl::DoublyLinkedListNodeState<JobDispatcher*>& node_state(
            JobDispatcher& obj) {
            return obj.dll_job_raw_;
        }
    };

    // Traits to belong to the parent's job list.
    struct ListTraits {
        static fbl::SinglyLinkedListNodeState<fbl::RefPtr<JobDispatcher>>& node_state(
            JobDispatcher& obj) {
            return obj.dll_job_;
        }
    };

    static fbl::RefPtr<JobDispatcher> CreateRootJob();
    static zx_status_t Create(uint32_t flags,
                              fbl::RefPtr<JobDispatcher> parent,
                              fbl::RefPtr<Dispatcher>* dispatcher,
                              zx_rights_t* rights);

    ~JobDispatcher() final;

    // Dispatcher implementation.
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_JOB; }
    bool has_state_tracker() const final { return true; }
    zx_koid_t get_related_koid() const final;
    fbl::RefPtr<JobDispatcher> parent() { return fbl::RefPtr<JobDispatcher>(parent_); }

    // Job methods.
    void get_name(char out_name[ZX_MAX_NAME_LEN]) const final;
    zx_status_t set_name(const char* name, size_t len) final;
    uint32_t max_height() const { return max_height_; }

    bool AddChildProcess(const fbl::RefPtr<ProcessDispatcher>& process);
    void RemoveChildProcess(ProcessDispatcher* process);
    void Kill();

    // Set policy. |mode| is is either ZX_JOB_POL_RELATIVE or ZX_JOB_POL_ABSOLUTE and
    // in_policy is an array of |count| elements.
    zx_status_t SetPolicy(uint32_t mode, const zx_policy_basic* in_policy, size_t policy_count);
    pol_cookie_t GetPolicy();

    // Calls the provided |zx_status_t func(JobDispatcher*)| on every
    // JobDispatcher in the system. Stops if |func| returns an error,
    // returning the error value.
    template <typename T>
    static zx_status_t ForEachJob(T func) {
        Guard<fbl::Mutex> guard{AllJobsLock::Get()};
        for (auto &job : all_jobs_list_) {
            zx_status_t s = func(&job);
            if (s != ZX_OK)
                return s;
        }
        return ZX_OK;
    }

    // Walks the job/process tree and invokes |je| methods on each node. If
    // |recurse| is false, only visits direct children of this job. Returns
    // false if any methods of |je| return false; returns true otherwise.
    bool EnumerateChildren(JobEnumerator* je, bool recurse);

    fbl::RefPtr<ProcessDispatcher> LookupProcessById(zx_koid_t koid);
    fbl::RefPtr<JobDispatcher> LookupJobById(zx_koid_t koid);

    // exception handling support
    zx_status_t SetExceptionPort(fbl::RefPtr<ExceptionPort> eport);
    // Returns true if a port had been set.
    bool ResetExceptionPort(bool quietly);
    fbl::RefPtr<ExceptionPort> exception_port();

private:
    enum class State {
        READY,
        KILLING,
        DEAD
    };

    using LiveRefsArray = fbl::Array<fbl::RefPtr<Dispatcher>>;

    JobDispatcher(uint32_t flags, fbl::RefPtr<JobDispatcher> parent, pol_cookie_t policy);

    bool AddChildJob(const fbl::RefPtr<JobDispatcher>& job);
    void RemoveChildJob(JobDispatcher* job);

    void UpdateSignalsIncrementLocked() TA_REQ(get_lock());
    void UpdateSignalsDecrementLocked() TA_REQ(get_lock());

    template <typename T, typename Fn>
     __attribute__((warn_unused_result)) LiveRefsArray ForEachChildInLocked(
        T& children, zx_status_t* status, Fn func) TA_REQ(get_lock());

    template <typename T>
    uint32_t ChildCountLocked() const TA_REQ(get_lock());

    fbl::Canary<fbl::magic("JOBD")> canary_;

    const fbl::RefPtr<JobDispatcher> parent_;
    const uint32_t max_height_;

    fbl::DoublyLinkedListNodeState<JobDispatcher*> dll_job_raw_;
    fbl::SinglyLinkedListNodeState<fbl::RefPtr<JobDispatcher>> dll_job_;

    // The user-friendly job name. For debug purposes only. That
    // is, there is no mechanism to mint a handle to a job via this name.
    fbl::Name<ZX_MAX_NAME_LEN> name_;

    // The common |get_lock()| protects all members below.
    State state_ TA_GUARDED(get_lock());
    uint32_t process_count_ TA_GUARDED(get_lock());
    uint32_t job_count_ TA_GUARDED(get_lock());

    using RawJobList =
        fbl::DoublyLinkedList<JobDispatcher*, ListTraitsRaw>;
    using RawProcessList =
        fbl::DoublyLinkedList<ProcessDispatcher*, ProcessDispatcher::JobListTraitsRaw>;

    using ProcessList =
        fbl::SinglyLinkedList<fbl::RefPtr<ProcessDispatcher>, ProcessDispatcher::JobListTraits>;
    using JobList =
        fbl::SinglyLinkedList<fbl::RefPtr<JobDispatcher>, ListTraits>;

    // Access to the pointers in these lists, especially any promotions to
    // RefPtr, must be handled very carefully, because the children can die
    // even when |lock_| is held. See ForEachChildInLocked() for more details
    // and for a safe way to enumerate them.
    RawJobList jobs_ TA_GUARDED(get_lock());
    RawProcessList procs_ TA_GUARDED(get_lock());

    pol_cookie_t policy_ TA_GUARDED(get_lock());

    fbl::RefPtr<ExceptionPort> exception_port_ TA_GUARDED(get_lock());

    // Global list of JobDispatchers, ordered by hierarchy and
    // creation order. Used to find victims in low-resource
    // situations.
    fbl::DoublyLinkedListNodeState<JobDispatcher*> dll_all_jobs_;
    struct ListTraitsAllJobs {
        static fbl::DoublyLinkedListNodeState<JobDispatcher*>& node_state(
            JobDispatcher& obj) {
            return obj.dll_all_jobs_;
        }
    };
    using AllJobsList =
        fbl::DoublyLinkedList<JobDispatcher*, ListTraitsAllJobs>;

    DECLARE_SINGLETON_MUTEX(AllJobsLock);

    // All jobs in the system.
    static AllJobsList all_jobs_list_ TA_GUARDED(AllJobsLock::Get());
};

// Returns the job that is the ancestor of all other tasks.
fbl::RefPtr<JobDispatcher> GetRootJobDispatcher();
