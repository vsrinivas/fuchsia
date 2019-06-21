// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cinttypes>
#include <climits>
#include <memory>
#include <utility>

#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/vmar.h>
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
    static const zx::unowned_thread thread = zx::thread::self();
    return *thread;
};

constexpr auto vmar_provider = []() -> const zx::vmar& {
    const static zx::unowned_vmar vmar = zx::vmar::root_self();
    return *vmar;
};

TEST(VmarGetInfoTest, InfoHandleBasicOnSelfSuceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckSelfInfoSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1, vmar_provider())));
}

TEST(VmarGetInfoTest, InfoHandleBasicInvalidHandleFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckInvalidHandleFails<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1, vmar_provider)));
}

TEST(VmarGetInfoTest, InfoHandleBasicNullAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullAvailSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1, vmar_provider)));
}

TEST(VmarGetInfoTest, InfoHandleBasicNullActualSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1, vmar_provider)));
}

TEST(VmarGetInfoTest, InfoHandleBasicNullActualAndAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES((CheckNullActualAndAvailSuceeds<zx_info_handle_basic_t>(
        ZX_INFO_HANDLE_BASIC, 1, vmar_provider)));
}

TEST(VmarGetInfoTest, InfoHandleBasicInvalidBufferPointerFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1, vmar_provider)));
}

TEST(VmarGetInfoTest, InfoHandleBasicBadActualgIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1, vmar_provider)));
}

TEST(VmarGetInfoTest, InfoHandleBasicBadAvailIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1, vmar_provider)));
}

TEST(VmarGetInfoTest, InfoHandleBasicZeroSizedBufferFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckZeroSizeBufferFails<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, vmar_provider)));
}

TEST(VmarGetInfoTest, InfoVmarOnSelfFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckSelfInfoSuceeds<zx_info_vmar_t>(ZX_INFO_VMAR, 1, vmar_provider())));
}

TEST(VmarGetInfoTest, InfoVmarInvalidHandleFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckInvalidHandleFails<zx_info_vmar_t>(ZX_INFO_VMAR, 1, vmar_provider)));
}

TEST(VmarGetInfoTest, InfoVmarNullAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullAvailSuceeds<zx_info_vmar_t>(ZX_INFO_VMAR, 1, vmar_provider)));
}

TEST(VmarGetInfoTest, InfoVmarNullActualSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_vmar_t>(ZX_INFO_VMAR, 1, vmar_provider)));
}

TEST(VmarGetInfoTest, InfoVmarNullActualAndAvailSucceeds) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualAndAvailSuceeds<zx_info_vmar_t>(ZX_INFO_VMAR, 1, vmar_provider)));
}

TEST(VmarGetInfoTest, InfoVmarInvalidBufferPointerFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_vmar_t>(ZX_INFO_VMAR, 1, vmar_provider)));
}

TEST(VmarGetInfoTest, InfoVmarBadActualgIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_vmar_t>(ZX_INFO_VMAR, 1, vmar_provider)));
}

TEST(VmarGetInfoTest, InfoVmarBadAvailIsInvalidArg) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckNullActualSuceeds<zx_info_vmar_t>(ZX_INFO_VMAR, 1, vmar_provider)));
}

TEST(VmarGetInfoTest, InfoVmarZeroSizedBufferFails) {
    ASSERT_NO_FATAL_FAILURES(
        (CheckZeroSizeBufferFails<zx_info_vmar_t>(ZX_INFO_VMAR, vmar_provider)));
}

TEST(VmarGetInfoTest, InfoVmarJobHandleIsBadHandle) {
    ASSERT_NO_FATAL_FAILURES(
        CheckWrongHandleTypeFails<zx_info_vmar_t>(ZX_INFO_VMAR, 32, job_provider));
}

TEST(VmarGetInfoTest, InfoVmarProcessHandleIsBadHandle) {
    ASSERT_NO_FATAL_FAILURES(
        CheckWrongHandleTypeFails<zx_info_vmar_t>(ZX_INFO_VMAR, 32, process_provider));
}

TEST(VmarGetInfoTest, InfoVmarThreadHandleIsBadHandle) {
    ASSERT_NO_FATAL_FAILURES(
        CheckWrongHandleTypeFails<zx_info_vmar_t>(ZX_INFO_VMAR, 32, thread_provider));
}

} // namespace
} // namespace object_info_test
