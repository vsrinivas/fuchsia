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
    MX_RIGHT_ENUMERATE | MX_RIGHT_GET_PROPERTY | MX_RIGHT_SET_PROPERTY;

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

    if (!parent->AddChildJob(job)) {
        delete job;
        return ERR_BAD_STATE;
    }

    *rights = kDefaultJobRights;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(job);
    return NO_ERROR;
}

JobDispatcher::JobDispatcher(uint32_t /*flags*/,
                             mxtl::RefPtr<JobDispatcher> parent)
    : parent_(mxtl::move(parent)),
      state_(State::READY),
      process_count_(0u), job_count_(0u),
      state_tracker_(MX_JOB_NO_PROCESSES|MX_JOB_NO_JOBS) {
}

JobDispatcher::~JobDispatcher() {
    if (parent_)
        parent_->RemoveChildJob(this);
}

void JobDispatcher::on_zero_handles() {
}

mx_koid_t JobDispatcher::get_related_koid() const {
    return parent_? parent_->get_koid() : 0u;
}

bool JobDispatcher::AddChildProcess(ProcessDispatcher* process) {
    AutoLock lock(&lock_);
    if (state_ != State::READY)
        return false;
    procs_.push_back(process);
    ++process_count_;
    UpdateSignalsIncrementLocked();
    return true;
}

bool JobDispatcher::AddChildJob(JobDispatcher* job) {
    AutoLock lock(&lock_);
    if (state_ != State::READY)
        return false;
    jobs_.push_back(job);
    ++job_count_;
    UpdateSignalsIncrementLocked();
    return true;
}

void JobDispatcher::RemoveChildProcess(ProcessDispatcher* process) {
    AutoLock lock(&lock_);
    // The process dispatcher can call us in its destructor or in Kill().
    if (!ProcessDispatcher::JobListTraitsWeak::node_state(*process).InContainer())
        return;
    procs_.erase(*process);
    --process_count_;
    UpdateSignalsDecrementLocked();
}

void JobDispatcher::RemoveChildJob(JobDispatcher* job) {
    AutoLock lock(&lock_);
    if (!JobDispatcher::ListTraitsWeak::node_state(*job).InContainer())
        return;
    jobs_.erase(*job);
    --job_count_;
    UpdateSignalsDecrementLocked();
}

void JobDispatcher::UpdateSignalsDecrementLocked() {
    DEBUG_ASSERT(lock_.IsHeld());
    // removing jobs or processes.
    mx_signals_t set = 0u;
    if (process_count_ == 0u) {
        DEBUG_ASSERT(procs_.is_empty());
        set |= MX_JOB_NO_PROCESSES;
    }
    if (job_count_ == 0u) {
        DEBUG_ASSERT(jobs_.is_empty());
        set |= MX_JOB_NO_JOBS;
    }

    if ((job_count_ == 0) && (process_count_ == 0)) {
        if (state_ == State::KILLING)
            state_ = State::READY;
    }

    state_tracker_.UpdateState(0u, set);
}

void JobDispatcher::UpdateSignalsIncrementLocked() {
    DEBUG_ASSERT(lock_.IsHeld());
    // Adding jobs or processes.
    mx_signals_t clear = 0u;
    if (process_count_ == 1u) {
        DEBUG_ASSERT(!procs_.is_empty());
        clear |= MX_JOB_NO_PROCESSES;
    }
    if (job_count_ == 1u) {
        DEBUG_ASSERT(!jobs_.is_empty());
        clear |= MX_JOB_NO_JOBS;
    }
    state_tracker_.UpdateState(clear, 0u);
}

void JobDispatcher::Kill() {
    JobList jobs_to_kill;
    ProcessList procs_to_kill;

    {
        AutoLock lock(&lock_);
        if (state_ != State::READY)
            return;

        // Short circuit if there is nothing to do. Notice |state_|
        // does not change. See note at the end of this function.
        if ((job_count_ == 0u) && (process_count_ == 0u))
            return;

        state_ = State::KILLING;

        // Convert our weak pointers into refcounted. We will do the killing
        // outside the lock.
        for (auto& j : jobs_) {
            jobs_to_kill.push_front(mxtl::RefPtr<JobDispatcher>(&j));
        }

        for (auto& p : procs_) {
            procs_to_kill.push_front(mxtl::RefPtr<ProcessDispatcher>(&p));
        }
    }

    // Since we kill the child jobs first we have a depth-first massacre.
    while (!jobs_to_kill.is_empty()) {
        // TODO(cpu): This recursive call can overflow the stack.
        jobs_to_kill.pop_front()->Kill();
    }

    while (!procs_to_kill.is_empty()) {
        procs_to_kill.pop_front()->Kill();
    }
}

bool JobDispatcher::EnumerateChildren(JobEnumerator* je) {
    AutoLock lock(&lock_);

    uint32_t proc_index = 0u;
    uint32_t job_index = 0u;

    if (!je->Size(process_count_, job_count_))
        return false;

    bool completed = true;

    for (auto& proc : procs_) {
        if (!je->OnProcess(&proc, proc_index++)) {
            completed = false;
            break;
        }
    }

    for (auto& job : jobs_) {
        if (!je->OnJob(&job, job_index++)) {
            completed = false;
            break;
        }
        // TODO(kulakowski) This recursive call can overflow the stack.
        job.EnumerateChildren(je);
    }
    return completed;
}

mxtl::RefPtr<ProcessDispatcher> JobDispatcher::LookupProcessById(mx_koid_t koid) {
    AutoLock lock(&lock_);
    for (auto& proc : procs_) {
        if (proc.get_koid() == koid)
            return mxtl::RefPtr<ProcessDispatcher>(&proc);
    }
    return nullptr;
}

mxtl::RefPtr<JobDispatcher> JobDispatcher::LookupJobById(mx_koid_t koid) {
    AutoLock lock(&lock_);
    for (auto& job : jobs_) {
        if (job.get_koid() == koid) {
            return mxtl::RefPtr<JobDispatcher>(&job);
        }
    }
    return nullptr;
}

void JobDispatcher::get_name(char out_name[MX_MAX_NAME_LEN]) const {
    AutoSpinLock lock(name_lock_);
    memcpy(out_name, name_, MX_MAX_NAME_LEN);
}

status_t JobDispatcher::set_name(const char* name, size_t len) {
    if (len >= MX_MAX_NAME_LEN)
        len = MX_MAX_NAME_LEN - 1;

    AutoSpinLock lock(name_lock_);
    memcpy(name_, name, len);
    memset(name_ + len, 0, MX_MAX_NAME_LEN - len);
    return NO_ERROR;
}
