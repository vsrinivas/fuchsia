// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>

#include <zxtest/zxtest.h>

#include "helper.h"

namespace object_info_test {
namespace {

constexpr auto job_provider = []() -> const zx::job& {
    static const zx::unowned_job job = zx::job::default_job();
    return *job;
};

constexpr auto process_provider = []() -> const zx::process& {
    static const zx::unowned_process process = zx::process::self();
    return *process;
};

constexpr auto thread_provider = []() -> const zx::thread& {
    const static zx::unowned_thread thread = zx::thread::self();
    return *thread;
};

TEST(ThreadGetInfoTest, InfoHandleBasicOnSelfSuceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckSelfInfoSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1, thread_provider())));
}

TEST(ThreadGetInfoTest, InfoHandleBasicInvalidHandleFails) {
    ASSERT_NO_FATAL_FAILURES((
        CheckInvalidHandleFails<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoHandleBasicNullAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullAvailSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoHandleBasicNullActualSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoHandleBasicNullActualAndAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES((CheckNullActualAndAvailSuceeds<zx_info_handle_basic_t>(
        ZX_INFO_HANDLE_BASIC, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoHandleBasicInvalidBufferPointerFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoHandleBasicBadActualgIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoHandleBasicBadAvailIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoHandleBasicZeroSizedBufferFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckZeroSizeBufferFails<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoHandleCountOnSelfSuceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckSelfInfoSuceeds<zx_info_handle_count_t>(ZX_INFO_HANDLE_COUNT, 1, thread_provider())));
}

TEST(ThreadGetInfoTest, InfoHandleCountInvalidHandleFails) {
    ASSERT_NO_FATAL_FAILURES((
        CheckInvalidHandleFails<zx_info_handle_count_t>(ZX_INFO_HANDLE_COUNT, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoHandleCountNullAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullAvailSuceeds<zx_info_handle_count_t>(ZX_INFO_HANDLE_COUNT, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoHandleCountNullActualSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_handle_count_t>(ZX_INFO_HANDLE_COUNT, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoHandleCountNullActualAndAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES((CheckNullActualAndAvailSuceeds<zx_info_handle_count_t>(
        ZX_INFO_HANDLE_COUNT, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoHandleCountInvalidBufferPointerFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_handle_count_t>(ZX_INFO_HANDLE_COUNT, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoHandleCountBadActualgIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_handle_count_t>(ZX_INFO_HANDLE_COUNT, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoHandleCountBadAvailIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_handle_count_t>(ZX_INFO_HANDLE_COUNT, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoHandleCountZeroSizedBufferFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckZeroSizeBufferFails<zx_info_handle_count_t>(ZX_INFO_HANDLE_COUNT, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoThreadOnSelfSuceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckSelfInfoSuceeds<zx_info_thread_t>(ZX_INFO_THREAD, 1, thread_provider())));
}

TEST(ThreadGetInfoTest, InfoThreadInvalidHandleFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckInvalidHandleFails<zx_info_thread_t>(ZX_INFO_THREAD, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoThreadNullAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullAvailSuceeds<zx_info_thread_t>(ZX_INFO_THREAD, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoThreadNullActualSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_thread_t>(ZX_INFO_THREAD, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoThreadNullActualAndAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualAndAvailSuceeds<zx_info_thread_t>(ZX_INFO_THREAD, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoThreadInvalidBufferPointerFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_thread_t>(ZX_INFO_THREAD, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoThreadBadActualgIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_thread_t>(ZX_INFO_THREAD, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoThreadBadAvailIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_thread_t>(ZX_INFO_THREAD, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoThreadZeroSizedBufferFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckZeroSizeBufferFails<zx_info_thread_t>(ZX_INFO_THREAD, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoThreadJobHandleIsBadHandle) {
    ASSERT_NO_FATAL_FAILURES(
        CheckWrongHandleTypeFails<zx_info_thread_t>(ZX_INFO_THREAD, 32, job_provider));
}

TEST(ThreadGetInfoTest, InfoThreadProcessHandleIsBadHandle) {
    ASSERT_NO_FATAL_FAILURES(
        CheckWrongHandleTypeFails<zx_info_thread_t>(ZX_INFO_THREAD, 32, process_provider));
}

TEST(ThreadGetInfoTest, InfoThreadStatsOnSelfSuceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckSelfInfoSuceeds<zx_info_thread_stats_t>(ZX_INFO_THREAD_STATS, 1, thread_provider())));
}

TEST(ThreadGetInfoTest, InfoThreadStatsInvalidHandleFails) {
    ASSERT_NO_FATAL_FAILURES((
        CheckInvalidHandleFails<zx_info_thread_stats_t>(ZX_INFO_THREAD_STATS, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoThreadStatsNullAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullAvailSuceeds<zx_info_thread_stats_t>(ZX_INFO_THREAD_STATS, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoThreadStatsNullActualSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_thread_stats_t>(ZX_INFO_THREAD_STATS, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoThreadStatsNullActualAndAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES((CheckNullActualAndAvailSuceeds<zx_info_thread_stats_t>(
        ZX_INFO_THREAD_STATS, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoThreadStatsInvalidBufferPointerFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_thread_stats_t>(ZX_INFO_THREAD_STATS, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoThreadStatsBadActualgIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_thread_stats_t>(ZX_INFO_THREAD_STATS, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoThreadStatsBadAvailIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_thread_stats_t>(ZX_INFO_THREAD_STATS, 1, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoThreadStatsZeroSizedBufferFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckZeroSizeBufferFails<zx_info_thread_stats_t>(ZX_INFO_THREAD_STATS, thread_provider)));
}

TEST(ThreadGetInfoTest, InfoThreadStatsJobHandleIsBadHandle) {
    ASSERT_NO_FATAL_FAILURES(
        CheckWrongHandleTypeFails<zx_info_thread_stats_t>(ZX_INFO_THREAD_STATS, 32, job_provider));
}

TEST(ThreadGetInfoTest, InfoThreadStatsProcessHandleIsBadHandle) {
    ASSERT_NO_FATAL_FAILURES(CheckWrongHandleTypeFails<zx_info_thread_stats_t>(
        ZX_INFO_THREAD_STATS, 32, process_provider));
}

// As reference from previous object-info test.
// Skip most tests for ZX_INFO_THREAD_EXCEPTION_REPORT, which is tested
// elsewhere and requires the target thread to be in a certain state.
TEST(ThreadGetInfoTest, InfoThreadExceptionReportInvalidHandleFails) {
    ASSERT_NO_FATAL_FAILURES(CheckInvalidHandleFails<zx_exception_report_t>(
        ZX_INFO_THREAD_EXCEPTION_REPORT, 32, []() { return zx::handle(); }));
}

} // namespace
} // namespace object_info_test
