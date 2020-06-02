// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/exception.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/vmar.h>
#include <string.h>
#include <zircon/syscalls/exception.h>

#include <utility>

#include <test-utils/test-utils.h>
#include <zxtest/zxtest.h>

namespace {

// Helper class to start and crash a process to test tu_exception APIs.
class TestProcess {
 public:
  void Init() {
    ASSERT_OK(zx::process::create(*zx::job::default_job(), "test_p", strlen("test_p"), 0, &process_,
                                  &vmar_));
    ASSERT_OK(zx::thread::create(process_, "test_t", strlen("test_t"), 0, &thread_));
  }

  const zx::process& process() const { return process_; }
  const zx::thread& thread() const { return thread_; }

  // Starts the process and immediately crashes the thread, filling |info|
  // and |exception|.
  void CrashAndGetException(const zx::channel& exception_channel, zx_exception_info_t* out_info,
                            zx::exception* out_exception) {
    // Starting a thread with 0 sp/pc crashes it immediately.
    zx::event event;
    ASSERT_OK(zx::event::create(0, &event));
    ASSERT_OK(process_.start(thread_, 0, 0, std::move(event), 0));

    tu_channel_wait_readable(exception_channel.get());

    zx::exception exception;
    zx_exception_info_t info;
    uint32_t num_bytes = sizeof(info);
    uint32_t num_handles = 1;
    zx_status_t status =
        zx_channel_read(exception_channel.get(), 0, &info, exception.reset_and_get_address(),
                        num_bytes, num_handles, nullptr, nullptr);
    ASSERT_EQ(status, ZX_OK);

    // Sanity check, make sure this is our exception.
    zx_info_handle_basic_t basic_info;
    status =
        process_.get_info(ZX_INFO_HANDLE_BASIC, &basic_info, sizeof(basic_info), nullptr, nullptr);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(basic_info.koid, info.pid);
    status =
        thread_.get_info(ZX_INFO_HANDLE_BASIC, &basic_info, sizeof(basic_info), nullptr, nullptr);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(basic_info.koid, info.tid);

    *out_info = info;
    *out_exception = std::move(exception);
  }

 private:
  zx::process process_;
  zx::vmar vmar_;
  zx::thread thread_;
};

TEST(TestUtils, ThreadException) {
  TestProcess test_process;
  ASSERT_NO_FATAL_FAILURES(test_process.Init());

  zx::channel exception_channel;
  ASSERT_OK(test_process.thread().create_exception_channel(0, &exception_channel));

  zx_exception_info_t info;
  zx::exception exception;
  ASSERT_NO_FATAL_FAILURES(test_process.CrashAndGetException(exception_channel, &info, &exception));

  EXPECT_EQ(ZX_EXCP_FATAL_PAGE_FAULT, info.type);

  // Thread exceptions can retrieve the thread handle but not the process.
  zx::process process;
  zx::thread exception_thread;
  ASSERT_OK(exception.get_thread(&exception_thread));
  zx_info_handle_basic_t basic_info;
  zx_status_t status = test_process.thread().get_info(ZX_INFO_HANDLE_BASIC, &basic_info,
                                                      sizeof(basic_info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);
  zx_koid_t test_koid = basic_info.koid;
  status = exception_thread.get_info(ZX_INFO_HANDLE_BASIC, &basic_info, sizeof(basic_info), nullptr,
                                     nullptr);
  ASSERT_EQ(status, ZX_OK);
  zx_koid_t exception_koid = basic_info.koid;
  EXPECT_EQ(test_koid, exception_koid);
  EXPECT_NOT_OK(exception.get_process(&process));

  // Kill the process before the exception closes or else it will bubble up
  // to the system crash handler.
  EXPECT_OK(test_process.process().kill());
}

TEST(TestUtils, ProcessDebugException) {
  TestProcess test_process;
  ASSERT_NO_FATAL_FAILURES(test_process.Init());
  zx::channel exception_channel;
  ASSERT_OK(test_process.process().create_exception_channel(ZX_EXCEPTION_CHANNEL_DEBUGGER,
                                                            &exception_channel));

  zx_exception_info_t info;
  zx::exception exception;
  ASSERT_NO_FATAL_FAILURES(test_process.CrashAndGetException(exception_channel, &info, &exception));

  // Make sure the DEBUGGER flag got passed through correctly - if it was, we
  // should get a THREAD_STARTING exception instead of a crash.
  EXPECT_EQ(ZX_EXCP_THREAD_STARTING, info.type);

  // Process exceptions can retrieve both the thread and process handles.
  zx::thread exception_thread;
  ASSERT_OK(exception.get_thread(&exception_thread));
  zx_info_handle_basic_t basic_info;
  zx_status_t status = test_process.thread().get_info(ZX_INFO_HANDLE_BASIC, &basic_info,
                                                      sizeof(basic_info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);
  zx_koid_t test_koid = basic_info.koid;
  status = exception_thread.get_info(ZX_INFO_HANDLE_BASIC, &basic_info, sizeof(basic_info), nullptr,
                                     nullptr);
  ASSERT_EQ(status, ZX_OK);
  zx_koid_t exception_koid = basic_info.koid;
  EXPECT_EQ(test_koid, exception_koid);

  zx::process exception_process;
  ASSERT_OK(exception.get_process(&exception_process));
  status = test_process.process().get_info(ZX_INFO_HANDLE_BASIC, &basic_info, sizeof(basic_info),
                                           nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);
  zx_koid_t test_process_koid = basic_info.koid;
  status = exception_process.get_info(ZX_INFO_HANDLE_BASIC, &basic_info, sizeof(basic_info),
                                      nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);
  zx_koid_t exception_process_koid = basic_info.koid;
  EXPECT_EQ(test_process_koid, exception_process_koid);

  EXPECT_OK(test_process.process().kill());
}

}  // namespace
