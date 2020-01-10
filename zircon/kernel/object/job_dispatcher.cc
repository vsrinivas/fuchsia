// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/job_dispatcher.h"

#include <err.h>
#include <inttypes.h>
#include <lib/counters.h>
#include <platform.h>
#include <zircon/rights.h>
#include <zircon/syscalls/policy.h>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/inline_array.h>
#include <fbl/mutex.h>
#include <object/process_dispatcher.h>

KCOUNTER(dispatcher_job_create_count, "dispatcher.job.create")
KCOUNTER(dispatcher_job_destroy_count, "dispatcher.job.destroy")

// The starting max_height value of the root job.
static constexpr uint32_t kRootJobMaxHeight = 32;

static constexpr char kRootJobName[] = "root";

template <>
uint32_t JobDispatcher::ChildCountLocked<JobDispatcher>() const {
  return job_count_;
}

template <>
uint32_t JobDispatcher::ChildCountLocked<ProcessDispatcher>() const {
  return process_count_;
}

// To come up with an order on our recursive locks we take advantage of the fact that our
// max_height reduces from parent to child. As we acquire locks from parent->child we can build an
// increasing counter by inverting the max_height. We add 1 to the counter just so the order value
// of 0 is reserved for the default order when the lock is acquired without an order being
// specified.
uint32_t JobDispatcher::LockOrder() const { return kRootJobMaxHeight - max_height() + 1; }

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
JobDispatcher::LiveRefsArray JobDispatcher::ForEachChildInLocked(T& children, zx_status_t* result,
                                                                 Fn func) {
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
    auto cref = ::fbl::MakeRefPtrUpgradeFromRaw(&craw, get_lock());
    if (!cref)
      continue;

    *result = func(cref);
    // |cref| might be the last reference at this point. If so,
    // when we drop it in the next iteration the object dtor
    // would be called here with the |get_lock()| held. To avoid that
    // we keep the reference alive in the |refs| array and pass
    // the responsibility of releasing them outside the lock to
    // the caller.
    refs[ix++] = ktl::move(cref);

    if (*result != ZX_OK)
      break;
  }

  return refs;
}

fbl::RefPtr<JobDispatcher> JobDispatcher::CreateRootJob() {
  fbl::AllocChecker ac;
  auto job = fbl::AdoptRef(new (&ac) JobDispatcher(0u, nullptr, JobPolicy::CreateRootPolicy()));
  if (!ac.check())
    return nullptr;
  job->set_name(kRootJobName, sizeof(kRootJobName));
  return job;
}

zx_status_t JobDispatcher::Create(uint32_t flags, fbl::RefPtr<JobDispatcher> parent,
                                  KernelHandle<JobDispatcher>* handle, zx_rights_t* rights) {
  if (parent != nullptr && parent->max_height() == 0) {
    // The parent job cannot have children.
    return ZX_ERR_OUT_OF_RANGE;
  }

  fbl::AllocChecker ac;
  KernelHandle new_handle(
      fbl::AdoptRef(new (&ac) JobDispatcher(flags, parent, parent->GetPolicy())));
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  if (!parent->AddChildJob(new_handle.dispatcher())) {
    return ZX_ERR_BAD_STATE;
  }

  *rights = default_rights();
  *handle = ktl::move(new_handle);
  return ZX_OK;
}

JobDispatcher::JobDispatcher(uint32_t /*flags*/, fbl::RefPtr<JobDispatcher> parent,
                             JobPolicy policy)
    : SoloDispatcher(ZX_JOB_NO_PROCESSES | ZX_JOB_NO_JOBS),
      parent_(ktl::move(parent)),
      max_height_(parent_ ? parent_->max_height() - 1 : kRootJobMaxHeight),
      state_(State::READY),
      process_count_(0u),
      job_count_(0u),
      return_code_(0),
      kill_on_oom_(false),
      policy_(policy),
      exceptionate_(ZX_EXCEPTION_CHANNEL_TYPE_JOB),
      debug_exceptionate_(ZX_EXCEPTION_CHANNEL_TYPE_JOB_DEBUGGER) {
  kcounter_add(dispatcher_job_create_count, 1);
}

JobDispatcher::~JobDispatcher() {
  kcounter_add(dispatcher_job_destroy_count, 1);
  RemoveFromJobTreesUnlocked();
}

zx_koid_t JobDispatcher::get_related_koid() const { return parent_ ? parent_->get_koid() : 0u; }

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

  // Put the new job after our next-youngest child, or us if we have none.
  //
  // We try to make older jobs closer to the root (both hierarchically and
  // temporally) show up earlier in enumeration.
  JobDispatcher* neighbor = (jobs_.is_empty() ? this : &jobs_.back());

  // This can only be called once, the job should not already be part
  // of any job tree.
  DEBUG_ASSERT(!job->dll_job_raw_.InContainer());
  DEBUG_ASSERT(neighbor != job.get());

  jobs_.push_back(job.get());
  ++job_count_;
  UpdateSignalsIncrementLocked();
  return true;
}

void JobDispatcher::RemoveChildProcess(ProcessDispatcher* process) {
  canary_.Assert();

  bool should_die = false;
  {
    Guard<fbl::Mutex> guard{get_lock()};
    // The process dispatcher can call us in its destructor, Kill(),
    // or RemoveThread().
    if (!ProcessDispatcher::JobListTraitsRaw::node_state(*process).InContainer())
      return;
    procs_.erase(*process);
    --process_count_;
    UpdateSignalsDecrementLocked();
    should_die = IsReadyForDeadTransitionLocked();
  }

  if (should_die)
    FinishDeadTransitionUnlocked();
}

void JobDispatcher::RemoveChildJob(JobDispatcher* job) {
  canary_.Assert();

  bool should_die = false;
  {
    Guard<fbl::Mutex> guard{get_lock()};
    if (!JobDispatcher::ListTraitsRaw::node_state(*job).InContainer())
      return;
    jobs_.erase(*job);
    --job_count_;
    UpdateSignalsDecrementLocked();
    should_die = IsReadyForDeadTransitionLocked();
  }

  if (should_die)
    FinishDeadTransitionUnlocked();
}

JobDispatcher::State JobDispatcher::GetState() const {
  Guard<fbl::Mutex> guard{get_lock()};
  return state_;
}

void JobDispatcher::RemoveFromJobTreesUnlocked() {
  canary_.Assert();

  if (parent_)
    parent_->RemoveChildJob(this);
}

bool JobDispatcher::IsReadyForDeadTransitionLocked() {
  canary_.Assert();
  return state_ == State::KILLING && job_count_ == 0 && process_count_ == 0;
}

void JobDispatcher::FinishDeadTransitionUnlocked() {
  canary_.Assert();

  // Make sure we're killing from the bottom of the tree up or else parent
  // jobs could die before their children.
  //
  // In particular, this means we have to finish dying before leaving the job
  // trees, since the last child leaving the tree can trigger its parent to
  // finish dying.
  DEBUG_ASSERT(!parent_ || (parent_->GetState() != State::DEAD));
  {
    Guard<fbl::Mutex> guard{get_lock()};
    state_ = State::DEAD;
    exceptionate_.Shutdown();
    debug_exceptionate_.Shutdown();
    UpdateStateLocked(0u, ZX_TASK_TERMINATED);
  }

  RemoveFromJobTreesUnlocked();
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

  if (!parent_ && (job_count_ == 0) && (process_count_ == 0)) {
    // There are no userspace process left. From here, there's
    // no particular context as to whether this was
    // intentional, or if a core devhost crashed due to a
    // bug. Either way, shut down the kernel.
    platform_halt(HALT_ACTION_HALT, HALT_REASON_SW_RESET);
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

JobPolicy JobDispatcher::GetPolicy() const {
  Guard<fbl::Mutex> guard{get_lock()};
  return policy_;
}

bool JobDispatcher::KillJobWithKillOnOOM() {
  // Get list of jobs with kill bit set.
  OOMBitJobArray oom_jobs;
  int count = 0;
  CollectJobsWithOOMBit(&oom_jobs, &count);
  if (count == 0) {
    printf("OOM: no jobs with kill_on_oom found\n");
    return false;
  }

  // Sort by max height. TODO(scottmg): This is not stable, which makes the
  // ordering unpredictable in theory. We don't currently have a stable sort in
  // kernel.
  qsort(reinterpret_cast<void*>(oom_jobs.begin()), count, sizeof(oom_jobs[0]),
        [](const void* a, const void* b) -> int {
          const auto& ta = *reinterpret_cast<const fbl::RefPtr<JobDispatcher>*>(a);
          const auto& tb = *reinterpret_cast<const fbl::RefPtr<JobDispatcher>*>(b);
          return ta->max_height() < tb->max_height();
        });

  // Kill lowest to highest until we find something to kill.
  for (int i = count - 1; i >= 0; --i) {
    auto& job = oom_jobs[i];
    if (job->Kill(ZX_TASK_RETCODE_OOM_KILL)) {
      char name[ZX_MAX_NAME_LEN];
      job->get_name(name);
      printf("OOM: killing %" PRIu64 " '%s'\n", job->get_koid(), name);
      return true;
    }
  }

  printf("OOM: no job found to kill\n");
  return false;
}

void JobDispatcher::CollectJobsWithOOMBit(OOMBitJobArray* into, int* count) {
  // As CollectJobsWithOOMBit will recurse we need to give a lock order to the guard.
  Guard<fbl::Mutex> guard{&lock_, LockOrder()};
  // We had to take the guard directly on lock_ above as the get_lock() virtual method erases the
  // Nestasble type information. The AssertHeld here allows us to restore the clang capability
  // analysis.
  AssertHeld(*get_lock());

  if (kill_on_oom_) {
    if (*count >= static_cast<int>(into->size())) {
      printf("OOM: skipping some jobs, exceeded max count\n");
      return;
    }

    auto cref = ::fbl::MakeRefPtrUpgradeFromRaw(this, get_lock());
    if (!cref)
      return;
    (*into)[*count] = ktl::move(cref);
    *count += 1;
  }

  for (auto& job : jobs_) {
    job.CollectJobsWithOOMBit(into, count);
  }
}

bool JobDispatcher::Kill(int64_t return_code) {
  canary_.Assert();

  JobList jobs_to_kill;
  ProcessList procs_to_kill;

  LiveRefsArray jobs_refs;
  LiveRefsArray proc_refs;

  bool should_die = false;
  {
    Guard<fbl::Mutex> guard{get_lock()};
    if (state_ != State::READY)
      return false;

    return_code_ = return_code;
    state_ = State::KILLING;
    zx_status_t result;

    // Safely gather refs to the children.
    jobs_refs = ForEachChildInLocked(jobs_, &result, [&](fbl::RefPtr<JobDispatcher> job) {
      jobs_to_kill.push_front(ktl::move(job));
      return ZX_OK;
    });
    proc_refs = ForEachChildInLocked(procs_, &result, [&](fbl::RefPtr<ProcessDispatcher> proc) {
      procs_to_kill.push_front(ktl::move(proc));
      return ZX_OK;
    });

    should_die = IsReadyForDeadTransitionLocked();
  }

  if (should_die)
    FinishDeadTransitionUnlocked();

  // Since we kill the child jobs first we have a depth-first massacre.
  while (!jobs_to_kill.is_empty()) {
    // TODO(cpu): This recursive call can overflow the stack.
    jobs_to_kill.pop_front()->Kill(return_code);
  }

  while (!procs_to_kill.is_empty()) {
    procs_to_kill.pop_front()->Kill(return_code);
  }

  return true;
}

bool JobDispatcher::CanSetPolicy() TA_REQ(get_lock()) {
  // Can't set policy when there are active processes or jobs. This constraint ensures that a
  // process's policy cannot change over its lifetime.  Because a process's policy cannot change,
  // the risk of TOCTOU bugs is reduced and we are free to apply policy at the ProcessDispatcher
  // without having to walk up the tree to its containing job.
  if (!procs_.is_empty() || !jobs_.is_empty()) {
    return false;
  }
  return true;
}

zx_status_t JobDispatcher::SetBasicPolicy(uint32_t mode, const zx_policy_basic_v1_t* in_policy,
                                          size_t policy_count) {
  fbl::AllocChecker ac;
  fbl::InlineArray<zx_policy_basic_v2_t, kPolicyBasicInlineCount> policy(&ac, policy_count);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  for (size_t ix = 0; ix != policy.size(); ++ix) {
    policy[ix].condition = in_policy[ix].condition;
    policy[ix].action = in_policy[ix].policy;
    policy[ix].flags = ZX_POL_OVERRIDE_DENY;
  }

  return SetBasicPolicy(mode, policy.get(), policy.size());
}

zx_status_t JobDispatcher::SetBasicPolicy(uint32_t mode, const zx_policy_basic_v2_t* in_policy,
                                          size_t policy_count) {
  Guard<fbl::Mutex> guard{get_lock()};

  if (!CanSetPolicy()) {
    return ZX_ERR_BAD_STATE;
  }
  return policy_.AddBasicPolicy(mode, in_policy, policy_count);
}

zx_status_t JobDispatcher::SetTimerSlackPolicy(const zx_policy_timer_slack& policy) {
  Guard<fbl::Mutex> guard{get_lock()};

  if (!CanSetPolicy()) {
    return ZX_ERR_BAD_STATE;
  }

  // Is the policy valid?
  if (policy.min_slack < 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  slack_mode new_mode;
  switch (policy.default_mode) {
    case ZX_TIMER_SLACK_CENTER:
      new_mode = TIMER_SLACK_CENTER;
      break;
    case ZX_TIMER_SLACK_EARLY:
      new_mode = TIMER_SLACK_EARLY;
      break;
    case ZX_TIMER_SLACK_LATE:
      new_mode = TIMER_SLACK_LATE;
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  };

  const TimerSlack old_slack = policy_.GetTimerSlack();
  const zx_duration_t new_amount = fbl::max(old_slack.amount(), policy.min_slack);
  const TimerSlack new_slack(new_amount, new_mode);

  policy_.SetTimerSlack(new_slack);

  return ZX_OK;
}

bool JobDispatcher::EnumerateChildren(JobEnumerator* je, bool recurse) {
  canary_.Assert();

  LiveRefsArray jobs_refs;
  LiveRefsArray proc_refs;

  zx_status_t result = ZX_OK;

  {
    // As EnumerateChildren will recurse we need to give a lock order to the guard.
    Guard<fbl::Mutex> guard{&lock_, LockOrder()};
    // We had to take the guard directly on lock_ above as the get_lock() virtual method erases the
    // Nestasble type information. The AssertHeld here allows us to restore the clang capability
    // analysis.
    AssertHeld(*get_lock());

    proc_refs = ForEachChildInLocked(procs_, &result, [&](fbl::RefPtr<ProcessDispatcher> proc) {
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
        return job->EnumerateChildren(je, /* recurse */ true) ? ZX_OK : ZX_ERR_STOP;
      }
      return ZX_OK;
    });
  }

  return result == ZX_OK;
}

fbl::RefPtr<ProcessDispatcher> JobDispatcher::LookupProcessById(zx_koid_t koid) {
  canary_.Assert();

  LiveRefsArray proc_refs;

  fbl::RefPtr<ProcessDispatcher> found_proc;
  {
    Guard<fbl::Mutex> guard{get_lock()};
    zx_status_t result;

    proc_refs = ForEachChildInLocked(procs_, &result, [&](fbl::RefPtr<ProcessDispatcher> proc) {
      if (proc->get_koid() == koid) {
        found_proc = ktl::move(proc);
        return ZX_ERR_STOP;
      }
      return ZX_OK;
    });
  }
  return found_proc;  // Null if not found.
}

fbl::RefPtr<JobDispatcher> JobDispatcher::LookupJobById(zx_koid_t koid) {
  canary_.Assert();

  LiveRefsArray jobs_refs;

  fbl::RefPtr<JobDispatcher> found_job;
  {
    Guard<fbl::Mutex> guard{get_lock()};
    zx_status_t result;

    jobs_refs = ForEachChildInLocked(jobs_, &result, [&](fbl::RefPtr<JobDispatcher> job) {
      if (job->get_koid() == koid) {
        found_job = ktl::move(job);
        return ZX_ERR_STOP;
      }
      return ZX_OK;
    });
  }
  return found_job;  // Null if not found.
}

void JobDispatcher::get_name(char out_name[ZX_MAX_NAME_LEN]) const {
  canary_.Assert();

  name_.get(ZX_MAX_NAME_LEN, out_name);
}

zx_status_t JobDispatcher::set_name(const char* name, size_t len) {
  canary_.Assert();

  return name_.set(name, len);
}

Exceptionate* JobDispatcher::exceptionate(Exceptionate::Type type) {
  canary_.Assert();
  return type == Exceptionate::Type::kDebug ? &debug_exceptionate_ : &exceptionate_;
}

void JobDispatcher::set_kill_on_oom(bool value) {
  Guard<fbl::Mutex> guard{get_lock()};
  kill_on_oom_ = value;
}

bool JobDispatcher::get_kill_on_oom() const {
  Guard<fbl::Mutex> guard{get_lock()};
  return kill_on_oom_;
}

void JobDispatcher::GetInfo(zx_info_job_t* info) const {
  canary_.Assert();

  Guard<fbl::Mutex> guard{get_lock()};
  info->return_code = return_code_;
  info->exited = (state_ == State::DEAD);
  info->kill_on_oom = kill_on_oom_;
  info->debugger_attached = debug_exceptionate_.HasValidChannel();
}
