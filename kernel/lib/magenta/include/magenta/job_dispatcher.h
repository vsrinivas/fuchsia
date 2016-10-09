// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/mutex.h>

#include <magenta/dispatcher.h>
#include <magenta/state_tracker.h>
#include <magenta/types.h>

#include <mxtl/intrusive_double_list.h>
#include <mxtl/ref_counted.h>

class JobNode;
class ProcessDispatcher;

class JobDispatcher final : public Dispatcher {
public:
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

    // Job methods.
    mx_status_t AddChildProcess(mxtl::RefPtr<ProcessDispatcher> process);
    mx_status_t AddChildJob(mxtl::RefPtr<JobDispatcher> job);
    void Kill();

private:
    JobDispatcher(uint32_t flags, JobDispatcher* parent);

    const uint32_t flags_;
    NonIrqStateTracker state_tracker_;

    // The |lock_| protects all members below.
    Mutex lock_;
    mxtl::DoublyLinkedList<mxtl::unique_ptr<JobNode>> children_;
    JobDispatcher* parent_;
};
