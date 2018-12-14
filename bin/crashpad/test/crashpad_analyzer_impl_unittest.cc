// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/crashpad/crashpad_analyzer_impl.h"

#include <utility>

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <gtest/gtest.h>
#include <lib/fdio/spawn.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/files/scoped_temp_dir.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/job.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/errors.h>

namespace fuchsia {
namespace crash {
namespace {

class CrashpadAnalyzerImplTest : public ::testing::Test {
 public:
  void SetUp() override {
    analyzer_ = CrashpadAnalyzerImpl::TryCreate(database_path_.path());
  }

 protected:
  std::unique_ptr<CrashpadAnalyzerImpl> analyzer_;

 private:
  files::ScopedTempDir database_path_;
};

TEST_F(CrashpadAnalyzerImplTest, HandleNativeException_C_Basic) {
  // We create a parent job and a child job. The child job will spawn the
  // crashing program and analyze the crash. The parent job is just here to
  // swallow the exception potentially bubbling up from the child job once the
  // exception has been handled by the test crash analyzer (today this is the
  // case as the Crashpad exception handler RESUME_TRY_NEXTs the thread).
  zx::job parent_job;
  zx::port parent_exception_port;
  zx::job job;
  zx::port exception_port;
  zx::process process;
  zx::thread thread;

  // Create the child jobs of the current job now so we can bind to the
  // exception port before spawning the crashing program.
  zx::unowned_job current_job(zx_job_default());
  ASSERT_EQ(zx::job::create(*current_job, 0, &parent_job), ZX_OK);
  ASSERT_EQ(zx::port::create(0u, &parent_exception_port), ZX_OK);
  ASSERT_EQ(zx_task_bind_exception_port(parent_job.get(),
                                        parent_exception_port.get(), 0u, 0u),
            ZX_OK);
  ASSERT_EQ(zx::job::create(parent_job, 0, &job), ZX_OK);
  ASSERT_EQ(zx::port::create(0u, &exception_port), ZX_OK);
  ASSERT_EQ(
      zx_task_bind_exception_port(job.get(), exception_port.get(), 0u, 0u),
      ZX_OK);

  // Create child process using our utility program `crasher` that will crash on
  // startup.
  const char* argv[] = {"crasher", nullptr};
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  ASSERT_EQ(fdio_spawn_etc(job.get(), FDIO_SPAWN_CLONE_ALL,
                           "/system/bin/crasher", argv, nullptr, 0, nullptr,
                           process.reset_and_get_address(), err_msg),
            ZX_OK)
      << err_msg;

  // Get the one thread from the child process.
  zx_koid_t thread_ids[1];
  size_t num_ids;
  ASSERT_EQ(process.get_info(ZX_INFO_PROCESS_THREADS, thread_ids,
                             sizeof(zx_koid_t), &num_ids, nullptr),
            ZX_OK);
  ASSERT_EQ(num_ids, 1u);
  ASSERT_EQ(process.get_child(thread_ids[0], ZX_RIGHT_SAME_RIGHTS, &thread),
            ZX_OK);

  // Test crash analysis.
  zx_status_t out_status = ZX_ERR_UNAVAILABLE;
  analyzer_->HandleNativeException(
      std::move(process), std::move(thread), std::move(exception_port),
      [&out_status](zx_status_t status) { out_status = status; });
  EXPECT_EQ(out_status, ZX_OK);

  // The parent job just swallows the exception, i.e. not RESUME_TRY_NEXT it,
  // to not trigger the real crash analyzer attached to the root job.
  thread.resume_from_exception(
      parent_exception_port,
      0u /*no options to mark the exception as handled*/);

  // We kill the job so that it doesn't try to reschedule the process, which
  // would crash again, but this time would be handled by the real crash
  // analyzer attached to the root job as the exception has already been handled
  // by the parent and child jobs.
  job.kill();
}

TEST_F(CrashpadAnalyzerImplTest, HandleManagedRuntimeException_Dart_Basic) {
  fuchsia::mem::Buffer stack_trace;
  ASSERT_TRUE(fsl::VmoFromString("#0", &stack_trace));
  zx_status_t out_status = ZX_ERR_UNAVAILABLE;
  analyzer_->HandleManagedRuntimeException(
      ManagedRuntimeLanguage::DART, "component_url", "UnhandledException: Foo",
      std::move(stack_trace),
      [&out_status](zx_status_t status) { out_status = status; });
  EXPECT_EQ(out_status, ZX_OK);
}

TEST_F(CrashpadAnalyzerImplTest,
       HandleManagedRuntimeException_Dart_ExceptionStringInBadFormat) {
  fuchsia::mem::Buffer stack_trace;
  ASSERT_TRUE(fsl::VmoFromString("#0", &stack_trace));
  zx_status_t out_status = ZX_ERR_UNAVAILABLE;
  analyzer_->HandleManagedRuntimeException(
      ManagedRuntimeLanguage::DART, "component_url", "wrong format",
      std::move(stack_trace),
      [&out_status](zx_status_t status) { out_status = status; });
  EXPECT_EQ(out_status, ZX_OK);
}

TEST_F(CrashpadAnalyzerImplTest,
       HandleManagedRuntimeException_OtherLanguage_Basic) {
  fuchsia::mem::Buffer stack_trace;
  ASSERT_TRUE(fsl::VmoFromString("#0", &stack_trace));
  zx_status_t out_status = ZX_ERR_UNAVAILABLE;
  analyzer_->HandleManagedRuntimeException(
      ManagedRuntimeLanguage::OTHER_LANGUAGE, "component_url", "error",
      std::move(stack_trace),
      [&out_status](zx_status_t status) { out_status = status; });
  EXPECT_EQ(out_status, ZX_OK);
}

TEST_F(CrashpadAnalyzerImplTest, ProcessKernelPanicCrashlog_Basic) {
  fuchsia::mem::Buffer crashlog;
  ASSERT_TRUE(fsl::VmoFromString("ZIRCON KERNEL PANIC", &crashlog));
  zx_status_t out_status = ZX_ERR_UNAVAILABLE;
  analyzer_->ProcessKernelPanicCrashlog(
      std::move(crashlog),
      [&out_status](zx_status_t status) { out_status = status; });
  EXPECT_EQ(out_status, ZX_OK);
}

}  // namespace
}  // namespace crash
}  // namespace fuchsia

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"crash", "test"});
  return RUN_ALL_TESTS();
}
