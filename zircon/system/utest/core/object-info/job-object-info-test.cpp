// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <fbl/vector.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/syscalls/object.h>

#include <zxtest/zxtest.h>

#include "helper.h"

namespace object_info_test {
namespace {

// ZX_INFO_JOB_PROCESS/ZX_INFO_JOB_CHILDREN tests
constexpr size_t kChildProcs = 3;
constexpr size_t kChildJobs = 2;
constexpr char kChildrenName[] = "child";
constexpr char kGrandChildrenName[] = "grandchild";

// Sets out_job to point towards the singleton instance of a job:
// - returned job
//   - child process 1
//   - child process 2
//   - child process 3 (kChildProcs)
//   - child job 1
//     - grandchild process 1.1
//     - grandchild job 1.1
//   - child job 2 (kChildJobs)
//     - grandchild process 2.1
//     - grandchild job 2.1
class JobFixutre : public zxtest::Test {
public:
    static void SetUpTestCase() {
        ASSERT_OK(zx::job::create(*zx::job::default_job(), 0, &root_), "Failed to create job.");

        for (size_t i = 0; i < kChildProcs; ++i) {
            zx::process process;
            zx::vmar vmar;
            ASSERT_OK(zx::process::create(root_, kChildrenName,
                                          fbl::constexpr_strlen(kChildrenName), 0, &process, &vmar),
                      "Failed to create %zu child", i);
            child_processes_.push_back(std::move(process));
            vmar_.push_back(std::move(vmar));
        }

        for (size_t i = 0; i < kChildJobs; ++i) {
            zx::job job;
            ASSERT_OK(zx::job::create(root_, 0, &job), "Failed to create %zu job", i);

            zx::process process;
            zx::vmar vmar;

            ASSERT_OK(zx::process::create(job, kGrandChildrenName,
                                          fbl::constexpr_strlen(kGrandChildrenName), 0, &process,
                                          &vmar),
                      "Failed to create %zu process grandchild", i);

            zx::job subjob;
            ASSERT_OK(zx::job::create(job, 0, &subjob), "Failed to create grandchild %zu job", i);
            child_jobs_.push_back(std::move(job));
            child_processes_.push_back(std::move(process));
            vmar_.push_back(std::move(vmar));
            child_jobs_.push_back(std::move(subjob));
        }
        ASSERT_TRUE(root_.is_valid());
    }

    // Clean up job handles.
    static void TearDownTestCase() {
        vmar_.reset();
        for (auto& vmar : vmar_) {
            vmar.destroy();
        }

        child_processes_.reset();
        for (auto& proc : child_processes_) {
            proc.kill();
        }

        for (auto& job : child_jobs_) {
            job.kill();
        }

        child_jobs_.reset();

        root_.kill();
        root_.reset();
    }

    const zx::job& GetJob() { return root_; }

    const auto& GetHandleProvider() const { return handle_provider; }

private:
    static zx::job root_;

    static fbl::Vector<zx::vmar> vmar_;
    static fbl::Vector<zx::process> child_processes_;
    static fbl::Vector<zx::job> child_jobs_;

    fbl::Function<const zx::job&()> handle_provider = [this]() -> const zx::job& {
        return GetJob();
    };
};

zx::job JobFixutre::root_;
fbl::Vector<zx::vmar> JobFixutre::vmar_;
fbl::Vector<zx::process> JobFixutre::child_processes_;
fbl::Vector<zx::job> JobFixutre::child_jobs_;

constexpr int kChildCount = 32;

// The jobch_helper_* (job child helper) functions allow testing both
// ZX_INFO_JOB_PROCESS and ZX_INFO_JOB_CHILDREN.
void CheckJobGetChild(const zx::job* job, uint32_t topic, size_t object_count,
                      size_t expected_count) {

    zx_koid_t koids[object_count];
    size_t actual;
    size_t available;

    ASSERT_OK(job->get_info(topic, koids, sizeof(zx_koid_t) * object_count, &actual, &available));

    EXPECT_EQ(expected_count, actual);
    EXPECT_EQ(expected_count, available);

    // All returned koids should produce a valid handle when passed to
    // zx_object_get_child.
    for (size_t i = 0; i < actual; i++) {
        zx::job child;
        EXPECT_OK(job->get_child(koids[i], ZX_RIGHT_SAME_RIGHTS, &child), "koid %zu", koids[i]);
    }
}

using JobGetInfoTest = JobFixutre;

constexpr auto process_provider = []() -> const zx::process& {
    static const zx::unowned_process process = zx::process::self();
    return *process;
};

constexpr auto job_provider = []() -> const zx::job& {
    const static zx::unowned_job job = zx::job::default_job();
    return *job;
};

constexpr auto thread_provider = []() -> const zx::thread& {
    const static zx::unowned_thread thread = zx::thread::self();
    return *thread;
};

TEST_F(JobGetInfoTest, InfoJobProcessesGetChild) {
    ASSERT_NO_FATAL_FAILURES(
        CheckJobGetChild(&GetJob(), ZX_INFO_JOB_PROCESSES, kChildCount, kChildProcs));
}

TEST_F(JobGetInfoTest, InfoJobChildJobsGetChild) {
    ASSERT_NO_FATAL_FAILURES(
        CheckJobGetChild(&GetJob(), ZX_INFO_JOB_CHILDREN, kChildCount, kChildJobs));
}

TEST_F(JobGetInfoTest, InfoJobProcessesOnSelfSuceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckSelfInfoSuceeds<zx_koid_t>(ZX_INFO_JOB_PROCESSES, 1, job_provider())));
}

TEST_F(JobGetInfoTest, InfoJobProcessesInvalidHandleFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckInvalidHandleFails<zx_koid_t>(ZX_INFO_JOB_PROCESSES, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobProcessesNullAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullAvailSuceeds<zx_koid_t>(ZX_INFO_JOB_PROCESSES, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobProcessesNullActualSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_koid_t>(ZX_INFO_JOB_PROCESSES, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobProcessesNullActualAndAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualAndAvailSuceeds<zx_koid_t>(ZX_INFO_JOB_PROCESSES, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobProcessesInvalidBufferPointerFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_koid_t>(ZX_INFO_JOB_PROCESSES, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobProcessesBadActualgIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_koid_t>(ZX_INFO_JOB_PROCESSES, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobProcessesBadAvailIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_koid_t>(ZX_INFO_JOB_PROCESSES, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobProcessesZeroSizedBufferIsOk) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckZeroSizeBufferSucceeds<zx_koid_t>(ZX_INFO_JOB_PROCESSES, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobProcessesSmallBufferIsOk) {
    // We use only one entry count, because we know that the process created at the fixture has more
    // mappings.
    ASSERT_NO_FATAL_FAILURES(
        (CheckSmallBufferSucceeds<zx_koid_t>(ZX_INFO_JOB_PROCESSES, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobProcessesPartiallyUnmappedBufferIsInvalidArgs) {
    ASSERT_NO_FATAL_FAILURES((CheckPartiallyUnmappedBufferIsInvalidArgs<zx_koid_t>(
        ZX_INFO_JOB_PROCESSES, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobProcessesRequiresEnumerateRights) {
    ASSERT_NO_FATAL_FAILURES(CheckMissingRightsFail<zx_koid_t>(
        ZX_INFO_JOB_PROCESSES, 32, ZX_RIGHT_ENUMERATE, GetHandleProvider()));
}

TEST_F(JobGetInfoTest, InfoJobProcessesProcessbHandleIsBadHandle) {
    ASSERT_NO_FATAL_FAILURES(
        CheckWrongHandleTypeFails<zx_koid_t>(ZX_INFO_JOB_PROCESSES, 32, process_provider));
}

TEST_F(JobGetInfoTest, InfoJobProcessesThreadHandleIsBadHandle) {
    ASSERT_NO_FATAL_FAILURES(
        CheckWrongHandleTypeFails<zx_koid_t>(ZX_INFO_JOB_PROCESSES, 32, thread_provider));
}

TEST_F(JobGetInfoTest, InfoJobChildrenOnSelfSuceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckSelfInfoSuceeds<zx_koid_t>(ZX_INFO_JOB_CHILDREN, 1, job_provider())));
}

TEST_F(JobGetInfoTest, InfoJobChildrenInvalidHandleFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckInvalidHandleFails<zx_koid_t>(ZX_INFO_JOB_CHILDREN, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobChildrenNullAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullAvailSuceeds<zx_koid_t>(ZX_INFO_JOB_CHILDREN, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobChildrenNullActualSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_koid_t>(ZX_INFO_JOB_CHILDREN, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobChildrenNullActualAndAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualAndAvailSuceeds<zx_koid_t>(ZX_INFO_JOB_CHILDREN, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobChildrenInvalidBufferPointerFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_koid_t>(ZX_INFO_JOB_CHILDREN, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobChildrenBadActualgIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_koid_t>(ZX_INFO_JOB_CHILDREN, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobChildrenBadAvailIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_koid_t>(ZX_INFO_JOB_CHILDREN, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobChildrenZeroSizedBufferIsOk) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckZeroSizeBufferSucceeds<zx_koid_t>(ZX_INFO_JOB_CHILDREN, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobChildrenSmallBufferIsOk) {
    // We use only one entry count, because we know that the process created at the fixture has more
    // mappings.
    ASSERT_NO_FATAL_FAILURES(
        (CheckSmallBufferSucceeds<zx_koid_t>(ZX_INFO_JOB_CHILDREN, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobChildrenPartiallyUnmappedBufferIsInvalidArgs) {
    ASSERT_NO_FATAL_FAILURES((CheckPartiallyUnmappedBufferIsInvalidArgs<zx_koid_t>(
        ZX_INFO_JOB_CHILDREN, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoJobChildrenRequiresEnumerateRights) {
    ASSERT_NO_FATAL_FAILURES(CheckMissingRightsFail<zx_koid_t>(
        ZX_INFO_JOB_CHILDREN, 32, ZX_RIGHT_ENUMERATE, GetHandleProvider()));
}

TEST_F(JobGetInfoTest, InfoJobChildrenJobHandleIsBadHandle) {
    ASSERT_NO_FATAL_FAILURES(
        CheckWrongHandleTypeFails<zx_koid_t>(ZX_INFO_JOB_CHILDREN, 32, process_provider));
}

TEST_F(JobGetInfoTest, InfoJobChildrenThreadHandleIsBadHandle) {
    ASSERT_NO_FATAL_FAILURES(
        CheckWrongHandleTypeFails<zx_koid_t>(ZX_INFO_JOB_CHILDREN, 32, thread_provider));
}

TEST_F(JobGetInfoTest, InfoHandleBasicOnSelfSuceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckSelfInfoSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1, job_provider())));
}

TEST_F(JobGetInfoTest, InfoHandleBasicInvalidHandleFails) {
    ASSERT_NO_FATAL_FAILURES((CheckInvalidHandleFails<zx_info_handle_basic_t>(
        ZX_INFO_HANDLE_BASIC, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoHandleBasicNullAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES((CheckNullAvailSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1,
                                                                            GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoHandleBasicNullActualSucceeds) {
    ASSERT_NO_FATAL_FAILURES((CheckNullActualSuceeds<zx_info_handle_basic_t>(
        ZX_INFO_HANDLE_BASIC, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoHandleBasicNullActualAndAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES((CheckNullActualAndAvailSuceeds<zx_info_handle_basic_t>(
        ZX_INFO_HANDLE_BASIC, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoHandleBasicInvalidBufferPointerFails) {
    ASSERT_NO_FATAL_FAILURES((CheckNullActualSuceeds<zx_info_handle_basic_t>(
        ZX_INFO_HANDLE_BASIC, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoHandleBasicBadActualgIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES((CheckNullActualSuceeds<zx_info_handle_basic_t>(
        ZX_INFO_HANDLE_BASIC, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoHandleBasicBadAvailIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES((CheckNullActualSuceeds<zx_info_handle_basic_t>(
        ZX_INFO_HANDLE_BASIC, 1, GetHandleProvider())));
}

TEST_F(JobGetInfoTest, InfoHandleBasicZeroSizedBufferFails) {
    ASSERT_NO_FATAL_FAILURES((CheckZeroSizeBufferFails<zx_info_handle_basic_t>(
        ZX_INFO_HANDLE_BASIC, GetHandleProvider())));
}

} // namespace
} // namespace object_info_test
