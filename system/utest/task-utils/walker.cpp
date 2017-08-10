// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <task-utils/walker.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <unittest/unittest.h>

namespace {

bool is_valid_handle(mx_handle_t handle) {
    return mx_object_get_info(
               handle, MX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr) ==
           MX_OK;
}

// TestTaskEnumerator ctor flags.
static constexpr unsigned int HAS_ON_JOB = 1u << 0;
static constexpr unsigned int HAS_ON_PROCESS = 1u << 1;
static constexpr unsigned int HAS_ON_THREAD = 1u << 2;

// An enumerator that does basic validation and allows for turning on and off
// job/process/thread callbacks.
class TestTaskEnumerator : public TaskEnumerator {
public:
    // |flags| is a bitmask of HAS_ON_* values indicating the values that
    // corresponding has_on_*() methods should return.
    TestTaskEnumerator(unsigned int flags)
        : flags_(flags) {}

    // Checks postconditions, marking the current test as failed
    // if any are not met.
    virtual void Validate() const {
        if (has_on_job()) {
            EXPECT_GT(jobs_seen_, 0);
        } else {
            EXPECT_EQ(jobs_seen_, 0);
        }
        if (has_on_process()) {
            EXPECT_GT(processes_seen_, 0);
        } else {
            EXPECT_EQ(processes_seen_, 0);
        }
        if (has_on_thread()) {
            EXPECT_GT(threads_seen_, 0);
        } else {
            EXPECT_EQ(threads_seen_, 0);
        }
    }

protected:
    virtual mx_status_t OnJob(int depth, mx_handle_t job, mx_koid_t koid,
                              mx_koid_t parent_koid) override {
        EXPECT_TRUE(has_on_job());
        EXPECT_GE(depth, 0);
        EXPECT_TRUE(is_valid_handle(job));
        EXPECT_NE(koid, 0);
        if (depth == 0) {
            EXPECT_EQ(parent_koid, 0, "root job");
        } else {
            EXPECT_NE(parent_koid, 0, "non-root job");
        }
        jobs_seen_++;
        return MX_OK;
    }
    virtual mx_status_t OnProcess(int depth, mx_handle_t process,
                                  mx_koid_t koid,
                                  mx_koid_t parent_koid) override {
        EXPECT_TRUE(has_on_process());
        EXPECT_GT(depth, 0, "process depth should always be > 0");
        EXPECT_TRUE(is_valid_handle(process));
        EXPECT_NE(koid, 0);
        EXPECT_NE(parent_koid, 0);
        processes_seen_++;
        return MX_OK;
    }
    virtual mx_status_t OnThread(int depth, mx_handle_t thread,
                                 mx_koid_t koid,
                                 mx_koid_t parent_koid) override {
        EXPECT_TRUE(has_on_thread());
        EXPECT_GT(depth, 1, "thread depth should always be > 1");
        EXPECT_TRUE(is_valid_handle(thread));
        EXPECT_NE(koid, 0);
        EXPECT_NE(parent_koid, 0);
        threads_seen_++;
        return MX_OK;
    }

private:
    bool has_on_job() const final { return flags_ & HAS_ON_JOB; }
    bool has_on_process() const final { return flags_ & HAS_ON_PROCESS; }
    bool has_on_thread() const final { return flags_ & HAS_ON_THREAD; }

    const unsigned int flags_;

    int jobs_seen_ = 0;
    int processes_seen_ = 0;
    int threads_seen_ = 0;
};

template <unsigned int Flags>
bool basic_cpp_walk() {
    BEGIN_TEST;
    TestTaskEnumerator tte(Flags);
    // TODO(dbort): Build a job tree just for the test and walk that instead;
    // same for other tests in this file. utest/core/object-info and
    // utest/policy (and maybe more) already do their own test job-tree
    // building; create a common helper lib.
    EXPECT_EQ(tte.WalkRootJobTree(), MX_OK);
    tte.Validate();
    END_TEST;
}

// A subclass of TestTaskEnumerator that will return a non-MX_OK status
// at some point, demonstrating that the walk stops and the status value
// is passed to the caller.
class FailingTaskEnumerator : public TestTaskEnumerator {
public:
    FailingTaskEnumerator(unsigned int flags, int poison_depth)
        : TestTaskEnumerator(flags), poison_depth_(poison_depth) {}

    // An unusual error code not used by the base class.
    static constexpr mx_status_t FailingStatus = MX_ERR_STOP;

private:
    // Not worth calling since the walk will stop before completing.
    void Validate() const override {
        EXPECT_TRUE(false);
    }

    mx_status_t OnJob(int depth, mx_handle_t job,
                      mx_koid_t koid, mx_koid_t parent_koid) override {
        EXPECT_FALSE(poisoned_);
        return MaybePoison(
            depth,
            TestTaskEnumerator::OnJob(depth, job, koid, parent_koid));
    }
    mx_status_t OnProcess(int depth, mx_handle_t process,
                          mx_koid_t koid, mx_koid_t parent_koid) override {
        EXPECT_FALSE(poisoned_);
        return MaybePoison(
            depth,
            TestTaskEnumerator::OnProcess(depth, process, koid, parent_koid));
    }
    mx_status_t OnThread(int depth, mx_handle_t thread,
                         mx_koid_t koid, mx_koid_t parent_koid) override {
        EXPECT_FALSE(poisoned_);
        return MaybePoison(
            depth,
            TestTaskEnumerator::OnThread(depth, thread, koid, parent_koid));
    }

    mx_status_t MaybePoison(int depth, mx_status_t s) {
        if (s == MX_OK && depth >= poison_depth_) {
            poisoned_ = true;
            return FailingStatus;
        }
        return s;
    }

    const int poison_depth_;

    bool poisoned_ = false;
};

template <unsigned int Flags, int PoisonDepth>
bool cpp_walk_failure() {
    BEGIN_TEST;
    FailingTaskEnumerator fte(Flags, PoisonDepth);
    EXPECT_EQ(fte.WalkRootJobTree(), FailingTaskEnumerator::FailingStatus);
    END_TEST;
}

} // namespace

// NOTE: Since the C++ API is built on top of the C API, this provides decent
// coverage for the C API without testing it directly.
BEGIN_TEST_CASE(task_utils)

RUN_TEST(basic_cpp_walk<0>)
RUN_TEST(basic_cpp_walk<HAS_ON_JOB>)
RUN_TEST(basic_cpp_walk<HAS_ON_JOB | HAS_ON_PROCESS>)
RUN_TEST(basic_cpp_walk<HAS_ON_JOB | HAS_ON_THREAD>)
RUN_TEST(basic_cpp_walk<HAS_ON_JOB | HAS_ON_PROCESS | HAS_ON_THREAD>)
RUN_TEST(basic_cpp_walk<HAS_ON_PROCESS>)
RUN_TEST(basic_cpp_walk<HAS_ON_PROCESS | HAS_ON_THREAD>)
RUN_TEST(basic_cpp_walk<HAS_ON_THREAD>)

// The callback on the root job happens on a different code path than other job
// depths, so test it explicitly.
RUN_TEST((cpp_walk_failure<HAS_ON_JOB, /*PoisonDepth=*/0>))
// A minimal system doesn't have jobs deeper than depth 1.
// TODO(dbort): Use depth 2 or more for all types once we have a test job
// hierarchy instead of the root job.
RUN_TEST((cpp_walk_failure<HAS_ON_JOB, /*PoisonDepth=*/1>))
RUN_TEST((cpp_walk_failure<HAS_ON_PROCESS, /*PoisonDepth=*/2>))
RUN_TEST((cpp_walk_failure<HAS_ON_THREAD, /*PoisonDepth=*/2>))

END_TEST_CASE(task_utils)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
