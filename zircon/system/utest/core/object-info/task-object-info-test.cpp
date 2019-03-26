// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
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

TEST(TaskGetInfoTest, InfoStatsUnstartedSuceeds) {
    static constexpr char kProcessName[] = "object-info-unstarted";

    zx::vmar vmar;
    zx::process process;

    ASSERT_OK(zx::process::create(*zx::job::default_job(), kProcessName, sizeof(kProcessName), 0u,
                                  &process, &vmar));

    zx_info_task_stats_t info;
    ASSERT_OK(process.get_info(ZX_INFO_TASK_STATS, &info, sizeof(info), nullptr, nullptr));
}

TEST(TaskGetInfoTest, InfoStatsSmokeTest) {
    zx_info_task_stats_t info;
    ASSERT_OK(
        zx::process::self()->get_info(ZX_INFO_TASK_STATS, &info, sizeof(info), nullptr, nullptr));

    EXPECT_GT(info.mem_private_bytes, 0u);
    EXPECT_GT(info.mem_shared_bytes, 0u);
    EXPECT_GE(info.mem_mapped_bytes, info.mem_private_bytes + info.mem_shared_bytes);
    EXPECT_GT(info.mem_scaled_shared_bytes, 0u);
    EXPECT_GT(info.mem_shared_bytes, info.mem_scaled_shared_bytes);
}

constexpr auto handle_provider = []() -> const zx::process& {
    const static zx::unowned_process process = zx::process::self();
    return *process;
};

TEST(TaskGetInfoTest, InfoTaskStatsInvalidHandleFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckInvalidHandleFails<zx_info_task_stats_t>(ZX_INFO_TASK_STATS, 1, handle_provider)));
}

TEST(TaskGetInfoTest, InfoTaskStatsNullAvailSucceeds) {
    ASSERT_TRUE(handle_provider().is_valid());
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullAvailSuceeds<zx_info_task_stats_t>(ZX_INFO_TASK_STATS, 1, handle_provider)));
}

TEST(TaskGetInfoTest, InfoTaskStatsNullActualSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_task_stats_t>(ZX_INFO_TASK_STATS, 1, handle_provider)));
}

TEST(TaskGetInfoTest, InfoTaskStatsNullActualAndAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES((CheckNullActualAndAvailSuceeds<zx_info_task_stats_t>(
        ZX_INFO_TASK_STATS, 1, handle_provider)));
}

TEST(TaskGetInfoTest, InfoTaskStatsInvalidBufferPointerFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_task_stats_t>(ZX_INFO_TASK_STATS, 1, handle_provider)));
}

TEST(TaskGetInfoTest, InfoTaskStatsBadActualgIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_task_stats_t>(ZX_INFO_TASK_STATS, 1, handle_provider)));
}

TEST(TaskGetInfoTest, InfoTaskStatsBadAvailIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_task_stats_t>(ZX_INFO_TASK_STATS, 1, handle_provider)));
}

TEST(TaskGetInfoTest, InfoTaskStatsZeroSizedBufferIsTooSmall) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckZeroSizeBufferFails<zx_info_task_stats_t>(ZX_INFO_TASK_STATS, handle_provider)));
}

constexpr auto job_provider = []() -> const zx::job& {
    const static zx::unowned_job job = zx::job::default_job();
    return *job;
};

TEST(TaskGetInfoTest, InfoTaskStatsJobHandleIsBadHandle) {
    ASSERT_NO_FATAL_FAILURES(
        CheckWrongHandleTypeFails<zx_info_task_stats_t>(ZX_INFO_TASK_STATS, 1, job_provider));
}

constexpr auto thread_provider = []() -> const zx::thread& {
    const static zx::unowned_thread thread = zx::thread::self();
    return *thread;
};

TEST(TaskGetInfoTest, InfoTaskStatsThreadHandleIsBadHandle) {
    ASSERT_NO_FATAL_FAILURES(
        CheckWrongHandleTypeFails<zx_info_task_stats_t>(ZX_INFO_TASK_STATS, 1, thread_provider));
}

} // namespace
} // namespace object_info_test
