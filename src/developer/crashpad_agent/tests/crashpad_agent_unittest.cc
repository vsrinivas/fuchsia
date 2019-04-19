// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/crashpad_agent/crashpad_agent.h"

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl/cpp/binding_set.h>
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
#include "src/developer/crashpad_agent/crash_server.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/logging.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace fuchsia {
namespace crash {
namespace {

using fuchsia::feedback::Annotation;
using fuchsia::feedback::Attachment;
using fuchsia::feedback::DataProvider;
using fuchsia::feedback::DataProvider_GetData_Response;
using fuchsia::feedback::DataProvider_GetData_Result;

// We keep the local Crashpad database size under a certain value. As we want to
// check the produced attachments in the database, we should set the size to be
// at least the total size for a single report so that it does not get cleaned
// up before we are able to inspect its attachments.
// For now, a single report should take up to 1MB.
constexpr uint64_t kMaxTotalReportSizeInKb = 1024u;

const char kStubCrashServerUrl[] = "localhost:1234";

constexpr bool alwaysReturnSuccess = true;
constexpr bool alwaysReturnFailure = false;

Annotation BuildAnnotation(const std::string& key) {
  Annotation annotation;
  annotation.key = key;
  annotation.value = "unused";
  return annotation;
}

Attachment BuildAttachment(const std::string& key) {
  Attachment attachment;
  attachment.key = key;
  FXL_CHECK(fsl::VmoFromString("unused", &attachment.value));
  return attachment;
}

// Stub fuchsia.feedback.DataProvider service that returns canned responses for
// DataProvider::GetData().
class StubFeedbackDataProvider : public DataProvider {
 public:
  // Returns a request handler for binding to this stub service.
  // We pass a dispatcher to run it on a different loop than the agent.
  fidl::InterfaceRequestHandler<DataProvider> GetHandler(
      async_dispatcher_t* dispatcher) {
    return bindings_.GetHandler(this, dispatcher);
  }

  // DataProvider methods.
  void GetData(GetDataCallback callback) override {
    DataProvider_GetData_Result result;

    if (!next_annotation_keys_ && !next_attachment_keys_) {
      result.set_err(ZX_ERR_INTERNAL);
    } else {
      DataProvider_GetData_Response response;

      if (next_annotation_keys_) {
        std::vector<Annotation> annotations;
        for (const auto& key : *next_annotation_keys_.get()) {
          annotations.push_back(BuildAnnotation(key));
        }
        response.data.set_annotations(annotations);
        reset_annotation_keys();
      }

      if (next_attachment_keys_) {
        std::vector<Attachment> attachments;
        for (const auto& key : *next_attachment_keys_.get()) {
          attachments.push_back(BuildAttachment(key));
        }
        response.data.set_attachments(std::move(attachments));
        reset_attachment_keys();
      }

      result.set_response(std::move(response));
    }

    callback(std::move(result));
  }
  void GetScreenshot(fuchsia::feedback::ImageEncoding encoding,
                     GetScreenshotCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  // Stub injection methods.
  void set_annotation_keys(const std::vector<std::string>& annotation_keys) {
    next_annotation_keys_ =
        std::make_unique<std::vector<std::string>>(annotation_keys);
  }
  void set_attachment_keys(const std::vector<std::string>& attachment_keys) {
    next_attachment_keys_ =
        std::make_unique<std::vector<std::string>>(attachment_keys);
  }
  void reset_annotation_keys() { next_annotation_keys_.reset(); }
  void reset_attachment_keys() { next_attachment_keys_.reset(); }

 private:
  fidl::BindingSet<fuchsia::feedback::DataProvider> bindings_;
  std::unique_ptr<std::vector<std::string>> next_annotation_keys_;
  std::unique_ptr<std::vector<std::string>> next_attachment_keys_;
};

class StubCrashServer : public CrashServer {
 public:
  StubCrashServer(bool request_return_value)
      : CrashServer(kStubCrashServerUrl),
        request_return_value_(request_return_value) {}

  bool MakeRequest(const crashpad::HTTPHeaders& headers,
                   std::unique_ptr<crashpad::HTTPBodyStream> stream,
                   std::string* server_report_id) override {
    // TODO(frousseau): check this is the one written in the local Crashpad
    // database.
    *server_report_id = "untestedRepordId";
    return request_return_value_;
  }

 private:
  const bool request_return_value_;
};

// Unit-tests the implementation of the fuchsia.crash.Analyzer FIDL interface.
//
// This does not test the environment service. It directly instantiates the
// class, without connecting through FIDL.
class CrashpadAgentTest : public gtest::RealLoopFixture {
 public:
  CrashpadAgentTest()
      : service_directory_provider_loop_(&kAsyncLoopConfigNoAttachToThread),
        service_directory_provider_(
            service_directory_provider_loop_.dispatcher()) {
    // We run the service directory provider in a different loop so that it can
    // serve the requests to and responses from the stub feedback data provider
    // as the agent connects it synchronously.
    FXL_CHECK(service_directory_provider_loop_.StartThread(
                  "service directory provider thread") == ZX_OK);
  }

  ~CrashpadAgentTest() { service_directory_provider_loop_.Shutdown(); }

  void SetUp() override {
    stub_feedback_data_provider_.reset(new StubFeedbackDataProvider());
    FXL_CHECK(service_directory_provider_.AddService(
                  stub_feedback_data_provider_->GetHandler(
                      service_directory_provider_loop_.dispatcher())) == ZX_OK);

    // The underlying agent is initialized with a default config, but can
    // be reset via ResetAgent() if a different config is necessary.
    ResetAgent(Config{/*local_crashpad_database_path=*/database_path_.path(),
                      /*max_crashpad_database_size_in_kb=*/
                      kMaxTotalReportSizeInKb,
                      /*enable_upload_to_crash_server=*/true,
                      /*crash_server_url=*/
                      std::make_unique<std::string>(kStubCrashServerUrl)},
               std::make_unique<StubCrashServer>(alwaysReturnSuccess));
  }

 protected:
  // Resets the underlying agent using the given |config| and |crash_server|.
  void ResetAgent(Config config,
                  std::unique_ptr<StubCrashServer> crash_server) {
    FXL_CHECK(config.enable_upload_to_crash_server ^ !crash_server);
    crash_server_ = std::move(crash_server);

    ResetFeedbackDataProvider(
        // TODO(frousseau): check the annotations just like the attachments.
        // This is trickier because they are stored in the minidump at best.
        /*annotation.keys=*/{"unused.annotation.1", "unused.annotation.2"},
        /*attachment.keys=*/{"build.snapshot", "log.kernel"});

    // "attachments" should be kept in sync with the value defined in
    // //crashpad/client/crash_report_database_generic.cc
    attachments_dir_ =
        files::JoinPath(config.local_crashpad_database_path, "attachments");
    agent_ = CrashpadAgent::TryCreate(
        service_directory_provider_.service_directory(), std::move(config),
        std::move(crash_server_));
    FXL_CHECK(agent_);
  }

  // Resets the underlying agent using the given |config|.
  void ResetAgent(Config config) {
    FXL_CHECK(!config.enable_upload_to_crash_server);
    return ResetAgent(std::move(config), /*crash_server=*/nullptr);
  }

  // Resets the annotations and attachments returned by the underlying stub
  // feedback data provider.
  void ResetFeedbackDataProvider(
      const std::vector<std::string>& annotation_keys,
      const std::vector<std::string>& attachment_keys) {
    stub_feedback_data_provider_->set_annotation_keys(annotation_keys);
    stub_feedback_data_provider_->set_attachment_keys(attachment_keys);
    feedback_attachment_keys_ = attachment_keys;
  }

  // Tells the stub feedback::DataProvider to return no annotation or
  // attachment.
  void FlushFeedbackDataAnnotationKeys() {
    stub_feedback_data_provider_->reset_annotation_keys();
  }
  void FlushFeedbackDataAttachmentKeys() {
    stub_feedback_data_provider_->reset_attachment_keys();
    feedback_attachment_keys_.clear();
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
    expected_attachments.insert(expected_attachments.begin(),
                                feedback_attachment_keys_.begin(),
                                feedback_attachment_keys_.end());

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
    agent_->OnKernelPanicCrashLog(
        std::move(crash_log),
        [&out_result](Analyzer_OnKernelPanicCrashLog_Result result) {
          out_result = std::move(result);
        });
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

  std::unique_ptr<CrashpadAgent> agent_;
  files::ScopedTempDir database_path_;
  std::unique_ptr<StubCrashServer> crash_server_;

 private:
  void RemoveCurrentDirectory(std::vector<std::string>* dirs) {
    dirs->erase(std::remove(dirs->begin(), dirs->end(), "."), dirs->end());
  }

  async::Loop service_directory_provider_loop_;
  ::sys::testing::ServiceDirectoryProvider service_directory_provider_;
  std::unique_ptr<StubFeedbackDataProvider> stub_feedback_data_provider_;
  std::string attachments_dir_;
  std::vector<std::string> feedback_attachment_keys_;
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
  Analyzer_OnNativeException_Result out_result;
  agent_->OnNativeException(
      std::move(process), std::move(thread), std::move(exception_port),
      [&out_result](Analyzer_OnNativeException_Result result) {
        out_result = std::move(result);
      });
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
  GenericException exception = {};
  const std::string type = "FileSystemException";
  std::copy(type.begin(), type.end(), exception.type.data());
  const std::string message = "cannot open file";
  std::copy(message.begin(), message.end(), exception.message.data());
  ASSERT_TRUE(fsl::VmoFromString("#0", &exception.stack_trace));
  ManagedRuntimeException dart_exception;
  dart_exception.set_dart(std::move(exception));

  Analyzer_OnManagedRuntimeException_Result out_result;
  agent_->OnManagedRuntimeException(
      "component_url", std::move(dart_exception),
      [&out_result](Analyzer_OnManagedRuntimeException_Result result) {
        out_result = std::move(result);
      });
  EXPECT_TRUE(out_result.is_response());
  CheckAttachments({"DartError"});
}

TEST_F(CrashpadAgentTest, OnManagedRuntimeException_UnknownLanguage_Basic) {
  UnknownException exception;
  ASSERT_TRUE(fsl::VmoFromString("#0", &exception.data));
  ManagedRuntimeException unknown_exception;
  unknown_exception.set_unknown_(std::move(exception));

  Analyzer_OnManagedRuntimeException_Result out_result;
  agent_->OnManagedRuntimeException(
      "component_url", std::move(unknown_exception),
      [&out_result](Analyzer_OnManagedRuntimeException_Result result) {
        out_result = std::move(result);
      });
  EXPECT_TRUE(out_result.is_response());
  CheckAttachments({"data"});
}

TEST_F(CrashpadAgentTest, OnKernelPanicCrashLog_Basic) {
  fuchsia::mem::Buffer crash_log;
  ASSERT_TRUE(fsl::VmoFromString("ZIRCON KERNEL PANIC", &crash_log));
  Analyzer_OnKernelPanicCrashLog_Result out_result;
  agent_->OnKernelPanicCrashLog(
      std::move(crash_log),
      [&out_result](Analyzer_OnKernelPanicCrashLog_Result result) {
        out_result = std::move(result);
      });
  EXPECT_TRUE(out_result.is_response());
  CheckAttachments({"kernel_panic_crash_log"});
}

TEST_F(CrashpadAgentTest, PruneDatabase_ZeroSize) {
  // We reset the agent with a max database size of 0, meaning reports will
  // get cleaned up before the end of the |agent_| call.
  ResetAgent(Config{/*local_crashpad_database_path=*/database_path_.path(),
                    /*max_crashpad_database_size_in_kb=*/0u,
                    /*enable_upload_to_crash_server=*/false,
                    /*crash_server_url=*/nullptr});

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
  // We reset the agent with a max database size equivalent to the expected
  // size of a report plus the value of an especially large attachment.
  const uint64_t crash_log_size_in_kb = 2u * kMaxTotalReportSizeInKb;
  const std::string large_string = GenerateString(crash_log_size_in_kb);
  ResetAgent(
      Config{/*local_crashpad_database_path=*/database_path_.path(),
             /*max_crashpad_database_size_in_kb=*/kMaxTotalReportSizeInKb +
                 crash_log_size_in_kb,
             /*enable_upload_to_crash_server=*/false,
             /*crash_server_url=*/nullptr});

  // We generate a first crash report.
  EXPECT_TRUE(RunOneCrashAnalysis(large_string).is_response());

  // We check that only one set of attachments is there.
  const std::vector<std::string> attachment_subdirs = GetAttachmentSubdirs();
  ASSERT_EQ(attachment_subdirs.size(), 1u);

  // We sleep for one second to guarantee a different creation time for the
  // next crash report.
  zx_nanosleep(zx_deadline_after(ZX_SEC(1)));

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
  ResetAgent(Config{/*local_crashpad_database_path=*/database_path_.path(),
                    /*max_crashpad_database_size_in_kb=*/
                    kMaxTotalReportSizeInKb,
                    /*enable_upload_to_crash_server=*/true,
                    /*crash_server_url=*/
                    std::make_unique<std::string>(kStubCrashServerUrl)},
             std::make_unique<StubCrashServer>(alwaysReturnFailure));

  EXPECT_TRUE(RunOneCrashAnalysis().is_err());
}

TEST_F(CrashpadAgentTest, AnalysisSucceedOnNoUpload) {
  ResetAgent(Config{/*local_crashpad_database_path=*/database_path_.path(),
                    /*max_crashpad_database_size_in_kb=*/
                    kMaxTotalReportSizeInKb,
                    /*enable_upload_to_crash_server=*/false,
                    /*crash_server_url=*/nullptr});

  EXPECT_TRUE(RunOneCrashAnalysis().is_response());
}

TEST_F(CrashpadAgentTest, AnalysisSucceedOnNoFeedbackAttachments) {
  FlushFeedbackDataAttachmentKeys();
  EXPECT_TRUE(RunOneCrashAnalysis().is_response());
  CheckAttachments({"kernel_panic_crash_log"});
}

TEST_F(CrashpadAgentTest, AnalysisSucceedOnNoFeedbackAnnotations) {
  FlushFeedbackDataAnnotationKeys();
  EXPECT_TRUE(RunOneCrashAnalysis().is_response());
}

TEST_F(CrashpadAgentTest, AnalysisSucceedOnNoFeedbackData) {
  FlushFeedbackDataAnnotationKeys();
  FlushFeedbackDataAttachmentKeys();
  EXPECT_TRUE(RunOneCrashAnalysis().is_response());
  CheckAttachments({"kernel_panic_crash_log"});
}

}  // namespace
}  // namespace crash
}  // namespace fuchsia

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"crash", "test"});
  return RUN_ALL_TESTS();
}
