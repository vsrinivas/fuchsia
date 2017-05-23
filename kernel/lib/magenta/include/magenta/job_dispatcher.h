// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/mutex.h>

#include <magenta/dispatcher.h>
#include <magenta/policy_manager.h>
#include <magenta/process_dispatcher.h>
#include <magenta/state_tracker.h>
#include <magenta/types.h>

#include <mxtl/array.h>
#include <mxtl/canary.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/name.h>
#include <mxtl/ref_counted.h>

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
    // Traits to belong to the parent's weak job list.
    struct ListTraitsWeak {
        static mxtl::DoublyLinkedListNodeState<JobDispatcher*>& node_state(
            JobDispatcher& obj) {
            return obj.dll_job_weak_;
        }
    };

    // Traits to belong to the parent's job list.
    struct ListTraits {
        static mxtl::SinglyLinkedListNodeState<mxtl::RefPtr<JobDispatcher>>& node_state(
            JobDispatcher& obj) {
            return obj.dll_job_;
        }
    };

    static mxtl::RefPtr<JobDispatcher> CreateRootJob();
    static status_t Create(uint32_t flags,
                           mxtl::RefPtr<JobDispatcher> parent,
                           mxtl::RefPtr<Dispatcher>* dispatcher,
                           mx_rights_t* rights);

    ~JobDispatcher() final;

    // Dispatcher implementation.
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_JOB; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }
    void on_zero_handles() final;
    mx_koid_t get_related_koid() const final;
    mxtl::RefPtr<JobDispatcher> parent() { return mxtl::RefPtr<JobDispatcher>(parent_); }

    // Job methods.
    void get_name(char out_name[MX_MAX_NAME_LEN]) const final;
    status_t set_name(const char* name, size_t len) final;
    uint32_t max_height() const { return max_height_; }
    uint32_t process_count() const TA_REQ(lock_) { return process_count_;}
    uint32_t job_count() const TA_REQ(lock_) { return job_count_; }
    bool AddChildProcess(ProcessDispatcher* process);
    void RemoveChildProcess(ProcessDispatcher* process);
    void Kill();

    // Set policy. |mode| is is either MX_JOB_POL_RELATIVE or MX_JOB_POL_ABSOLUTE and
    // in_policy is an array of |count| elements.
    status_t SetPolicy(uint32_t mode, const mx_policy_basic* in_policy, size_t policy_count);
    pol_cookie_t GetPolicy();

    // Walks the job/process tree and invokes |je| methods on each node. If
    // |recurse| is false, only visits direct children of this job. Returns
    // false if any methods of |je| return false; returns true otherwise.
    bool EnumerateChildren(JobEnumerator* je, bool recurse);

    mxtl::RefPtr<ProcessDispatcher> LookupProcessById(mx_koid_t koid);
    mxtl::RefPtr<JobDispatcher> LookupJobById(mx_koid_t koid);

private:
    enum class State {
        READY,
        KILLING,
    };

    JobDispatcher(uint32_t flags, mxtl::RefPtr<JobDispatcher> parent, pol_cookie_t policy);

    bool AddChildJob(JobDispatcher* job);
    void RemoveChildJob(JobDispatcher* job);

    void UpdateSignalsIncrementLocked() TA_REQ(lock_);
    void UpdateSignalsDecrementLocked() TA_REQ(lock_);

    mxtl::Canary<mxtl::magic("JOBD")> canary_;

    const mxtl::RefPtr<JobDispatcher> parent_;
    const uint32_t max_height_;

    mxtl::DoublyLinkedListNodeState<JobDispatcher*> dll_job_weak_;
    mxtl::SinglyLinkedListNodeState<mxtl::RefPtr<JobDispatcher>> dll_job_;

    // The user-friendly job name. For debug purposes only. That
    // is, there is no mechanism to mint a handle to a job via this name.
    mxtl::Name<MX_MAX_NAME_LEN> name_;

    // The |lock_| protects all members below.
    Mutex lock_;
    State state_ TA_GUARDED(lock_);
    uint32_t process_count_ TA_GUARDED(lock_);
    uint32_t job_count_ TA_GUARDED(lock_);
    StateTracker state_tracker_;

    using WeakJobList =
        mxtl::DoublyLinkedList<JobDispatcher*, ListTraitsWeak>;
    using WeakProcessList =
        mxtl::DoublyLinkedList<ProcessDispatcher*, ProcessDispatcher::JobListTraitsWeak>;

    using ProcessList =
        mxtl::SinglyLinkedList<mxtl::RefPtr<ProcessDispatcher>, ProcessDispatcher::JobListTraits>;
    using JobList =
        mxtl::SinglyLinkedList<mxtl::RefPtr<JobDispatcher>, ListTraits>;

    WeakJobList jobs_ TA_GUARDED(lock_);
    WeakProcessList procs_ TA_GUARDED(lock_);

    pol_cookie_t policy_ TA_GUARDED(lock_);
};
