// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/crashpad_agent/crashpad_agent.h"

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fdio/spawn.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/job.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <stdint.h>
#include <zircon/errors.h>
#include <zircon/time.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "src/developer/crashpad_agent/config.h"
#include "src/developer/crashpad_agent/tests/stub_crash_server.h"
#include "src/developer/crashpad_agent/tests/stub_feedback_data_provider.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/test/test_settings.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace fuchsia {
namespace crash {
namespace {

// We keep the local Crashpad database size under a certain value. As we want to
// check the produced attachments in the database, we should set the size to be
// at least the total size for a single report so that it does not get cleaned
// up before we are able to inspect its attachments.
// For now, a single report should take up to 1MB.
constexpr uint64_t kMaxTotalReportSizeInKb = 1024u;

// A full second should be enough for the stub feedback data provider to return
// its result.
constexpr uint64_t kFeedbackDataCollectionTimeoutInMillisecondsKey = 1000u;

constexpr bool alwaysReturnSuccess = true;
constexpr bool alwaysReturnFailure = false;

// Unit-tests the implementation of the fuchsia.crash.Analyzer FIDL interface.
//
// This does not test the environment service. It directly instantiates the
// class, without connecting through FIDL.
class CrashpadAgentTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    // The underlying agent is initialized with a default config, but can
    // be reset via ResetAgent() if a different config is necessary.
    ResetAgent(Config{/*local_crashpad_database_path=*/database_path_.path(),
                      /*max_crashpad_database_size_in_kb=*/
                      kMaxTotalReportSizeInKb,
                      /*enable_upload_to_crash_server=*/true,
                      /*crash_server_url=*/
                      std::make_unique<std::string>(kStubCrashServerUrl),
                      /*feedback_data_collection_timeout_in_milliseconds=*/
                      kFeedbackDataCollectionTimeoutInMillisecondsKey},
               std::make_unique<StubCrashServer>(alwaysReturnSuccess));
  }

 protected:
  // Resets the underlying agent using the given |config| and |crash_server|.
  void ResetAgent(Config config,
                  std::unique_ptr<StubCrashServer> crash_server) {
    FXL_CHECK(config.enable_upload_to_crash_server ^ !crash_server);
    crash_server_ = std::move(crash_server);

    // "attachments" should be kept in sync with the value defined in
    // //crashpad/client/crash_report_database_generic.cc
    attachments_dir_ =
        files::JoinPath(config.local_crashpad_database_path, "attachments");
    agent_ = CrashpadAgent::TryCreate(
        dispatcher(), service_directory_provider_.service_directory(),
        std::move(config), std::move(crash_server_));
    FXL_CHECK(agent_);
  }

  // Resets the underlying agent using the given |config|.
  void ResetAgent(Config config) {
    FXL_CHECK(!config.enable_upload_to_crash_server);
    return ResetAgent(std::move(config), /*crash_server=*/nullptr);
  }

  // Resets the underlying stub feedback data provider and registers it in the
  // |service_directory_provider_|.
  //
  // This can only be done once per test as ServiceDirectoryProvider does not
  // allow overridding a service. Hence why it is not in the SetUp().
  void ResetFeedbackDataProvider(
      std::unique_ptr<StubFeedbackDataProvider> stub_feedback_data_provider) {
    stub_feedback_data_provider_ = std::move(stub_feedback_data_provider);
    if (stub_feedback_data_provider_) {
      FXL_CHECK(service_directory_provider_.AddService(
                    stub_feedback_data_provider_->GetHandler()) == ZX_OK);
    }
  }

  // Checks that there is:
  //   * only one set of attachments
  //   * the set of attachment filenames matches the concatenation of
  //   |expected_extra_attachments| and feedback_attachment_keys_
  //   * no attachment is empty
  // in the local Crashpad database.
  void CheckAttachments(
      const std::vector<std::string>& expected_extra_attachments = {}) {
    const std::vector<std::string> subdirs = GetAttachmentSubdirs();
    // We expect a single crash report to have been generated.
    ASSERT_EQ(subdirs.size(), 1u);

    // We expect as attachments the ones returned by the feedback::DataProvider
    // and the extra ones specific to the crash analysis flow under test.
    std::vector<std::string> expected_attachments = expected_extra_attachments;
    if (stub_feedback_data_provider_) {
      expected_attachments.insert(
          expected_attachments.begin(),
          stub_feedback_data_provider_->attachment_keys().begin(),
          stub_feedback_data_provider_->attachment_keys().end());
    }

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

  // Runs one crash analysis. Useful to test shared logic among all crash
  // analysis flows.
  //
  // |attachment| allows to control the lower bound of the size of the report.
  //
  // Today we use the kernel panic flow because it requires fewer arguments to
  // set up.
  Analyzer_OnKernelPanicCrashLog_Result RunOneCrashAnalysis(
      const std::string& attachment) {
    fuchsia::mem::Buffer crash_log;
    FXL_CHECK(fsl::VmoFromString(attachment, &crash_log));

    Analyzer_OnKernelPanicCrashLog_Result out_result;
    bool has_out_result = false;
    agent_->OnKernelPanicCrashLog(
        std::move(crash_log),
        [&out_result,
         &has_out_result](Analyzer_OnKernelPanicCrashLog_Result result) {
          out_result = std::move(result);
          has_out_result = true;
        });
    RunLoopUntil([&has_out_result] { return has_out_result; });
    return out_result;
  }

  // Runs one crash analysis. Useful to test shared logic among all crash
  // analysis flows.
  //
  // Today we use the kernel panic flow because it requires fewer arguments to
  // set up.
  Analyzer_OnKernelPanicCrashLog_Result RunOneCrashAnalysis() {
    return RunOneCrashAnalysis("irrelevant, just not empty");
  }

  uint64_t total_num_feedback_data_provider_bindings() {
    if (!stub_feedback_data_provider_) {
      return 0u;
    }
    return stub_feedback_data_provider_->total_num_bindings();
  }
  size_t current_num_feedback_data_provider_bindings() {
    if (!stub_feedback_data_provider_) {
      return 0u;
    }
    return stub_feedback_data_provider_->current_num_bindings();
  }

  std::unique_ptr<CrashpadAgent> agent_;
  files::ScopedTempDir database_path_;
  std::unique_ptr<StubCrashServer> crash_server_;

 private:
  void RemoveCurrentDirectory(std::vector<std::string>* dirs) {
    dirs->erase(std::remove(dirs->begin(), dirs->end(), "."), dirs->end());
  }

  ::sys::testing::ServiceDirectoryProvider service_directory_provider_;
  std::unique_ptr<StubFeedbackDataProvider> stub_feedback_data_provider_;
  std::string attachments_dir_;
};

TEST_F(CrashpadAgentTest, OnNativeException_C_Basic) {
  // We create a parent job and a child job. The child job will spawn the
  // crashing program and analyze the crash. The parent job is just here to
  // swallow the exception potentially bubbling up from the child job once the
  // exception has been handled by the test agent (today this is the case as the
  // Crashpad exception handler RESUME_TRY_NEXTs the thread).
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

  // Wait up to 1s for the exception to be thrown. We need the process and
  // thread to be blocked in the exception for Crashpad to analyze them.
  zx_port_packet_t packet;
  ASSERT_EQ(exception_port.wait(zx::deadline_after(zx::sec(1)), &packet),
            ZX_OK);
  ASSERT_TRUE(ZX_PKT_IS_EXCEPTION(packet.type));

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
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());

  Analyzer_OnNativeException_Result out_result;
  bool has_out_result = false;
  agent_->OnNativeException(
      std::move(process), std::move(thread), std::move(exception_port),
      [&out_result, &has_out_result](Analyzer_OnNativeException_Result result) {
        out_result = std::move(result);
        has_out_result = true;
      });
  RunLoopUntil([&has_out_result] { return has_out_result; });

  EXPECT_TRUE(out_result.is_response());
  CheckAttachments();

  // The parent job just swallows the exception, i.e. not RESUME_TRY_NEXT it,
  // to not trigger the real agent attached to the root job.
  thread.resume_from_exception(
      parent_exception_port,
      0u /*no options to mark the exception as handled*/);

  // We kill the job so that it doesn't try to reschedule the process, which
  // would crash again, but this time would be handled by the real agent
  // attached to the root job as the exception has already been handled by the
  // parent and child jobs.
  job.kill();
}

TEST_F(CrashpadAgentTest, OnManagedRuntimeException_Dart_Basic) {
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  GenericException exception = {};
  const std::string type = "FileSystemException";
  std::copy(type.begin(), type.end(), exception.type.data());
  const std::string message = "cannot open file";
  std::copy(message.begin(), message.end(), exception.message.data());
  ASSERT_TRUE(fsl::VmoFromString("#0", &exception.stack_trace));
  ManagedRuntimeException dart_exception;
  dart_exception.set_dart(std::move(exception));

  Analyzer_OnManagedRuntimeException_Result out_result;
  bool has_out_result = false;
  agent_->OnManagedRuntimeException(
      "component_url", std::move(dart_exception),
      [&out_result,
       &has_out_result](Analyzer_OnManagedRuntimeException_Result result) {
        out_result = std::move(result);
        has_out_result = true;
      });
  RunLoopUntil([&has_out_result] { return has_out_result; });

  EXPECT_TRUE(out_result.is_response());
  CheckAttachments({"DartError"});
}

TEST_F(CrashpadAgentTest, OnManagedRuntimeException_UnknownLanguage_Basic) {
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  UnknownException exception;
  ASSERT_TRUE(fsl::VmoFromString("#0", &exception.data));
  ManagedRuntimeException unknown_exception;
  unknown_exception.set_unknown_(std::move(exception));

  Analyzer_OnManagedRuntimeException_Result out_result;
  bool has_out_result = false;
  agent_->OnManagedRuntimeException(
      "component_url", std::move(unknown_exception),
      [&out_result,
       &has_out_result](Analyzer_OnManagedRuntimeException_Result result) {
        out_result = std::move(result);
        has_out_result = true;
      });
  RunLoopUntil([&has_out_result] { return has_out_result; });

  EXPECT_TRUE(out_result.is_response());
  CheckAttachments({"data"});
}

TEST_F(CrashpadAgentTest, OnKernelPanicCrashLog_Basic) {
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  fuchsia::mem::Buffer crash_log;
  ASSERT_TRUE(fsl::VmoFromString("ZIRCON KERNEL PANIC", &crash_log));

  Analyzer_OnKernelPanicCrashLog_Result out_result;
  bool has_out_result = false;
  agent_->OnKernelPanicCrashLog(
      std::move(crash_log), [&out_result, &has_out_result](
                                Analyzer_OnKernelPanicCrashLog_Result result) {
        out_result = std::move(result);
        has_out_result = true;
      });
  RunLoopUntil([&has_out_result] { return has_out_result; });

  EXPECT_TRUE(out_result.is_response());
  CheckAttachments({"kernel_panic_crash_log"});
}

TEST_F(CrashpadAgentTest, PruneDatabase_ZeroSize) {
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  // We reset the agent with a max database size of 0, meaning reports will
  // get cleaned up before the end of the |agent_| call.
  ResetAgent(Config{/*local_crashpad_database_path=*/database_path_.path(),
                    /*max_crashpad_database_size_in_kb=*/0u,
                    /*enable_upload_to_crash_server=*/false,
                    /*crash_server_url=*/nullptr,
                    /*feedback_data_collection_timeout_in_milliseconds=*/
                    kFeedbackDataCollectionTimeoutInMillisecondsKey});

  // We generate a crash report.
  EXPECT_TRUE(RunOneCrashAnalysis().is_response());

  // We check that all the attachments have been cleaned up.
  EXPECT_TRUE(GetAttachmentSubdirs().empty());
}

std::string GenerateString(const uint64_t string_size_in_kb) {
  std::string str;
  for (size_t i = 0; i < string_size_in_kb * 1024; ++i) {
    str.push_back(static_cast<char>(i % 128));
  }
  return str;
}

TEST_F(CrashpadAgentTest, PruneDatabase_SizeForOneReport) {
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  // We reset the agent with a max database size equivalent to the expected
  // size of a report plus the value of an especially large attachment.
  const uint64_t crash_log_size_in_kb = 2u * kMaxTotalReportSizeInKb;
  const std::string large_string = GenerateString(crash_log_size_in_kb);
  ResetAgent(
      Config{/*local_crashpad_database_path=*/database_path_.path(),
             /*max_crashpad_database_size_in_kb=*/kMaxTotalReportSizeInKb +
                 crash_log_size_in_kb,
             /*enable_upload_to_crash_server=*/false,
             /*crash_server_url=*/nullptr,
             /*feedback_data_collection_timeout_in_milliseconds=*/
             kFeedbackDataCollectionTimeoutInMillisecondsKey});

  // We generate a first crash report.
  EXPECT_TRUE(RunOneCrashAnalysis(large_string).is_response());

  // We check that only one set of attachments is there.
  const std::vector<std::string> attachment_subdirs = GetAttachmentSubdirs();
  ASSERT_EQ(attachment_subdirs.size(), 1u);

  // We sleep for one second to guarantee a different creation time for the
  // next crash report.
  zx::nanosleep(zx::deadline_after(zx::sec(1)));

  // We generate a new crash report.
  EXPECT_TRUE(RunOneCrashAnalysis(large_string).is_response());

  // We check that only one set of attachments is there and that it is a
  // different directory than previously (the directory name is the local crash
  // report ID).
  const std::vector<std::string> new_attachment_subdirs =
      GetAttachmentSubdirs();
  EXPECT_EQ(new_attachment_subdirs.size(), 1u);
  EXPECT_THAT(
      new_attachment_subdirs,
      testing::Not(testing::UnorderedElementsAreArray(attachment_subdirs)));
}

TEST_F(CrashpadAgentTest, AnalysisFailOnFailedUpload) {
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  ResetAgent(Config{/*local_crashpad_database_path=*/database_path_.path(),
                    /*max_crashpad_database_size_in_kb=*/
                    kMaxTotalReportSizeInKb,
                    /*enable_upload_to_crash_server=*/true,
                    /*crash_server_url=*/
                    std::make_unique<std::string>(kStubCrashServerUrl),
                    /*feedback_data_collection_timeout_in_milliseconds=*/
                    kFeedbackDataCollectionTimeoutInMillisecondsKey},
             std::make_unique<StubCrashServer>(alwaysReturnFailure));

  EXPECT_TRUE(RunOneCrashAnalysis().is_err());
}

TEST_F(CrashpadAgentTest, AnalysisSucceedOnNoUpload) {
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  ResetAgent(Config{/*local_crashpad_database_path=*/database_path_.path(),
                    /*max_crashpad_database_size_in_kb=*/
                    kMaxTotalReportSizeInKb,
                    /*enable_upload_to_crash_server=*/false,
                    /*crash_server_url=*/nullptr,
                    /*feedback_data_collection_timeout_in_milliseconds=*/
                    kFeedbackDataCollectionTimeoutInMillisecondsKey});

  EXPECT_TRUE(RunOneCrashAnalysis().is_response());
}

TEST_F(CrashpadAgentTest, AnalysisSucceedOnNoFeedbackAttachments) {
  ResetFeedbackDataProvider(
      std::make_unique<StubFeedbackDataProviderReturnsNoAttachment>());
  EXPECT_TRUE(RunOneCrashAnalysis().is_response());
  // The only attachment should be the one from the crash analysis as no
  // feedback data attachments will be retrieved.
  CheckAttachments({"kernel_panic_crash_log"});
}

TEST_F(CrashpadAgentTest, AnalysisSucceedOnNoFeedbackAnnotations) {
  ResetFeedbackDataProvider(
      std::make_unique<StubFeedbackDataProviderReturnsNoAnnotation>());
  EXPECT_TRUE(RunOneCrashAnalysis().is_response());
}

TEST_F(CrashpadAgentTest, AnalysisSucceedOnNoFeedbackData) {
  ResetFeedbackDataProvider(
      std::make_unique<StubFeedbackDataProviderReturnsNoData>());
  EXPECT_TRUE(RunOneCrashAnalysis().is_response());
  // The only attachment should be the one from the crash analysis as no
  // feedback data will be retrieved.
  CheckAttachments({"kernel_panic_crash_log"});
}

TEST_F(CrashpadAgentTest, AnalysisSucceedOnNoFeedbackDataProvider) {
  // We pass a nullptr stub so there will be no fuchsia.feedback.DataProvider
  // service to connect to.
  ResetFeedbackDataProvider(nullptr);
  EXPECT_TRUE(RunOneCrashAnalysis().is_response());
  // The only attachment should be the one from the crash analysis as no
  // feedback data will be retrieved.
  CheckAttachments({"kernel_panic_crash_log"});
}

TEST_F(CrashpadAgentTest, AnalysisSucceedOnFeedbackDataProviderTakingTooLong) {
  ResetFeedbackDataProvider(
      std::make_unique<StubFeedbackDataProviderNeverReturning>());
  // We use a timeout of 1ms for the feedback data collection as the test will
  // need to wait that long before skipping feedback data collection.
  ResetAgent(Config{/*local_crashpad_database_path=*/database_path_.path(),
                    /*max_crashpad_database_size_in_kb=*/
                    kMaxTotalReportSizeInKb,
                    /*enable_upload_to_crash_server=*/true,
                    /*crash_server_url=*/
                    std::make_unique<std::string>(kStubCrashServerUrl),
                    /*feedback_data_collection_timeout_in_milliseconds=*/
                    1u},
             std::make_unique<StubCrashServer>(alwaysReturnSuccess));

  EXPECT_TRUE(RunOneCrashAnalysis().is_response());
  // The only attachment should be the one from the crash analysis as no
  // feedback data will be retrieved.
  CheckAttachments({"kernel_panic_crash_log"});
}

TEST_F(CrashpadAgentTest, OneFeedbackDataProviderConnectionPerAnalysis) {
  // We use a stub that returns no data as we are not interested in the
  // payload, just the number of different connections to the stub.
  ResetFeedbackDataProvider(
      std::make_unique<StubFeedbackDataProviderReturnsNoData>());

  const size_t num_calls = 5u;
  std::vector<Analyzer_OnKernelPanicCrashLog_Result> out_results;
  for (size_t i = 0; i < num_calls; i++) {
    fuchsia::mem::Buffer crash_log;
    FXL_CHECK(fsl::VmoFromString("irrelevant, just not empty", &crash_log));
    agent_->OnKernelPanicCrashLog(
        std::move(crash_log),
        [&out_results](Analyzer_OnKernelPanicCrashLog_Result result) {
          out_results.push_back(std::move(result));
        });
  }
  RunLoopUntil(
      [&out_results, num_calls] { return out_results.size() == num_calls; });

  EXPECT_EQ(total_num_feedback_data_provider_bindings(), num_calls);
  // The unbinding is asynchronous so we need to run the loop until all the
  // outstanding connections are actually closed in the stub.
  RunLoopUntil(
      [this] { return current_num_feedback_data_provider_bindings() == 0u; });
}

}  // namespace
}  // namespace crash
}  // namespace fuchsia

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"crash", "test"});
  return RUN_ALL_TESTS();
}
