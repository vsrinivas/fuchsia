// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>

#include <object/job_dispatcher.h>

#include <err.h>

#include <zircon/rights.h>
#include <zircon/syscalls/policy.h>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include <object/process_dispatcher.h>

#include <platform.h>

// The starting max_height value of the root job.
static constexpr uint32_t kRootJobMaxHeight = 32;

static constexpr char kRootJobName[] = "<superroot>";

template <>
uint32_t JobDispatcher::ChildCountLocked<JobDispatcher>() const {
    return job_count_;
}

template <>
uint32_t JobDispatcher::ChildCountLocked<ProcessDispatcher>() const {
    return process_count_;
}

// Calls the provided |zx_status_t func(fbl::RefPtr<DISPATCHER_TYPE>)|
// function on all live elements of |children|, which must be one of |jobs_|
// or |procs_|. Stops iterating early if |func| returns a value other than
// ZX_OK, returning that value from this method. |lock_| must be held when
// calling this method, and it will still be held while the callback is
// called.
//
// The returned |LiveRefsArray| needs to be destructed when |lock_| is not
// held anymore. The recommended pattern is:
//
//  LiveRefsArray refs;
//  {
//      Guard<fbl::Mutex> guard{get_lock()};
//      refs = ForEachChildInLocked(...);
//  }
//
template <typename T, typename Fn>
JobDispatcher::LiveRefsArray JobDispatcher::ForEachChildInLocked(
    T& children, zx_status_t* result, Fn func) {
    // Convert child raw pointers into RefPtrs. This is tricky and requires
    // special logic on the RefPtr class to handle a ref count that can be
    // zero.
    //
    // The main requirement is that |lock_| is both controlling child
    // list lookup and also making sure that the child destructor cannot
    // make progress when doing so. In other words, when inspecting the
    // |children| list we can be sure that a given child process or child
    // job is either
    //   - alive, with refcount > 0
    //   - in destruction process but blocked, refcount == 0

    const uint32_t count = ChildCountLocked<typename T::ValueType>();

    if (!count) {
        *result = ZX_OK;
        return LiveRefsArray();
    }

    fbl::AllocChecker ac;
    LiveRefsArray refs(new (&ac) fbl::RefPtr<Dispatcher>[count], count);
    if (!ac.check()) {
        *result = ZX_ERR_NO_MEMORY;
        return LiveRefsArray();
    }

    size_t ix = 0;

    for (auto& craw : children) {
        auto cref = ::fbl::internal::MakeRefPtrUpgradeFromRaw(&craw, lock_.lock());
        if (!cref)
            continue;

        *result = func(cref);
        // |cref| might be the last reference at this point. If so,
        // when we drop it in the next iteration the object dtor
        // would be called here with the |get_lock()| held. To avoid that
        // we keep the reference alive in the |refs| array and pass
        // the responsibility of releasing them outside the lock to
        // the caller.
        refs[ix++] = fbl::move(cref);

        if (*result != ZX_OK)
            break;
    }

    return refs;
}

fbl::RefPtr<JobDispatcher> JobDispatcher::CreateRootJob() {
    fbl::AllocChecker ac;
    auto job = fbl::AdoptRef(new (&ac) JobDispatcher(0u, nullptr, kPolicyEmpty));
    if (!ac.check())
        return nullptr;
    job->set_name(kRootJobName, sizeof(kRootJobName));
    return job;
}

zx_status_t JobDispatcher::Create(uint32_t flags,
                                  fbl::RefPtr<JobDispatcher> parent,
                                  fbl::RefPtr<Dispatcher>* dispatcher,
                                  zx_rights_t* rights) {
    if (parent != nullptr && parent->max_height() == 0) {
        // The parent job cannot have children.
        return ZX_ERR_OUT_OF_RANGE;
    }

    fbl::AllocChecker ac;
    fbl::RefPtr<JobDispatcher> job =
        fbl::AdoptRef(new (&ac) JobDispatcher(flags, parent, parent->GetPolicy()));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    if (!parent->AddChildJob(job)) {
        return ZX_ERR_BAD_STATE;
    }

    *rights = ZX_DEFAULT_JOB_RIGHTS;
    *dispatcher = fbl::move(job);
    return ZX_OK;
}

JobDispatcher::JobDispatcher(uint32_t /*flags*/,
                             fbl::RefPtr<JobDispatcher> parent,
                             pol_cookie_t policy)
    : SoloDispatcher(ZX_JOB_NO_PROCESSES | ZX_JOB_NO_JOBS),
      parent_(fbl::move(parent)),
      max_height_(parent_ ? parent_->max_height() - 1 : kRootJobMaxHeight),
      state_(State::READY),
      process_count_(0u),
      job_count_(0u),
      policy_(policy) {

    // Set the initial job order, and try to make older jobs closer to
    // the root (both hierarchically and temporally) show up earlier
    // in enumeration.
    if (parent_ == nullptr) {
        // Root job is the most important.
        Guard<fbl::Mutex> guard{AllJobsLock::Get()};
        all_jobs_list_.push_back(this);
    } else {
        Guard<fbl::Mutex> parent_guard{parent_->get_lock()};
        JobDispatcher* neighbor;
        if (!parent_->jobs_.is_empty()) {
            // Our youngest sibling.
            //
            // IMPORTANT: We must hold the parent's lock during list insertion
            // to ensure that our sibling stays alive until we're done with it.
            // The sibling may be in its dtor right now, trying to remove itself
            // from parent_->jobs_ but blocked on parent_->get_lock(), and could be
            // freed if we released the lock.
            neighbor = &parent_->jobs_.back();

            // This can't be us: we aren't added to our parent's child list
            // until after construction.
            DEBUG_ASSERT(!dll_job_raw_.InContainer());
            DEBUG_ASSERT(neighbor != this);
        } else {
            // Our parent.
            neighbor = parent_.get();
        }

        // Make ourselves appear after our next-youngest neighbor.
        Guard<fbl::Mutex> guard{AllJobsLock::Get()};
        all_jobs_list_.insert(all_jobs_list_.make_iterator(*neighbor), this);
    }
}

JobDispatcher::~JobDispatcher() {
    if (parent_)
        parent_->RemoveChildJob(this);

    {
        Guard<fbl::Mutex> guard{AllJobsLock::Get()};
        DEBUG_ASSERT(dll_all_jobs_.InContainer());
        all_jobs_list_.erase(*this);
    }
}

zx_koid_t JobDispatcher::get_related_koid() const {
    return parent_ ? parent_->get_koid() : 0u;
}

bool JobDispatcher::AddChildProcess(const fbl::RefPtr<ProcessDispatcher>& process) {
    canary_.Assert();

    Guard<fbl::Mutex> guard{get_lock()};
    if (state_ != State::READY)
        return false;
    procs_.push_back(process.get());
    ++process_count_;
    UpdateSignalsIncrementLocked();
    return true;
}

bool JobDispatcher::AddChildJob(const fbl::RefPtr<JobDispatcher>& job) {
    canary_.Assert();

    Guard<fbl::Mutex> guard{get_lock()};
    if (state_ != State::READY)
        return false;

    jobs_.push_back(job.get());
    ++job_count_;
    UpdateSignalsIncrementLocked();
    return true;
}

void JobDispatcher::RemoveChildProcess(ProcessDispatcher* process) {
    canary_.Assert();

    Guard<fbl::Mutex> guard{get_lock()};
    // The process dispatcher can call us in its destructor, Kill(),
    // or RemoveThread().
    if (!ProcessDispatcher::JobListTraitsRaw::node_state(*process).InContainer())
        return;
    procs_.erase(*process);
    --process_count_;
    UpdateSignalsDecrementLocked();
}

void JobDispatcher::RemoveChildJob(JobDispatcher* job) {
    canary_.Assert();

    Guard<fbl::Mutex> guard{get_lock()};
    if (!JobDispatcher::ListTraitsRaw::node_state(*job).InContainer())
        return;
    jobs_.erase(*job);
    --job_count_;
    UpdateSignalsDecrementLocked();
}

void JobDispatcher::UpdateSignalsDecrementLocked() {
    canary_.Assert();

    DEBUG_ASSERT(get_lock()->lock().IsHeld());

    // removing jobs or processes.
    zx_signals_t set = 0u;
    if (process_count_ == 0u) {
        DEBUG_ASSERT(procs_.is_empty());
        set |= ZX_JOB_NO_PROCESSES;
    }
    if (job_count_ == 0u) {
        DEBUG_ASSERT(jobs_.is_empty());
        set |= ZX_JOB_NO_JOBS;
    }

    if ((job_count_ == 0) && (process_count_ == 0)) {
        if (state_ == State::KILLING)
            state_ = State::DEAD;

        if (!parent_) {
            // There are no userspace process left. From here, there's
            // no particular context as to whether this was
            // intentional, or if a core devhost crashed due to a
            // bug. Either way, shut down the kernel.
            platform_halt(HALT_ACTION_HALT, HALT_REASON_SW_RESET);
        }
    }

    UpdateStateLocked(0u, set);
}

void JobDispatcher::UpdateSignalsIncrementLocked() {
    canary_.Assert();

    DEBUG_ASSERT(get_lock()->lock().IsHeld());

    // Adding jobs or processes.
    zx_signals_t clear = 0u;
    if (process_count_ == 1u) {
        DEBUG_ASSERT(!procs_.is_empty());
        clear |= ZX_JOB_NO_PROCESSES;
    }
    if (job_count_ == 1u) {
        DEBUG_ASSERT(!jobs_.is_empty());
        clear |= ZX_JOB_NO_JOBS;
    }
    UpdateStateLocked(clear, 0u);
}

pol_cookie_t JobDispatcher::GetPolicy() {
    Guard<fbl::Mutex> guard{get_lock()};
    return policy_;
}

void JobDispatcher::Kill() {
    canary_.Assert();

    JobList jobs_to_kill;
    ProcessList procs_to_kill;

    LiveRefsArray jobs_refs;
    LiveRefsArray proc_refs;

    {
        Guard<fbl::Mutex> guard{get_lock()};
        if (state_ != State::READY)
            return;

        // Short circuit if there is nothing to do. Notice |state_|
        // does not change.
        if ((job_count_ == 0u) && (process_count_ == 0u))
            return;

        state_ = State::KILLING;
        zx_status_t result;

        // Safely gather refs to the children.
        jobs_refs = ForEachChildInLocked(jobs_, &result, [&](fbl::RefPtr<JobDispatcher> job) {
            jobs_to_kill.push_front(fbl::move(job));
            return ZX_OK;
        });
        proc_refs = ForEachChildInLocked(procs_, &result, [&](fbl::RefPtr<ProcessDispatcher> proc) {
            procs_to_kill.push_front(fbl::move(proc));
            return ZX_OK;
        });
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

zx_status_t JobDispatcher::SetPolicy(
    uint32_t mode, const zx_policy_basic* in_policy, size_t policy_count) {
    // Can't set policy when there are active processes or jobs.
    Guard<fbl::Mutex> guard{get_lock()};

    if (!procs_.is_empty() || !jobs_.is_empty())
        return ZX_ERR_BAD_STATE;

    pol_cookie_t new_policy;
    auto status = GetSystemPolicyManager()->AddPolicy(
        mode, policy_, in_policy, policy_count, &new_policy);

    if (status < 0)
        return status;

    policy_ = new_policy;
    return ZX_OK;
}

bool JobDispatcher::EnumerateChildren(JobEnumerator* je, bool recurse) {
    canary_.Assert();

    LiveRefsArray jobs_refs;
    LiveRefsArray proc_refs;

    zx_status_t result = ZX_OK;

    {
        Guard<fbl::Mutex> guard{get_lock()};

        proc_refs = ForEachChildInLocked(
            procs_, &result, [&](fbl::RefPtr<ProcessDispatcher> proc) {
                return je->OnProcess(proc.get()) ? ZX_OK : ZX_ERR_STOP;
            });
        if (result != ZX_OK) {
            return false;
        }

        jobs_refs = ForEachChildInLocked(jobs_, &result, [&](fbl::RefPtr<JobDispatcher> job) {
            if (!je->OnJob(job.get())) {
                return ZX_ERR_STOP;
            }
            if (recurse) {
                // TODO(kulakowski): This recursive call can overflow the stack.
                return job->EnumerateChildren(je, /* recurse */ true)
                           ? ZX_OK
                           : ZX_ERR_STOP;
            }
            return ZX_OK;
        });
    }

    return result == ZX_OK;
}

fbl::RefPtr<ProcessDispatcher>
JobDispatcher::LookupProcessById(zx_koid_t koid) {
    canary_.Assert();

    LiveRefsArray proc_refs;

    fbl::RefPtr<ProcessDispatcher> found_proc;
    {
        Guard<fbl::Mutex> guard{get_lock()};
        zx_status_t result;

        proc_refs = ForEachChildInLocked(procs_, &result, [&](fbl::RefPtr<ProcessDispatcher> proc) {
            if (proc->get_koid() == koid) {
                found_proc = fbl::move(proc);
                return ZX_ERR_STOP;
            }
            return ZX_OK;
        });
    }
    return found_proc; // Null if not found.
}

fbl::RefPtr<JobDispatcher>
JobDispatcher::LookupJobById(zx_koid_t koid) {
    canary_.Assert();

    LiveRefsArray jobs_refs;

    fbl::RefPtr<JobDispatcher> found_job;
    {
        Guard<fbl::Mutex> guard{get_lock()};
        zx_status_t result;

        jobs_refs = ForEachChildInLocked(jobs_, &result, [&](fbl::RefPtr<JobDispatcher> job) {
            if (job->get_koid() == koid) {
                found_job = fbl::move(job);
                return ZX_ERR_STOP;
            }
            return ZX_OK;
        });
    }
    return found_job; // Null if not found.
}

void JobDispatcher::get_name(char out_name[ZX_MAX_NAME_LEN]) const {
    canary_.Assert();

    name_.get(ZX_MAX_NAME_LEN, out_name);
}

zx_status_t JobDispatcher::set_name(const char* name, size_t len) {
    canary_.Assert();

    return name_.set(name, len);
}


// Global list of all jobs.
JobDispatcher::AllJobsList JobDispatcher::all_jobs_list_;

zx_status_t JobDispatcher::SetExceptionPort(fbl::RefPtr<ExceptionPort> eport) {
    canary_.Assert();

    DEBUG_ASSERT(eport->type() == ExceptionPort::Type::JOB);

    Guard<fbl::Mutex> lock{get_lock()};
    if (exception_port_)
        return ZX_ERR_ALREADY_BOUND;
    exception_port_ = fbl::move(eport);

    return ZX_OK;
}

class OnExceptionPortRemovalEnumerator final : public JobEnumerator {
public:
    OnExceptionPortRemovalEnumerator(fbl::RefPtr<ExceptionPort> eport)
        : eport_(fbl::move(eport)) {}
    OnExceptionPortRemovalEnumerator(const OnExceptionPortRemovalEnumerator&) = delete;

private:
    bool OnProcess(ProcessDispatcher* process) override {
        process->OnExceptionPortRemoval(eport_);
        // Keep looking.
        return true;
    }

    fbl::RefPtr<ExceptionPort> eport_;
};

bool JobDispatcher::ResetExceptionPort(bool quietly) {
    canary_.Assert();

    fbl::RefPtr<ExceptionPort> eport;
    {
        Guard<fbl::Mutex> lock{get_lock()};
        exception_port_.swap(eport);
        if (eport == nullptr) {
            // Attempted to unbind when no exception port is bound.
            return false;
        }
        // This method must guarantee that no caller will return until
        // OnTargetUnbind has been called on the port-to-unbind.
        // This becomes important when a manual unbind races with a
        // PortDispatcher::on_zero_handles auto-unbind.
        //
        // If OnTargetUnbind were called outside of the lock, it would lead to
        // a race (for threads A and B):
        //
        //   A: Calls ResetExceptionPort; acquires the lock
        //   A: Sees a non-null exception_port_, swaps it into the eport local.
        //      exception_port_ is now null.
        //   A: Releases the lock
        //
        //   B: Calls ResetExceptionPort; acquires the lock
        //   B: Sees a null exception_port_ and returns. But OnTargetUnbind()
        //      hasn't yet been called for the port.
        //
        // So, call it before releasing the lock.
        eport->OnTargetUnbind();
    }

    if (!quietly) {
        OnExceptionPortRemovalEnumerator remover(eport);
        if (!EnumerateChildren(&remover, true)) {
            DEBUG_ASSERT(false);
        }
    }
    return true;
}

fbl::RefPtr<ExceptionPort> JobDispatcher::exception_port() {
    Guard<fbl::Mutex> lock{get_lock()};
    return exception_port_;
}
