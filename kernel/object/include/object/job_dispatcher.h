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
#include <object/state_tracker.h>

#include <magenta/types.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/name.h>
#include <fbl/ref_counted.h>

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

class JobDispatcher final : public Dispatcher {
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
    static mx_status_t Create(uint32_t flags,
                              fbl::RefPtr<JobDispatcher> parent,
                              fbl::RefPtr<Dispatcher>* dispatcher,
                              mx_rights_t* rights);

    ~JobDispatcher() final;

    // Dispatcher implementation.
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_JOB; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }
    void on_zero_handles() final;
    mx_koid_t get_related_koid() const final;
    fbl::RefPtr<JobDispatcher> parent() { return fbl::RefPtr<JobDispatcher>(parent_); }

    // Job methods.
    void get_name(char out_name[MX_MAX_NAME_LEN]) const final;
    mx_status_t set_name(const char* name, size_t len) final;
    uint32_t max_height() const { return max_height_; }

    // "Importance" is a userspace-settable hint that is used to rank jobs for
    // OOM killing. See MX_PROP_JOB_IMPORTANCE.
    // Note: if the importance is set to MX_JOB_IMPORTANCE_INHERITED (which is
    // the default for all jobs except the root job), get_importance() will
    // return the inherited value.
    mx_status_t get_importance(mx_job_importance_t* out) const;
    mx_status_t set_importance(mx_job_importance_t importance);

    // TODO(dbort): Consider adding a get_capped_importance() so that userspace
    // doesn't need to check all ancestor jobs to find the value (which is the
    // minimum importance value of this job and its ancestors). Could also be
    // used by the killer thread to avoid jobs whose capped importance is
    // IMMORTAL.

    bool AddChildProcess(ProcessDispatcher* process);
    void RemoveChildProcess(ProcessDispatcher* process);
    void Kill();

    // Set policy. |mode| is is either MX_JOB_POL_RELATIVE or MX_JOB_POL_ABSOLUTE and
    // in_policy is an array of |count| elements.
    mx_status_t SetPolicy(uint32_t mode, const mx_policy_basic* in_policy, size_t policy_count);
    pol_cookie_t GetPolicy();

    // Updates a partial ordering between jobs so that this job will be killed
    // after |other| in low-resource situations. If |other| is null, then this
    // job becomes the least-important job in the system.
    mx_status_t MakeMoreImportantThan(fbl::RefPtr<JobDispatcher> other);

    // Calls the provided |mx_status_t func(JobDispatcher*)| on every
    // JobDispatcher in the system, from least important to most important,
    // using the order determined by MakeMoreImportantThan(). Stops if |func|
    // returns an error, returning the error value.
    template <typename T>
    static mx_status_t ForEachJobByImportance(T func) {
        fbl::AutoLock lock(&importance_lock_);
        for (auto &job : importance_list_) {
            mx_status_t s = func(&job);
            if (s != MX_OK)
                return s;
        }
        return MX_OK;
    }

    // Walks the job/process tree and invokes |je| methods on each node. If
    // |recurse| is false, only visits direct children of this job. Returns
    // false if any methods of |je| return false; returns true otherwise.
    bool EnumerateChildren(JobEnumerator* je, bool recurse);

    fbl::RefPtr<ProcessDispatcher> LookupProcessById(mx_koid_t koid);
    fbl::RefPtr<JobDispatcher> LookupJobById(mx_koid_t koid);

    // exception handling support
    mx_status_t SetExceptionPort(fbl::RefPtr<ExceptionPort> eport);
    // Returns true if a port had been set.
    bool ResetExceptionPort(bool quietly);
    fbl::RefPtr<ExceptionPort> exception_port();

private:
    enum class State {
        READY,
        KILLING,
    };

    JobDispatcher(uint32_t flags, fbl::RefPtr<JobDispatcher> parent, pol_cookie_t policy);

    // Like get_importance(), but does not resolve inheritance; i.e., this
    // method may return MX_JOB_IMPORTANCE_INHERITED.
    mx_job_importance_t GetRawImportance() const;

    bool AddChildJob(JobDispatcher* job);
    void RemoveChildJob(JobDispatcher* job);

    void UpdateSignalsIncrementLocked() TA_REQ(lock_);
    void UpdateSignalsDecrementLocked() TA_REQ(lock_);

    fbl::Canary<fbl::magic("JOBD")> canary_;

    const fbl::RefPtr<JobDispatcher> parent_;
    const uint32_t max_height_;

    fbl::DoublyLinkedListNodeState<JobDispatcher*> dll_job_raw_;
    fbl::SinglyLinkedListNodeState<fbl::RefPtr<JobDispatcher>> dll_job_;

    // The user-friendly job name. For debug purposes only. That
    // is, there is no mechanism to mint a handle to a job via this name.
    fbl::Name<MX_MAX_NAME_LEN> name_;

    // The |lock_| protects all members below.
    mutable fbl::Mutex lock_;
    State state_ TA_GUARDED(lock_);
    uint32_t process_count_ TA_GUARDED(lock_);
    uint32_t job_count_ TA_GUARDED(lock_);
    mx_job_importance_t importance_ TA_GUARDED(lock_);
    StateTracker state_tracker_;

    using RawJobList =
        fbl::DoublyLinkedList<JobDispatcher*, ListTraitsRaw>;
    using RawProcessList =
        fbl::DoublyLinkedList<ProcessDispatcher*, ProcessDispatcher::JobListTraitsRaw>;

    using ProcessList =
        fbl::SinglyLinkedList<fbl::RefPtr<ProcessDispatcher>, ProcessDispatcher::JobListTraits>;
    using JobList =
        fbl::SinglyLinkedList<fbl::RefPtr<JobDispatcher>, ListTraits>;

    RawJobList jobs_ TA_GUARDED(lock_);
    RawProcessList procs_ TA_GUARDED(lock_);

    pol_cookie_t policy_ TA_GUARDED(lock_);

    fbl::RefPtr<ExceptionPort> exception_port_ TA_GUARDED(lock_);

    // Global list of JobDispatchers, ordered by relative importance. Used to
    // find victims in low-resource situations.
    fbl::DoublyLinkedListNodeState<JobDispatcher*> dll_importance_;
    struct ListTraitsImportance {
        static fbl::DoublyLinkedListNodeState<JobDispatcher*>& node_state(
            JobDispatcher& obj) {
            return obj.dll_importance_;
        }
    };
    using JobImportanceList =
        fbl::DoublyLinkedList<JobDispatcher*, ListTraitsImportance>;

    static fbl::Mutex importance_lock_;
    // Jobs, ordered by importance, with the least-important job at the front.
    static JobImportanceList importance_list_ TA_GUARDED(importance_lock_);
};

// Returns the job that is the ancestor of all other tasks.
fbl::RefPtr<JobDispatcher> GetRootJobDispatcher();
