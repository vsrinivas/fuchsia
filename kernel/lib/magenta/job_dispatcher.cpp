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

mx_koid_t JobDispatcher::get_inner_koid() const {
    return parent_? parent_->get_koid() : 0u;
}

bool JobDispatcher::AddChildProcess(ProcessDispatcher* process) {
    AutoLock lock(&lock_);
    if (state_ != State::READY)
        return false;
    procs_.push_back(process);
    ++process_count_;
    MaybeUpdateSignalsLocked(false);
    return true;
}

bool JobDispatcher::AddChildJob(JobDispatcher* job) {
    AutoLock lock(&lock_);
    if (state_ != State::READY)
        return false;
    jobs_.push_back(job);
    ++job_count_;
    MaybeUpdateSignalsLocked(false);
    return true;
}

void JobDispatcher::RemoveChildProcess(ProcessDispatcher* process) {
    AutoLock lock(&lock_);
    if (state_ != State::READY)
        return;
    // The process dispatcher can call us in its destructor or in Kill().
    if (!ProcessDispatcher::JobListTraits::node_state(*process).InContainer())
        return;
    procs_.erase(*process);
    --process_count_;
    MaybeUpdateSignalsLocked(true);
}

void JobDispatcher::RemoveChildJob(JobDispatcher* job) {
    AutoLock lock(&lock_);
    if (state_ != State::READY)
        return;
    jobs_.erase(*job);
    --job_count_;
    MaybeUpdateSignalsLocked(true);
}

void JobDispatcher::MaybeUpdateSignalsLocked(bool is_decrement) {
    DEBUG_ASSERT(lock_.IsHeld());

    // Note on the asserts below. Asserting when job or process is
    // non-zero only makes sense when the counts are incrementing.
    // This is because Kill() clears the lists up front.

    if (is_decrement) {
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
        state_tracker_.UpdateState(0u, set);
    } else {
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
}

void JobDispatcher::Kill() {
    WeakProcessList procs_to_kill;
    WeakJobList jobs_to_kill;

    {
        AutoLock lock(&lock_);
        if (state_ != State::READY)
            return;

        // Short circuit if there is nothing to do. Notice |state_|
        // does not change. See note at the end of this function.
        if ((job_count_ == 0u) && (process_count_ == 0u))
            return;

        state_ = State::DYING;
        procs_to_kill.swap(procs_);
        jobs_to_kill.swap(jobs_);

        process_count_ = 0u;
        job_count_ = 0u;
    }

    // Since we kill the child jobs first we have a depth-first massacre.
    while (!jobs_to_kill.is_empty()) {
        mxtl::RefPtr<JobDispatcher> job(jobs_to_kill.pop_front());
        // TODO(cpu): This recursive call can overflow the stack.
        job->Kill();
    }

    while (!procs_to_kill.is_empty()) {
        mxtl::RefPtr<ProcessDispatcher> proc(procs_to_kill.pop_front());
        proc->Kill();
    }

    {
        AutoLock lock(&lock_);
        DEBUG_ASSERT((process_count_ == 0u) && (job_count_ == 0u));
        state_tracker_.UpdateState(0u, MX_JOB_NO_PROCESSES | MX_JOB_NO_JOBS);

        // BEWARE: Processes and threads transition to DEAD at this
        // point. Here we transition to READY so clients can create further
        // jobs or processes. Reason is that unlike them, nothing irreversible
        // has happended.
        state_ = State::READY;
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
