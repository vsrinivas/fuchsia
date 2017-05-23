// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/job_dispatcher.h>

#include <err.h>

#include <kernel/auto_lock.h>

#include <magenta/process_dispatcher.h>
#include <magenta/syscalls/policy.h>
#include <mxalloc/new.h>

// The starting max_height value of the root job.
static const uint32_t kRootJobMaxHeight = 32;

constexpr mx_rights_t kDefaultJobRights =
    MX_RIGHT_TRANSFER | MX_RIGHT_DUPLICATE | MX_RIGHT_READ | MX_RIGHT_WRITE |
    MX_RIGHT_ENUMERATE | MX_RIGHT_GET_PROPERTY | MX_RIGHT_SET_PROPERTY |
    MX_RIGHT_SET_POLICY | MX_RIGHT_GET_POLICY;

mxtl::RefPtr<JobDispatcher> JobDispatcher::CreateRootJob() {
    AllocChecker ac;
    auto job = mxtl::AdoptRef(new (&ac) JobDispatcher(0u, nullptr, kPolicyEmpty));
    return ac.check() ? job  : nullptr;
}

status_t JobDispatcher::Create(uint32_t flags,
                               mxtl::RefPtr<JobDispatcher> parent,
                               mxtl::RefPtr<Dispatcher>* dispatcher,
                               mx_rights_t* rights) {
    if (parent != nullptr && parent->max_height() == 0) {
        // The parent job cannot have children.
        return ERR_OUT_OF_RANGE;
    }

    AllocChecker ac;
    auto job = new (&ac) JobDispatcher(flags, parent, parent->GetPolicy());
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
                             mxtl::RefPtr<JobDispatcher> parent,
                             pol_cookie_t policy)
    : parent_(mxtl::move(parent)),
      max_height_(parent_ ? parent_->max_height() - 1 : kRootJobMaxHeight),
      state_(State::READY),
      process_count_(0u),
      job_count_(0u),
      state_tracker_(MX_JOB_NO_PROCESSES | MX_JOB_NO_JOBS),
      policy_(policy) {
}

JobDispatcher::~JobDispatcher() {
    if (parent_)
        parent_->RemoveChildJob(this);
}

void JobDispatcher::on_zero_handles() {
    canary_.Assert();
}

mx_koid_t JobDispatcher::get_related_koid() const {
    return parent_? parent_->get_koid() : 0u;
}

bool JobDispatcher::AddChildProcess(ProcessDispatcher* process) {
    canary_.Assert();

    AutoLock lock(&lock_);
    if (state_ != State::READY)
        return false;
    procs_.push_back(process);
    ++process_count_;
    UpdateSignalsIncrementLocked();
    return true;
}

bool JobDispatcher::AddChildJob(JobDispatcher* job) {
    canary_.Assert();

    AutoLock lock(&lock_);
    if (state_ != State::READY)
        return false;

    jobs_.push_back(job);
    ++job_count_;
    UpdateSignalsIncrementLocked();
    return true;
}

void JobDispatcher::RemoveChildProcess(ProcessDispatcher* process) {
    canary_.Assert();

    AutoLock lock(&lock_);
    // The process dispatcher can call us in its destructor or in Kill().
    if (!ProcessDispatcher::JobListTraitsWeak::node_state(*process).InContainer())
        return;
    procs_.erase(*process);
    --process_count_;
    UpdateSignalsDecrementLocked();
}

void JobDispatcher::RemoveChildJob(JobDispatcher* job) {
    canary_.Assert();

    AutoLock lock(&lock_);
    if (!JobDispatcher::ListTraitsWeak::node_state(*job).InContainer())
        return;
    jobs_.erase(*job);
    --job_count_;
    UpdateSignalsDecrementLocked();
}

void JobDispatcher::UpdateSignalsDecrementLocked() {
    canary_.Assert();

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
    canary_.Assert();

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

pol_cookie_t JobDispatcher::GetPolicy() {
    AutoLock lock(&lock_);
    return policy_;
}

void JobDispatcher::Kill() {
    canary_.Assert();

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

status_t JobDispatcher::SetPolicy(
    uint32_t mode, const mx_policy_basic* in_policy, size_t policy_count) {
    // Can't set policy when there are active processes or jobs.
    AutoLock lock(&lock_);

    if (!procs_.is_empty() || !jobs_.is_empty())
        return ERR_BAD_STATE;

    pol_cookie_t new_policy;
    auto status = GetSystemPolicyManager()->AddPolicy(
        mode, policy_, in_policy, policy_count, &new_policy);

    if (status < 0)
        return status;

    policy_ = new_policy;
    return NO_ERROR;
}

bool JobDispatcher::EnumerateChildren(JobEnumerator* je, bool recurse) {
    canary_.Assert();

    AutoLock lock(&lock_);

    for (auto& proc : procs_) {
        if (!je->OnProcess(&proc)) {
            return false;
        }
    }

    for (auto& job : jobs_) {
        if (!je->OnJob(&job)) {
            return false;
        }
        if (recurse) {
            // TODO(kulakowski): This recursive call can overflow the stack.
            if (!job.EnumerateChildren(je, /* recurse */ true)) {
                return false;
            }
        }
    }
    return true;
}

mxtl::RefPtr<ProcessDispatcher> JobDispatcher::LookupProcessById(mx_koid_t koid) {
    canary_.Assert();

    AutoLock lock(&lock_);
    for (auto& proc : procs_) {
        if (proc.get_koid() == koid)
            return mxtl::RefPtr<ProcessDispatcher>(&proc);
    }
    return nullptr;
}

mxtl::RefPtr<JobDispatcher> JobDispatcher::LookupJobById(mx_koid_t koid) {
    canary_.Assert();

    AutoLock lock(&lock_);
    for (auto& job : jobs_) {
        if (job.get_koid() == koid) {
            return mxtl::RefPtr<JobDispatcher>(&job);
        }
    }
    return nullptr;
}

void JobDispatcher::get_name(char out_name[MX_MAX_NAME_LEN]) const {
    canary_.Assert();

    name_.get(MX_MAX_NAME_LEN, out_name);
}

status_t JobDispatcher::set_name(const char* name, size_t len) {
    canary_.Assert();

    return name_.set(name, len);
}
