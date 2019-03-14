// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/crashpad/crashpad_analyzer_impl.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fdio/spawn.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/strings/concatenate.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/job.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/errors.h>

#include "garnet/bin/crashpad/config.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace fuchsia {
namespace crash {
namespace {

// Unit-tests the implementation of the fuchsia.crash.Analyzer FIDL interface.
//
// This does not test the environment service. It directly instantiates the
// class, without connecting through FIDL.
class CrashpadAnalyzerImplTest : public ::testing::Test {
 public:
  // The underlying crash analyzer is initialized with a default config, but can
  // be reset via ResetAnalyzer() if a different config is necessary.
  void SetUp() override {
    ResetAnalyzer(Config{/*local_crashpad_database_path=*/database_path_.path(),
                         /*enable_upload_to_crash_server=*/false,
                         /*crash_server_url=*/nullptr});
  }

 protected:
  // Resets the underlying crash analyzer using the given |config|.
  void ResetAnalyzer(Config config) {
    // "attachments" should be kept in sync with the value defined in
    // //crashpad/client/crash_report_database_generic.cc
    attachments_dir_ =
        files::JoinPath(config.local_crashpad_database_path, "attachments");
    analyzer_ = CrashpadAnalyzerImpl::TryCreate(std::move(config));
    FXL_DCHECK(analyzer_);
  }

  // Checks that there is:
  //   * only one set of attachments
  //   * the set of attachment filenames match the |expected_attachments|
  //   * no attachment is empty
  // in the local Crashpad database.
  void CheckAttachments(const std::vector<std::string>& expected_attachments) {
    const std::vector<std::string> subdirs = GetAttachmentSubdirs();
    // We expect a single crash report to have been generated.
    ASSERT_EQ(subdirs.size(), 1u);

    std::vector<std::string> attachments;
    const std::string report_attachments_dir =
        files::JoinPath(attachments_dir_, subdirs[0]);
    ASSERT_TRUE(files::ReadDirContents(report_attachments_dir, &attachments));
    RemoveCurrentDirectory(&attachments);
    EXPECT_THAT(attachments,
                testing::UnorderedElementsAreArray(expected_attachments));
    for (const std::string& attachment : attachments) {
      uint64_t size;
      ASSERT_TRUE(files::GetFileSize(
          files::JoinPath(report_attachments_dir, attachment), &size));
      EXPECT_GT(size, 0u) << "attachment file '" << attachment
                          << "' shouldn't be empty";
    }
  }

  // Returns all the attachment subdirectories under the over-arching attachment
  // directory. Each subdirectory corresponds to one local crash report.
  std::vector<std::string> GetAttachmentSubdirs() {
    std::vector<std::string> subdirs;
    FXL_CHECK(files::ReadDirContents(attachments_dir_, &subdirs));
    RemoveCurrentDirectory(&subdirs);
    return subdirs;
  }

  std::unique_ptr<CrashpadAnalyzerImpl> analyzer_;
  files::ScopedTempDir database_path_;

 private:
  void RemoveCurrentDirectory(std::vector<std::string>* dirs) {
    dirs->erase(std::remove(dirs->begin(), dirs->end(), "."), dirs->end());
  }

  std::string attachments_dir_;
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
                           "/pkg/bin/crasher_exe", argv, nullptr, 0, nullptr,
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
  CheckAttachments({"build.snapshot", "kernel_log"});

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
  CheckAttachments({"build.snapshot", "DartError"});
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
  CheckAttachments({"build.snapshot", "DartError"});
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
  CheckAttachments({"build.snapshot", "stack_trace"});
}

TEST_F(CrashpadAnalyzerImplTest, ProcessKernelPanicCrashlog_Basic) {
  fuchsia::mem::Buffer crashlog;
  ASSERT_TRUE(fsl::VmoFromString("ZIRCON KERNEL PANIC", &crashlog));
  zx_status_t out_status = ZX_ERR_UNAVAILABLE;
  analyzer_->ProcessKernelPanicCrashlog(
      std::move(crashlog),
      [&out_status](zx_status_t status) { out_status = status; });
  EXPECT_EQ(out_status, ZX_OK);
  CheckAttachments({"build.snapshot", "log"});
}

}  // namespace
}  // namespace crash
}  // namespace fuchsia

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"crash", "test"});
  return RUN_ALL_TESTS();
}
