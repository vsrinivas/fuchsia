// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_JOB_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_JOB_DISPATCHER_H_

#include <stdint.h>
#include <zircon/types.h>

#include <fbl/array.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/name.h>
#include <fbl/ref_counted.h>
#include <kernel/lockdep.h>
#include <ktl/array.h>
#include <object/dispatcher.h>
#include <object/exceptionate.h>
#include <object/excp_port.h>
#include <object/handle.h>
#include <object/job_policy.h>
#include <object/process_dispatcher.h>

class JobNode;

// Assume the typical set-policy call has 8 items or less.
constexpr size_t kPolicyBasicInlineCount = 8;

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

// This class implements the Job object kernel interface. Each Job has a parent
// Job and zero or more child Jobs and zero or more Child processes. This
// creates a DAG (tree) that connects every living task in the system.
// This is critically important because of the bottoms up refcount nature of
// the system in which the scheduler keeps alive the thread and the thread keeps
// alive the process, so without the Job it would not be possible to enumerate
// or control the tasks in the system for which there are no outstanding handles.
//
// The second important job of the Job is to apply policies that cannot otherwise
// be easily enforced by capabilities, for example kernel object creation.
//
// The third one is to support exception propagation from the leaf tasks to
// the root tasks.
//
// Obviously there is a special case for the 'root' Job which its parent is null
// and in the current implementation will call platform_halt() when its process
// and job count reaches zero. The root job is not exposed to user mode, instead
// the single child Job of the root job is given to the userboot process.
class JobDispatcher final : public SoloDispatcher<JobDispatcher, ZX_DEFAULT_JOB_RIGHTS> {
 public:
  // Traits to belong to the parent's raw job list.
  struct ListTraitsRaw {
    static fbl::DoublyLinkedListNodeState<JobDispatcher*>& node_state(JobDispatcher& obj) {
      return obj.dll_job_raw_;
    }
  };

  // Traits to belong to the parent's job list.
  struct ListTraits {
    static fbl::SinglyLinkedListNodeState<fbl::RefPtr<JobDispatcher>>& node_state(
        JobDispatcher& obj) {
      return obj.dll_job_;
    }
  };

  static fbl::RefPtr<JobDispatcher> CreateRootJob();
  static zx_status_t Create(uint32_t flags, fbl::RefPtr<JobDispatcher> parent,
                            KernelHandle<JobDispatcher>* handle, zx_rights_t* rights);

  ~JobDispatcher() final;

  // Dispatcher implementation.
  zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_JOB; }
  zx_koid_t get_related_koid() const final;
  fbl::RefPtr<JobDispatcher> parent() { return fbl::RefPtr<JobDispatcher>(parent_); }

  // Job methods.
  void get_name(char out_name[ZX_MAX_NAME_LEN]) const final;
  zx_status_t set_name(const char* name, size_t len) final;
  uint32_t max_height() const { return max_height_; }

  bool AddChildProcess(const fbl::RefPtr<ProcessDispatcher>& process);
  void RemoveChildProcess(ProcessDispatcher* process);

  // Terminate the child processes and jobs. Returns |false| if the job is already
  // in the process of killing, or the children are already terminated. Regardless
  // of return value, the Job now will not accept new children and eventually
  // transitions to |DEAD|.  |return_code| can be obtained via ZX_INFO_JOB.
  bool Kill(int64_t return_code);

  // Set basic policy. |mode| is is either ZX_JOB_POL_RELATIVE or ZX_JOB_POL_ABSOLUTE and
  // in_policy is an array of |count| elements.
  //
  // It is an error to set policy on a non-empty job, i.e. a job with one or more sub-jobs or
  // processes.

  // V1 flavor (on its way out)
  zx_status_t SetBasicPolicy(uint32_t mode, const zx_policy_basic_v1* in_policy,
                             size_t policy_count);
  // V2 flavor (on its way in)
  zx_status_t SetBasicPolicy(uint32_t mode, const zx_policy_basic_v2* in_policy,
                             size_t policy_count);

  // Set timer slack policy.
  //
  // |policy.min_slack| must be >= 0.
  //
  // |policy.default_mode| must be one of ZX_TIMER_SLACK_CENTER, ZX_TIMER_SLACK_EARLY,
  // ZX_TIMER_SLACK_LATE.
  //
  // It is an error to set policy on a non-empty job, i.e. a job with one or more sub-jobs or
  // processes.
  zx_status_t SetTimerSlackPolicy(const zx_policy_timer_slack& policy);

  JobPolicy GetPolicy() const;

  // Kills its lowest child job that has get_kill_on_oom() set.
  // Returns false if no alive child job had get_kill_on_oom() set.
  bool KillJobWithKillOnOOM();

  // Walks the job/process tree and invokes |je| methods on each node. If
  // |recurse| is false, only visits direct children of this job. Returns
  // false if any methods of |je| return false; returns true otherwise.
  bool EnumerateChildren(JobEnumerator* je, bool recurse);

  fbl::RefPtr<ProcessDispatcher> LookupProcessById(zx_koid_t koid);
  fbl::RefPtr<JobDispatcher> LookupJobById(zx_koid_t koid);

  // exception handling support
  zx_status_t SetExceptionPort(fbl::RefPtr<ExceptionPort> eport);
  // Returns true if a port had been set.
  bool ResetExceptionPort(bool debugger);
  fbl::RefPtr<ExceptionPort> exception_port();
  fbl::RefPtr<ExceptionPort> debugger_exception_port();

  // TODO(ZX-3072): remove the port-based exception code once everyone is
  // switched over to channels.
  Exceptionate* exceptionate(Exceptionate::Type type);

  void set_kill_on_oom(bool kill);
  bool get_kill_on_oom() const;

  void GetInfo(zx_info_job_t* info) const;

 private:
  enum class State { READY, KILLING, DEAD };

  using LiveRefsArray = fbl::Array<fbl::RefPtr<Dispatcher>>;

  JobDispatcher(uint32_t flags, fbl::RefPtr<JobDispatcher> parent, JobPolicy policy);

  bool AddChildJob(const fbl::RefPtr<JobDispatcher>& job);
  void RemoveChildJob(JobDispatcher* job);

  State GetState() const;

  // Remove this job from its parent's job list and the global job tree,
  // either when the job was killed or its last reference was dropped.
  // It's safe to call this multiple times.
  //
  // We cannot be holding our lock when we call this because it requires
  // locking our parent, and we only nest locks down the tree.
  void RemoveFromJobTreesUnlocked() TA_EXCL(get_lock());

  // Helpers to transition into the DEAD state.
  //
  // The check for whether we should transition needs to be done under the
  // lock, but actually moving into the dead state has to be done after
  // releasing the lock.
  //
  // FinishDeadTransitionUnlocked() is thread-safe and idempotent so it's OK
  // if multiple concurrent threads end up calling it.
  bool IsReadyForDeadTransitionLocked() TA_REQ(get_lock());
  void FinishDeadTransitionUnlocked() TA_EXCL(get_lock());

  void UpdateSignalsIncrementLocked() TA_REQ(get_lock());
  void UpdateSignalsDecrementLocked() TA_REQ(get_lock());

  template <typename T, typename Fn>
  __attribute__((warn_unused_result)) LiveRefsArray ForEachChildInLocked(T& children,
                                                                         zx_status_t* status,
                                                                         Fn func)
      TA_REQ(get_lock());

  template <typename T>
  uint32_t ChildCountLocked() const TA_REQ(get_lock());

  bool CanSetPolicy() TA_REQ(get_lock());

  using OOMBitJobArray = ktl::array<fbl::RefPtr<JobDispatcher>, 8>;

  // Collects all jobs with get_kill_on_oom() up to the maxiumum fixed size of a
  // OOMBitJobArray array. RefPtrs stored in |into| must be released once the
  // corresponding job lock has been released. |count| is an in/out parameter
  // that must start at 0, and will indicate the number of elements in |into| on
  // return. |count| will not exceed the fixed capacity of OOMBitJobArray.
  void CollectJobsWithOOMBit(OOMBitJobArray* into, int* count);

  const fbl::RefPtr<JobDispatcher> parent_;
  const uint32_t max_height_;

  fbl::DoublyLinkedListNodeState<JobDispatcher*> dll_job_raw_;
  fbl::SinglyLinkedListNodeState<fbl::RefPtr<JobDispatcher>> dll_job_;

  // The user-friendly job name. For debug purposes only. That
  // is, there is no mechanism to mint a handle to a job via this name.
  fbl::Name<ZX_MAX_NAME_LEN> name_;

  // The common |get_lock()| protects all members below.
  State state_ TA_GUARDED(get_lock());
  uint32_t process_count_ TA_GUARDED(get_lock());
  uint32_t job_count_ TA_GUARDED(get_lock());
  int64_t return_code_ TA_GUARDED(get_lock());
  // TODO(cpu): The OOM kill system is incomplete, see ZX-2731 for details.
  bool kill_on_oom_ TA_GUARDED(get_lock());

  using RawJobList = fbl::DoublyLinkedList<JobDispatcher*, ListTraitsRaw>;
  using RawProcessList =
      fbl::DoublyLinkedList<ProcessDispatcher*, ProcessDispatcher::JobListTraitsRaw>;

  using ProcessList =
      fbl::SinglyLinkedList<fbl::RefPtr<ProcessDispatcher>, ProcessDispatcher::JobListTraits>;
  using JobList = fbl::SinglyLinkedList<fbl::RefPtr<JobDispatcher>, ListTraits>;

  // Access to the pointers in these lists, especially any promotions to
  // RefPtr, must be handled very carefully, because the children can die
  // even when |lock_| is held. See ForEachChildInLocked() for more details
  // and for a safe way to enumerate them.
  RawJobList jobs_ TA_GUARDED(get_lock());
  RawProcessList procs_ TA_GUARDED(get_lock());

  JobPolicy policy_ TA_GUARDED(get_lock());

  fbl::RefPtr<ExceptionPort> exception_port_ TA_GUARDED(get_lock());
  fbl::RefPtr<ExceptionPort> debugger_exception_port_ TA_GUARDED(get_lock());
  Exceptionate exceptionate_;
  Exceptionate debug_exceptionate_;
};

// Returns the job that is the ancestor of all other tasks.
fbl::RefPtr<JobDispatcher> GetRootJobDispatcher();

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_JOB_DISPATCHER_H_
