// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <object/job_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/vm_address_region_dispatcher.h>

namespace {

static const size_t index_limit_ = {20};  // Arbitrary.

static bool TestJobEnumerator() {
  BEGIN_TEST;
  class TestJobEnumerator : public JobEnumerator {
   public:
    // These could be private, but this is a unit test, so they're public.
    bool badness_ = false;
    bool called_after_stop_ = false;
    size_t index_ = 0u;
    struct Entry {
      zx_koid_t koid;
      zx_koid_t parent_koid;
    } entries_[index_limit_];

    bool AddEntry(zx_koid_t koid, zx_koid_t parent_koid) {
      if (index_ >= index_limit_) {
        // When index_ >= index_limit_ a value of false is returned (below)
        // which means that no further OnJob or OnProcess calls should be made.
        // If this is called after that state, then the EnumerateChildren code
        // didn't honor the contract about halting on a `return false`.
        called_after_stop_ = true;
        return false;
      }
      entries_[index_].koid = koid;
      entries_[index_].parent_koid = parent_koid;
      ++index_;
      return index_ < index_limit_;
    }

    bool OnJob(JobDispatcher* job) override {
      if (job == nullptr) {
        badness_ = true;  // Very unexpected.
        return false;
      }
      if (job->parent() == nullptr) {
        badness_ = true;  // Very unexpected.
        return false;
      }
      return AddEntry(job->get_koid(), job->parent()->get_koid());
    }

    bool OnProcess(ProcessDispatcher* proc) override {
      if (proc == nullptr) {
        badness_ = true;  // Very unexpected.
        return false;
      }
      if (proc->job() == nullptr) {
        badness_ = true;  // Very unexpected.
        return false;
      }
      return AddEntry(proc->get_koid(), proc->job()->get_koid());
    }
  } job_enumerator;

  fbl::RefPtr<JobDispatcher> root_job = GetRootJobDispatcher();
  ASSERT_TRUE(root_job, "The root job is required.");
  // Enumerating the children will not add the root job itself. Add it explicitly.
  job_enumerator.AddEntry(root_job->get_koid(), ZX_KOID_INVALID);
  root_job->EnumerateChildrenRecursive(&job_enumerator);

  ASSERT_FALSE(job_enumerator.badness_, "A pointer was unexpectedly null.");
  ASSERT_FALSE(job_enumerator.called_after_stop_, "Return false didn't halt Enumeration.");

  // There should be at least one job.
  ASSERT_GT(job_enumerator.index_, 0u, "At least one job");

  // Check that all nodes have a path to the root node.
  for (size_t i = 1u; i < job_enumerator.index_; ++i) {
    bool found_root = false;
    zx_koid_t current = job_enumerator.entries_[i].parent_koid;
    // All the parents are expected to be in the list prior to the child.
    for (size_t k = i; k != 0; --k) {
      if (current == job_enumerator.entries_[k - 1].koid) {
        if (k == 1 && current == job_enumerator.entries_[0].koid) {
          // The last step should always be at index 0 (the root).
          found_root = true;
        }
        current = job_enumerator.entries_[k - 1].parent_koid;
      }
    }
    ASSERT_TRUE(found_root, "Find root");
  }

  END_TEST;
}

bool TestJobNoChildrenSignal() {
  BEGIN_TEST;

  // Create a new job.
  KernelHandle<JobDispatcher> root;
  zx_rights_t rights;
  ASSERT_EQ(JobDispatcher::Create(0, /*parent=*/GetRootJobDispatcher(), &root, &rights), ZX_OK);

  // Ensure all three NO_{JOBS,PROCESSES,CHILDREN} signals are active.
  EXPECT_EQ(root.dispatcher()->PollSignals(),
            ZX_JOB_NO_PROCESSES | ZX_JOB_NO_JOBS | ZX_JOB_NO_CHILDREN);

  // Create a child job.
  KernelHandle<JobDispatcher> child_job;
  ASSERT_EQ(JobDispatcher::Create(0, root.dispatcher(), &child_job, &rights), ZX_OK);

  // Ensure the NO_CHILDREN and NO_JOBS signals have cleared.
  EXPECT_EQ(root.dispatcher()->PollSignals(), ZX_JOB_NO_PROCESSES);

  // Create a child process.
  KernelHandle<ProcessDispatcher> child_process;
  KernelHandle<VmAddressRegionDispatcher> vmar;
  zx_rights_t process_rights;
  zx_rights_t vmar_rights;
  ASSERT_EQ(ProcessDispatcher::Create(root.dispatcher(), "test-process", /*flags=*/0u,
                                      &child_process, &process_rights, &vmar, &vmar_rights),
            ZX_OK);

  // Ensure the NO_PROCESS signal has cleared.
  EXPECT_EQ(root.dispatcher()->PollSignals(), 0u);

  // Kill the child job. Ensure NO_JOBS is active again.
  child_job.dispatcher()->Kill(0);
  EXPECT_EQ(root.dispatcher()->PollSignals(), ZX_JOB_NO_JOBS);

  // Kill the child process. Ensure all three signals are active again.
  child_process.dispatcher()->Kill(0);
  EXPECT_EQ(root.dispatcher()->PollSignals(),
            ZX_JOB_NO_PROCESSES | ZX_JOB_NO_JOBS | ZX_JOB_NO_CHILDREN);

  root.dispatcher()->Kill(0);
  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(job_dispatcher_tests)
UNITTEST("JobDispatcherJobEnumerator", TestJobEnumerator)
UNITTEST("JobNoChildrenSignal", TestJobNoChildrenSignal)
UNITTEST_END_TESTCASE(job_dispatcher_tests, "job_dispatcher_tests", "JobDispatcher tests")
