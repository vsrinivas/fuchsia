// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/job_dispatcher.h>

#include <err.h>

#include <kernel/auto_lock.h>

#include <magenta/process_dispatcher.h>
#include <magenta/rights.h>
#include <magenta/syscalls/policy.h>
#include <mxalloc/new.h>

// The starting max_height value of the root job.
static constexpr uint32_t kRootJobMaxHeight = 32;

static constexpr char kRootJobName[] = "<superroot>";

mxtl::RefPtr<JobDispatcher> JobDispatcher::CreateRootJob() {
    AllocChecker ac;
    auto job = mxtl::AdoptRef(new (&ac) JobDispatcher(0u, nullptr, kPolicyEmpty));
    if (!ac.check())
        return nullptr;
    job->set_name(kRootJobName, sizeof(kRootJobName));
    return job;
}

status_t JobDispatcher::Create(uint32_t flags,
                               mxtl::RefPtr<JobDispatcher> parent,
                               mxtl::RefPtr<Dispatcher>* dispatcher,
                               mx_rights_t* rights) {
    if (parent != nullptr && parent->max_height() == 0) {
        // The parent job cannot have children.
        return MX_ERR_OUT_OF_RANGE;
    }

    AllocChecker ac;
    auto job = new (&ac) JobDispatcher(flags, parent, parent->GetPolicy());
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    if (!parent->AddChildJob(job)) {
        delete job;
        return MX_ERR_BAD_STATE;
    }

    *rights = MX_DEFAULT_JOB_RIGHTS;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(job);
    return MX_OK;
}

JobDispatcher::JobDispatcher(uint32_t /*flags*/,
                             mxtl::RefPtr<JobDispatcher> parent,
                             pol_cookie_t policy)
    : parent_(mxtl::move(parent)),
      max_height_(parent_ ? parent_->max_height() - 1 : kRootJobMaxHeight),
      state_(State::READY),
      process_count_(0u),
      job_count_(0u),
      importance_(parent != nullptr
                      ? MX_JOB_IMPORTANCE_INHERITED
                      : MX_JOB_IMPORTANCE_MAX),
      state_tracker_(MX_JOB_NO_PROCESSES | MX_JOB_NO_JOBS),
      policy_(policy) {

    // Set the initial relative importance.
    // Tries to make older jobs closer to the root more important.
    AutoLock lock(&importance_lock_);
    if (parent_ == nullptr) {
        // Root job is the most important.
        importance_list_.push_back(this);
    } else {
        mxtl::RefPtr<JobDispatcher> neighbor;
        {
            AutoLock plock(&parent_->lock_);
            if (!parent_->jobs_.is_empty()) {
                // Our youngest sibling.
                neighbor = mxtl::WrapRefPtr(&parent_->jobs_.back());
                // We aren't added to our parent's child list until after
                // construction.
                DEBUG_ASSERT(!dll_job_raw_.InContainer());
                DEBUG_ASSERT(neighbor.get() != this);
            } else {
                // Our parent.
                neighbor = parent_;
            }
        }
        // Make ourselves slightly less important than our neighbor.
        importance_list_.insert( // before
            importance_list_.make_iterator(*neighbor.get()), this);
    }
}

JobDispatcher::~JobDispatcher() {
    if (parent_)
        parent_->RemoveChildJob(this);

    {
        AutoLock lock(&importance_lock_);
        DEBUG_ASSERT(dll_importance_.InContainer());
        importance_list_.erase(*this);
    }
}

void JobDispatcher::on_zero_handles() {
    canary_.Assert();
}

mx_koid_t JobDispatcher::get_related_koid() const {
    return parent_ ? parent_->get_koid() : 0u;
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
    if (!ProcessDispatcher::JobListTraitsRaw::node_state(*process).InContainer())
        return;
    procs_.erase(*process);
    --process_count_;
    UpdateSignalsDecrementLocked();
}

void JobDispatcher::RemoveChildJob(JobDispatcher* job) {
    canary_.Assert();

    AutoLock lock(&lock_);
    if (!JobDispatcher::ListTraitsRaw::node_state(*job).InContainer())
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
        if (!parent_)
            panic("No user processes left!\n");
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
        // does not change.
        if ((job_count_ == 0u) && (process_count_ == 0u))
            return;

        state_ = State::KILLING;

        // Convert our raw pointers into refcounted. We will do the killing
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
        return MX_ERR_BAD_STATE;

    pol_cookie_t new_policy;
    auto status = GetSystemPolicyManager()->AddPolicy(
        mode, policy_, in_policy, policy_count, &new_policy);

    if (status < 0)
        return status;

    policy_ = new_policy;
    return MX_OK;
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

status_t JobDispatcher::get_importance(mx_job_importance_t* out) const {
    canary_.Assert();
    DEBUG_ASSERT(out != nullptr);

    // Find the importance value that we inherit.
    const JobDispatcher* job = this;
    do {
        mx_job_importance_t imp = job->GetRawImportance();
        if (imp != MX_JOB_IMPORTANCE_INHERITED) {
            *out = imp;
            return MX_OK;
        }
        // Don't need to use RefPtrs or grab locks: our caller has a reference
        // to |this|, and there's a const RefPtr chain from |this| to all
        // ancestors.
        job = job->parent_.get();
    } while (job != nullptr);

    // The root job should always have a non-INHERITED importance.
    PANIC("Could not find inherited importance of job %" PRIu64 "\n",
          get_koid());
}

// Does not resolve MX_JOB_IMPORTANCE_INHERITED.
mx_job_importance_t JobDispatcher::GetRawImportance() const {
    canary_.Assert();
    AutoLock lock(&lock_);
    return importance_;
}

status_t JobDispatcher::set_importance(mx_job_importance_t importance) {
    canary_.Assert();

    if ((importance < MX_JOB_IMPORTANCE_MIN ||
         importance > MX_JOB_IMPORTANCE_MAX) &&
        importance != MX_JOB_IMPORTANCE_INHERITED) {
        return MX_ERR_OUT_OF_RANGE;
    }
    AutoLock lock(&lock_);
    // No-one is allowed to change the importance of the root job.  Note that
    // the actual root job ("<superroot>") typically isn't seen by userspace, so
    // no userspace program should see this error.  The job that userspace calls
    // "root" is actually a child of the real (super) root job.
    if (parent_ == nullptr) {
        return MX_ERR_ACCESS_DENIED;
    }
    importance_ = importance;
    return MX_OK;
}

// Global importance ranking. Note that this is independent of
// mx_task_importance_t-style importance as far as JobDispatcher is concerned;
// some other entity will choose how to order importance_list_.
Mutex JobDispatcher::importance_lock_;
JobDispatcher::JobImportanceList JobDispatcher::importance_list_;

status_t JobDispatcher::MakeMoreImportantThan(
    mxtl::RefPtr<JobDispatcher> other) {

    canary_.Assert();
    if (other != nullptr)
        other->canary_.Assert();

    // Update this job's position in the global job importance list.
    AutoLock lock(&importance_lock_);
    DEBUG_ASSERT(dll_importance_.InContainer());
    DEBUG_ASSERT(other == nullptr || other->dll_importance_.InContainer());

    importance_list_.erase(*this);
    DEBUG_ASSERT(!dll_importance_.InContainer());
    if (other == nullptr) {
        // Make this the least important.
        importance_list_.push_front(this);
    } else {
        // Insert just after the less-important other job.
        importance_list_.insert_after(
            importance_list_.make_iterator(*other), this);
    }
    DEBUG_ASSERT(dll_importance_.InContainer());

    return MX_OK;
}
