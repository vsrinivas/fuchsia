// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/job_dispatcher.h>

#include <err.h>
#include <new.h>

#include <kernel/auto_lock.h>

#include <magenta/process_dispatcher.h>

constexpr mx_rights_t kDefaultJobRights =
    MX_RIGHT_TRANSFER | MX_RIGHT_DUPLICATE | MX_RIGHT_READ | MX_RIGHT_WRITE |
    MX_RIGHT_ENUMERATE;

mxtl::RefPtr<JobDispatcher> JobDispatcher::CreateRootJob() {
    AllocChecker ac;
    auto job = mxtl::AdoptRef(new (&ac) JobDispatcher(0u, nullptr));
    return ac.check() ? job  : nullptr;
}

status_t JobDispatcher::Create(uint32_t flags,
                               mxtl::RefPtr<JobDispatcher> parent,
                               mxtl::RefPtr<Dispatcher>* dispatcher,
                               mx_rights_t* rights) {
    AllocChecker ac;
    auto job = new (&ac) JobDispatcher(flags, parent);
    if (!ac.check())
        return ERR_NO_MEMORY;

    parent->AddChildJob(job);

    *rights = kDefaultJobRights;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(job);
    return NO_ERROR;
}

JobDispatcher::JobDispatcher(uint32_t flags, mxtl::RefPtr<JobDispatcher> parent)
    : flags_(flags),
      parent_(mxtl::move(parent)),
      process_count_(0u), job_count_(0u) {
    state_tracker_.set_initial_signals_state(0u);
}

JobDispatcher::~JobDispatcher() {
    if (parent_)
        parent_->RemoveChildJob(this);
}

void JobDispatcher::on_zero_handles() {
}

mx_koid_t JobDispatcher::get_inner_koid() const {
    return parent_? parent_->get_koid() : 0u;
}

void JobDispatcher::AddChildProcess(ProcessDispatcher* process) {
    AutoLock lock(&lock_);
    procs_.push_back(process);
    ++process_count_;
}

void JobDispatcher::RemoveChildProcess(ProcessDispatcher* process) {
    AutoLock lock(&lock_);
    if (!ProcessDispatcher::ProcessListTraits::node_state(*process).InContainer())
        return;
    procs_.erase(*process);
    --process_count_;
}

void JobDispatcher::AddChildJob(JobDispatcher* job) {
    AutoLock lock(&lock_);
    jobs_.push_back(job);
    ++job_count_;
}

void JobDispatcher::RemoveChildJob(JobDispatcher* job) {
    AutoLock lock(&lock_);
    if (!JobDispatcher::ListTraits::node_state(*job).InContainer())
        return;
    jobs_.erase(*job);
    --job_count_;
}

void JobDispatcher::Kill() {
    while (true) {
        mxtl::RefPtr<ProcessDispatcher> proc;
        {
            AutoLock lock(&lock_);
            if (procs_.is_empty())
                break;
            proc.reset(procs_.pop_front());
        }
        proc->Kill();
    }

    while (true) {
        mxtl::RefPtr<JobDispatcher> job;
        {
            AutoLock lock(&lock_);
            if (jobs_.is_empty())
                break;
            job.reset(jobs_.pop_front());
        }
        // TODO(cpu): This can overflow the stack.
        job->Kill();
    }
}

bool JobDispatcher::EnumerateChildren(JobEnumerator* je) {
    AutoLock lock(&lock_);

    uint32_t proc_index = 0u;
    uint32_t job_index = 0u;

    if (!je->Size(process_count_, job_count_))
        return false;

    for (auto& proc : procs_) {
        if (!je->OnProcess(&proc, proc_index++))
            return false;
    }

    for (auto& job : jobs_) {
        if (!je->OnJob(&job, job_index++))
            return false;
    }
    return true;
}
