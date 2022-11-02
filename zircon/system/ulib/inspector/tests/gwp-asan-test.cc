// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon/system/ulib/inspector/gwp-asan.h"

#include <lib/fdio/spawn.h>
#include <lib/fit/defer.h>
#include <lib/zx/exception.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <zircon/syscalls/exception.h>

#include <gwp_asan/common.h>
#include <zxtest/zxtest.h>

namespace inspector {

namespace {

constexpr const char* kHelperPath = "/pkg/bin/gwp-asan-test-helper";

void launch_proc_and_wait_for_exception(zx::job* test_job, zx::process* test_process,
                                        zx_exception_report_t* exception_report) {
  // Create a job and attach an exception channel.
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0, test_job));
  zx::channel exception_channel;
  ASSERT_OK(test_job->create_exception_channel(0, &exception_channel));

  // Spawn the helper process.
  const char* argv[] = {kHelperPath, nullptr};
  const char* envp[] = {
      "SCUDO_OPTIONS="
      "GWP_ASAN_Enabled=true:GWP_ASAN_SampleRate=1:GWP_ASAN_MaxSimultaneousAllocations=512",
      nullptr};

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  ASSERT_OK(fdio_spawn_etc(test_job->get(), FDIO_SPAWN_CLONE_ALL, kHelperPath, argv, envp, 0,
                           nullptr, test_process->reset_and_get_address(), err_msg),
            "%s", err_msg);

  // Wait for the helper to crash or the process to terminate.
  zx_wait_item_t wait_items[] = {
      {.handle = exception_channel.get(), .waitfor = ZX_CHANNEL_READABLE, .pending = 0},
      {.handle = test_process->get(), .waitfor = ZX_PROCESS_TERMINATED, .pending = 0},
  };
  ASSERT_OK(zx_object_wait_many(wait_items, 2, ZX_TIME_INFINITE));

  // The helper should crash and the exception channel should signal.
  ASSERT_TRUE(wait_items[0].pending & ZX_CHANNEL_READABLE);
  ASSERT_FALSE(wait_items[1].pending & ZX_PROCESS_TERMINATED);

  // Get the exception report.
  zx_exception_info_t exception_info;
  zx::exception exception;
  ASSERT_OK(exception_channel.read(0, &exception_info, exception.reset_and_get_address(),
                                   sizeof(exception_info), 1, nullptr, nullptr));
  zx::thread thread;
  ASSERT_OK(exception.get_thread(&thread));
  ASSERT_OK(thread.get_info(ZX_INFO_THREAD_EXCEPTION_REPORT, exception_report,
                            sizeof(zx_exception_report_t), nullptr, nullptr) != ZX_OK);
}

TEST(GwpAsanTest, GwpAsanException) {
  if constexpr (!HAS_GWP_ASAN) {
    return;
  }

  zx::job test_job;
  zx::process test_process;
  zx_exception_report_t exception_report;
  launch_proc_and_wait_for_exception(&test_job, &test_process, &exception_report);
  auto auto_call_kill_job = fit::defer([&test_job]() { test_job.kill(); });

  GwpAsanInfo info;
  ASSERT_TRUE(inspector_get_gwp_asan_info(test_process, exception_report, &info));
  ASSERT_EQ(gwp_asan::ErrorToString(gwp_asan::Error::USE_AFTER_FREE), info.error_type);
  ASSERT_GT(info.allocation_trace.size(), 3);
  ASSERT_GT(info.deallocation_trace.size(), 3);
}

// Tests that GWP-ASan ignores errors when the system runs out of memory.
TEST(GwpAsanTest, GwpAsanOOMException) {
  if constexpr (!HAS_GWP_ASAN) {
    return;
  }

  zx::job test_job;
  zx::process test_process;
  zx_exception_report_t exception_report;
  launch_proc_and_wait_for_exception(&test_job, &test_process, &exception_report);
  auto auto_call_kill_job = fit::defer([&test_job]() { test_job.kill(); });

  ASSERT_EQ(ZX_EXCP_FATAL_PAGE_FAULT, exception_report.header.type);

  // Set the report synth_code to ZX_ERR_NO_MEMORY.
  exception_report.context.synth_code = static_cast<uint32_t>(ZX_ERR_NO_MEMORY);

  GwpAsanInfo info;
  ASSERT_TRUE(inspector_get_gwp_asan_info(test_process, exception_report, &info));
  ASSERT_EQ(nullptr, info.error_type);
}

}  // namespace

}  // namespace inspector
