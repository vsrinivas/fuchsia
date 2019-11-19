// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <object/job_dispatcher.h>

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
  root_job->EnumerateChildren(&job_enumerator, /*recurse=*/true);

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

}  // namespace

UNITTEST_START_TESTCASE(job_dispatcher_tests)
UNITTEST("JobDispatcherJobEnumerator", TestJobEnumerator)
UNITTEST_END_TESTCASE(job_dispatcher_tests, "job_dispatcher_tests", "JobDispatcher tests")
