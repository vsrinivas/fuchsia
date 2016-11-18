// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/mutex.h>

#include <magenta/dispatcher.h>
#include <magenta/process_dispatcher.h>
#include <magenta/state_tracker.h>
#include <magenta/types.h>

#include <mxtl/array.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/ref_counted.h>

class JobNode;

class JobEnumerator {
public:
    virtual bool Size(uint32_t proc_count, uint32_t job_count) = 0;
    virtual bool OnJob(JobDispatcher* job, uint32_t index) = 0;
    virtual bool OnProcess(ProcessDispatcher* proc, uint32_t index) = 0;
};

class JobDispatcher final : public Dispatcher {
public:
    // Traits to belong to the parent's job list.
    struct ListTraits {
        static mxtl::DoublyLinkedListNodeState<JobDispatcher*>& node_state(
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
    mx_koid_t get_inner_koid() const final;
    mxtl::RefPtr<JobDispatcher> parent() { return mxtl::RefPtr<JobDispatcher>(parent_); }

    // Job methods.
    uint32_t process_count() const { return process_count_;}
    uint32_t job_count() const { return job_count_; }
    void AddChildProcess(ProcessDispatcher* process);
    void RemoveChildProcess(ProcessDispatcher* process);
    bool EnumerateChildren(JobEnumerator* je);
    void Kill();

private:
    JobDispatcher(uint32_t flags, mxtl::RefPtr<JobDispatcher> parent);
    void AddChildJob(JobDispatcher* job);
    void RemoveChildJob(JobDispatcher* job);

    const uint32_t flags_;
    StateTracker state_tracker_;
    const mxtl::RefPtr<JobDispatcher> parent_;

    mxtl::DoublyLinkedListNodeState<JobDispatcher*> dll_job_;

    // The |lock_| protects all members below.
    Mutex lock_;
    uint32_t process_count_;
    uint32_t job_count_;

    mxtl::DoublyLinkedList<JobDispatcher*, ListTraits> jobs_;
    mxtl::DoublyLinkedList<ProcessDispatcher*, ProcessDispatcher::JobListTraits> procs_;
};
