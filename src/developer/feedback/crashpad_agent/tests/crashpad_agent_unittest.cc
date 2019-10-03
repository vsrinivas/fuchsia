// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/crashpad_agent.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/timekeeper/test_clock.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sdk/lib/inspect/testing/cpp/inspect.h"
#include "src/developer/feedback/crashpad_agent/config.h"
#include "src/developer/feedback/crashpad_agent/constants.h"
#include "src/developer/feedback/crashpad_agent/settings.h"
#include "src/developer/feedback/crashpad_agent/tests/stub_crash_server.h"
#include "src/developer/feedback/crashpad_agent/tests/stub_feedback_data_provider.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/test/test_settings.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using fuchsia::feedback::Annotation;
using fuchsia::feedback::Attachment;
using fuchsia::feedback::CrashReport;
using fuchsia::feedback::GenericCrashReport;
using fuchsia::feedback::NativeCrashReport;
using fuchsia::feedback::RuntimeCrashReport;
using fuchsia::feedback::SpecificCrashReport;
using inspect::testing::ChildrenMatch;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;
using inspect::testing::UintIs;
using testing::Contains;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::Not;
using testing::UnorderedElementsAreArray;

// We keep the local Crashpad database size under a certain value. As we want to check the produced
// attachments in the database, we should set the size to be at least the total size for a single
// report so that it does not get cleaned up before we are able to inspect its attachments. For now,
// a single report should take up to 1MB.
constexpr uint64_t kMaxTotalReportSizeInKb = 1024u;

constexpr bool kUploadSuccessful = true;
constexpr bool kUploadFailed = false;

// "attachments" should be kept in sync with the value defined in
// //crashpad/client/crash_report_database_generic.cc
constexpr char kCrashpadAttachmentsDir[] = "attachments";
constexpr char kCrashpadUUIDString[] = "00000000-0000-0000-0000-000000000001";
constexpr char kProgramName[] = "crashing_program";

constexpr char kSingleAttachmentKey[] = "attachment.key";
constexpr char kSingleAttachmentValue[] = "attachment.value";

Attachment BuildAttachment(const std::string& key, const std::string& value) {
  Attachment attachment;
  attachment.key = key;
  FXL_CHECK(fsl::VmoFromString(value, &attachment.value));
  return attachment;
}

// Unit-tests the implementation of the fuchsia.feedback.CrashReporter FIDL interface.
//
// This does not test the environment service. It directly instantiates the class, without
// connecting through FIDL.
class CrashpadAgentTest : public gtest::TestLoopFixture {
 protected:
  // Resets the underlying agent using the given |config| and |crash_server|.
  void ResetAgent(Config config, std::unique_ptr<StubCrashServer> crash_server) {
    FXL_CHECK((config.crash_server.url && crash_server) ||
              (!config.crash_server.url && !crash_server));
    crash_server_ = crash_server.get();

    attachments_dir_ = files::JoinPath(config.crashpad_database.path, kCrashpadAttachmentsDir);
    inspector_ = std::make_unique<inspect::Inspector>();
    clock_ = std::make_unique<timekeeper::TestClock>();
    inspect_manager_ = std::make_unique<InspectManager>(&inspector_->GetRoot(), clock_.get());
    agent_ = CrashpadAgent::TryCreate(dispatcher(), service_directory_provider_.service_directory(),
                                      std::move(config), std::move(crash_server),
                                      inspect_manager_.get());
    FXL_CHECK(agent_);
  }

  // Resets the underlying agent using the given |config|.
  void ResetAgent(Config config) {
    FXL_CHECK(!config.crash_server.url);
    return ResetAgent(std::move(config), /*crash_server=*/nullptr);
  }

  void ResetAgentDefaultConfig(const std::vector<bool>& upload_attempt_results = {}) {
    ResetAgent(
        Config{/*crashpad_database=*/
               {
                   /*path=*/database_path_.path(),
                   /*max_size_in_kb=*/kMaxTotalReportSizeInKb,
               },
               /*crash_server=*/
               {
                   /*upload_policy=*/CrashServerConfig::UploadPolicy::ENABLED,
                   /*url=*/std::make_unique<std::string>(kStubCrashServerUrl),
               }},
        std::make_unique<StubCrashServer>(upload_attempt_results));
  }

  // Resets the underlying stub feedback data provider and registers it in the
  // |service_directory_provider_|.
  //
  // This can only be done once per test as ServiceDirectoryProvider does not allow overridding a
  // service. Hence why it is not in the SetUp().
  void ResetFeedbackDataProvider(std::unique_ptr<StubFeedbackDataProvider> feedback_data_provider) {
    feedback_data_provider_ = std::move(feedback_data_provider);
    if (feedback_data_provider_) {
      FXL_CHECK(service_directory_provider_.AddService(feedback_data_provider_->GetHandler()) ==
                ZX_OK);
    }
  }

  // Checks that in the local Crashpad database there is:
  //   * only one set of attachments
  //   * the set of attachment filenames matches the concatenation of
  //     |expected_extra_attachments| and feedback_data_provider_->attachment_bundle_key()
  //   * no attachment is empty
  void CheckAttachmentsInDatabase(const std::vector<std::string>& expected_extra_attachments = {}) {
    const std::vector<std::string> subdirs = GetAttachmentSubdirsInDatabase();
    // We expect a single crash report to have been generated.
    ASSERT_EQ(subdirs.size(), 1u);

    // We expect as attachments the ones returned by the feedback::DataProvider and the extra ones
    // specific to the crash analysis flow under test.
    std::vector<std::string> expected_attachments = expected_extra_attachments;
    if (feedback_data_provider_ && feedback_data_provider_->has_attachment_bundle_key()) {
      expected_attachments.push_back(feedback_data_provider_->attachment_bundle_key());
    }

    std::vector<std::string> attachments;
    const std::string report_attachments_dir = files::JoinPath(attachments_dir_, subdirs[0]);
    ASSERT_TRUE(files::ReadDirContents(report_attachments_dir, &attachments));
    RemoveCurrentDirectory(&attachments);
    EXPECT_THAT(attachments, UnorderedElementsAreArray(expected_attachments));
    for (const std::string& attachment : attachments) {
      uint64_t size;
      ASSERT_TRUE(files::GetFileSize(files::JoinPath(report_attachments_dir, attachment), &size));
      EXPECT_GT(size, 0u) << "attachment file '" << attachment << "' shouldn't be empty";
    }
  }

  // Checks that on the stub crash server the annotations received match the concatenation of:
  //   * |expected_extra_annotations|
  //   * feedback_data_provider_->annotations()
  //   * default annotations
  //
  // In case of duplicate keys, the value from |expected_extra_annotations| is picked.
  void CheckAnnotationsOnServer(
      const std::map<std::string, testing::Matcher<std::string>>& expected_extra_annotations = {}) {
    FXL_CHECK(crash_server_);

    std::map<std::string, testing::Matcher<std::string>> expected_annotations = {
        {"product", "Fuchsia"},
        {"version", Not(IsEmpty())},
        {"ptype", testing::StartsWith("crashing_program")},
        {"osName", "Fuchsia"},
        {"osVersion", "0.0.0"},
        {"should_process", "false"},
    };
    if (feedback_data_provider_) {
      for (const auto& [key, value] : feedback_data_provider_->annotations()) {
        expected_annotations[key] = value;
      }
    }
    for (const auto& [key, value] : expected_extra_annotations) {
      expected_annotations[key] = value;
    }

    EXPECT_EQ(crash_server_->latest_annotations().size(), expected_annotations.size());
    for (const auto& [key, value] : expected_annotations) {
      EXPECT_THAT(crash_server_->latest_annotations(),
                  testing::Contains(testing::Pair(key, value)));
    }
  }

  // Returns all the attachment subdirectories under the over-arching attachment directory in the
  // database.
  //
  // Each subdirectory corresponds to one local crash report.
  std::vector<std::string> GetAttachmentSubdirsInDatabase() {
    std::vector<std::string> subdirs;
    FXL_CHECK(files::ReadDirContents(attachments_dir_, &subdirs));
    RemoveCurrentDirectory(&subdirs);
    return subdirs;
  }

  // Files one crash report.
  fit::result<void, zx_status_t> FileOneCrashReport(CrashReport report) {
    FXL_CHECK(agent_ != nullptr) << "agent_ is nullptr. Call ResetAgent() or one of its variants "
                                    "at the beginning of a test case.";
    fit::result<void, zx_status_t> out_result;
    agent_->File(std::move(report), [&out_result](fit::result<void, zx_status_t> result) {
      out_result = std::move(result);
    });
    FXL_CHECK(RunLoopUntilIdle());
    return out_result;
  }

  // Files one crash report.
  fit::result<void, zx_status_t> FileOneCrashReport(const std::vector<Annotation>& annotations = {},
                                                    std::vector<Attachment> attachments = {}) {
    CrashReport report;
    report.set_program_name(kProgramName);
    if (!annotations.empty()) {
      report.set_annotations(annotations);
    }
    if (!attachments.empty()) {
      report.set_attachments(std::move(attachments));
    }
    return FileOneCrashReport(std::move(report));
  }

  // Files one crash report.
  //
  // |attachment| is useful to control the lower bound of the size of the report by controlling the
  // size of some of the attachment(s). This comes in handy when testing the database size limit
  // enforcement logic for instance.
  fit::result<void, zx_status_t> FileOneCrashReportWithSingleAttachment(
      const std::string& attachment = kSingleAttachmentValue) {
    std::vector<Attachment> attachments;
    attachments.emplace_back(BuildAttachment(kSingleAttachmentKey, attachment));
    return FileOneCrashReport(/*annotations=*/{},
                              /*attachments=*/std::move(attachments));
  }

  // Files one generic crash report.
  fit::result<void, zx_status_t> FileOneGenericCrashReport(
      const std::optional<std::string>& crash_signature) {
    GenericCrashReport generic_report;
    if (crash_signature.has_value()) {
      generic_report.set_crash_signature(crash_signature.value());
    }

    SpecificCrashReport specific_report;
    specific_report.set_generic(std::move(generic_report));

    CrashReport report;
    report.set_program_name("crashing_program_generic");
    report.set_specific_report(std::move(specific_report));

    return FileOneCrashReport(std::move(report));
  }

  // Files one native crash report.
  fit::result<void, zx_status_t> FileOneNativeCrashReport(
      std::optional<fuchsia::mem::Buffer> minidump) {
    NativeCrashReport native_report;
    if (minidump.has_value()) {
      native_report.set_minidump(std::move(minidump.value()));
    }

    SpecificCrashReport specific_report;
    specific_report.set_native(std::move(native_report));

    CrashReport report;
    report.set_program_name("crashing_program_native");
    report.set_specific_report(std::move(specific_report));

    return FileOneCrashReport(std::move(report));
  }

  // Files one Dart crash report.
  fit::result<void, zx_status_t> FileOneDartCrashReport(
      const std::optional<std::string>& exception_type,
      const std::optional<std::string>& exception_message,
      std::optional<fuchsia::mem::Buffer> exception_stack_trace) {
    RuntimeCrashReport dart_report;
    if (exception_type.has_value()) {
      dart_report.set_exception_type(exception_type.value());
    }
    if (exception_message.has_value()) {
      dart_report.set_exception_message(exception_message.value());
    }
    if (exception_stack_trace.has_value()) {
      dart_report.set_exception_stack_trace(std::move(exception_stack_trace.value()));
    }

    SpecificCrashReport specific_report;
    specific_report.set_dart(std::move(dart_report));

    CrashReport report;
    report.set_program_name("crashing_program_dart");
    report.set_specific_report(std::move(specific_report));

    return FileOneCrashReport(std::move(report));
  }

  inspect::Hierarchy InspectTree() {
    auto result = inspect::ReadFromVmo(inspector_->DuplicateVmo());
    FXL_CHECK(result.is_ok());
    return result.take_value();
  }

  uint64_t total_num_feedback_data_provider_bindings() {
    if (!feedback_data_provider_) {
      return 0u;
    }
    return feedback_data_provider_->total_num_bindings();
  }
  size_t current_num_feedback_data_provider_bindings() {
    if (!feedback_data_provider_) {
      return 0u;
    }
    return feedback_data_provider_->current_num_bindings();
  }

 private:
  void RemoveCurrentDirectory(std::vector<std::string>* dirs) {
    dirs->erase(std::remove(dirs->begin(), dirs->end(), "."), dirs->end());
  }

 protected:
  std::unique_ptr<CrashpadAgent> agent_;
  files::ScopedTempDir database_path_;

 private:
  sys::testing::ServiceDirectoryProvider service_directory_provider_;
  std::unique_ptr<StubFeedbackDataProvider> feedback_data_provider_;
  StubCrashServer* crash_server_;
  std::string attachments_dir_;
  std::unique_ptr<inspect::Inspector> inspector_;
  std::unique_ptr<timekeeper::TestClock> clock_;
  std::unique_ptr<InspectManager> inspect_manager_;
};

TEST_F(CrashpadAgentTest, Succeed_OnInputCrashReport) {
  ResetAgentDefaultConfig({kUploadSuccessful});
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  ASSERT_TRUE(FileOneCrashReport().is_ok());
  CheckAttachmentsInDatabase();
  CheckAnnotationsOnServer();
}

TEST_F(CrashpadAgentTest, Succeed_OnInputCrashReportWithAdditionalData) {
  ResetAgentDefaultConfig({kUploadSuccessful});
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  std::vector<Attachment> attachments;
  attachments.emplace_back(BuildAttachment(kSingleAttachmentKey, kSingleAttachmentValue));
  ASSERT_TRUE(FileOneCrashReport(
                  /*annotations=*/
                  {
                      {"annotation.key", "annotation.value"},
                  },
                  /*attachments=*/std::move(attachments))
                  .is_ok());
  CheckAttachmentsInDatabase({kSingleAttachmentKey});
  CheckAnnotationsOnServer({
      {"annotation.key", "annotation.value"},
  });
}

TEST_F(CrashpadAgentTest, Succeed_OnInputCrashReportWithEventId) {
  ResetAgentDefaultConfig({kUploadSuccessful});
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  CrashReport report;
  report.set_program_name(kProgramName);
  report.set_event_id("some-event-id");
  ASSERT_TRUE(FileOneCrashReport(std::move(report)).is_ok());
  CheckAttachmentsInDatabase();
  CheckAnnotationsOnServer({
      {"comments", "some-event-id"},
  });
}

TEST_F(CrashpadAgentTest, Succeed_OnGenericInputCrashReport) {
  ResetAgentDefaultConfig({kUploadSuccessful});
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  ASSERT_TRUE(FileOneGenericCrashReport(std::nullopt).is_ok());
  CheckAttachmentsInDatabase();
  CheckAnnotationsOnServer();
}

TEST_F(CrashpadAgentTest, Succeed_OnGenericInputCrashReportWithSignature) {
  ResetAgentDefaultConfig({kUploadSuccessful});
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  ASSERT_TRUE(FileOneGenericCrashReport("some-signature").is_ok());
  CheckAttachmentsInDatabase();
  CheckAnnotationsOnServer({
      {"signature", "some-signature"},
  });
}

TEST_F(CrashpadAgentTest, Succeed_OnNativeInputCrashReport) {
  ResetAgentDefaultConfig({kUploadSuccessful});
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  fuchsia::mem::Buffer minidump;
  fsl::VmoFromString("minidump", &minidump);
  ASSERT_TRUE(FileOneNativeCrashReport(std::move(minidump)).is_ok());
  CheckAttachmentsInDatabase();
  CheckAnnotationsOnServer({
      {"should_process", "true"},
  });
}

TEST_F(CrashpadAgentTest, Succeed_OnNativeInputCrashReportWithoutMinidump) {
  ResetAgentDefaultConfig({kUploadSuccessful});
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  ASSERT_TRUE(FileOneNativeCrashReport(std::nullopt).is_ok());
  CheckAttachmentsInDatabase();
  CheckAnnotationsOnServer();
}

TEST_F(CrashpadAgentTest, Succeed_OnDartInputCrashReport) {
  ResetAgentDefaultConfig({kUploadSuccessful});
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  fuchsia::mem::Buffer stack_trace;
  fsl::VmoFromString("#0", &stack_trace);
  ASSERT_TRUE(
      FileOneDartCrashReport("FileSystemException", "cannot open file", std::move(stack_trace))
          .is_ok());
  CheckAttachmentsInDatabase({"DartError"});
  CheckAnnotationsOnServer({
      {"error_runtime_type", "FileSystemException"},
      {"error_message", "cannot open file"},
      {"type", "DartError"},
  });
}

TEST_F(CrashpadAgentTest, Succeed_OnDartInputCrashReportWithoutExceptionData) {
  ResetAgentDefaultConfig({kUploadSuccessful});
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  ASSERT_TRUE(FileOneDartCrashReport(std::nullopt, std::nullopt, std::nullopt).is_ok());
  CheckAttachmentsInDatabase();
  CheckAnnotationsOnServer({
      {"type", "DartError"},
  });
}

TEST_F(CrashpadAgentTest, Fail_OnInvalidInputCrashReport) {
  ResetAgentDefaultConfig();
  CrashReport report;

  fit::result<void, zx_status_t> out_result;
  agent_->File(std::move(report), [&out_result](fit::result<void, zx_status_t> result) {
    out_result = std::move(result);
  });
  ASSERT_TRUE(out_result.is_error());
}

TEST_F(CrashpadAgentTest, Check_DatabaseIsEmpty_OnPruneDatabaseWithZeroSize) {
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  // We reset the agent with a max database size of 0, meaning reports will get cleaned up before
  // the end of the |agent_| call.
  ResetAgent(Config{/*crashpad_database=*/
                    {
                        /*path=*/database_path_.path(),
                        /*max_size_in_kb=*/0u,
                    },
                    /*crash_server=*/
                    {
                        /*upload_policy=*/CrashServerConfig::UploadPolicy::DISABLED,
                        /*url=*/nullptr,
                    }});

  // We generate a crash report.
  EXPECT_TRUE(FileOneCrashReport().is_ok());

  // We check that all the attachments have been cleaned up.
  EXPECT_TRUE(GetAttachmentSubdirsInDatabase().empty());
}

std::string GenerateString(const uint64_t string_size_in_kb) {
  std::string str;
  for (size_t i = 0; i < string_size_in_kb * 1024; ++i) {
    str.push_back(static_cast<char>(i % 128));
  }
  return str;
}

TEST_F(CrashpadAgentTest, Check_DatabaseHasOnlyOneReport_OnPruneDatabaseWithSizeForOnlyOneReport) {
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  // We reset the agent with a max database size equivalent to the expected size of a report plus
  // the value of an especially large attachment.
  const uint64_t crash_log_size_in_kb = 2u * kMaxTotalReportSizeInKb;
  const std::string large_string = GenerateString(crash_log_size_in_kb);
  ResetAgent(Config{/*crashpad_database=*/
                    {
                        /*path=*/database_path_.path(),
                        /*max_size_in_kb=*/kMaxTotalReportSizeInKb + crash_log_size_in_kb,
                    },
                    /*crash_server=*/
                    {
                        /*upload_policy=*/CrashServerConfig::UploadPolicy::DISABLED,
                        /*url=*/nullptr,
                    }});

  // We generate a first crash report.
  EXPECT_TRUE(FileOneCrashReportWithSingleAttachment(large_string).is_ok());

  // We check that only one set of attachments is there.
  const std::vector<std::string> attachment_subdirs = GetAttachmentSubdirsInDatabase();
  ASSERT_EQ(attachment_subdirs.size(), 1u);

  // We generate a new crash report.
  EXPECT_TRUE(FileOneCrashReportWithSingleAttachment(large_string).is_ok());

  // We check that only one set of attachments is there.
  const std::vector<std::string> new_attachment_subdirs = GetAttachmentSubdirsInDatabase();
  EXPECT_EQ(new_attachment_subdirs.size(), 1u);
  // We cannot expect the set of attachments to be different than the first set as the real-time
  // clock could go back in time between the generation of the two reports and then the second
  // report would actually be older than the first report and be the one that was pruned, cf.
  // fxb/37067.
}

TEST_F(CrashpadAgentTest, Check_DatabaseHasNoOrphanedAttachments) {
  ResetAgentDefaultConfig({kUploadSuccessful});
  // We generate an orphan attachment and check it is there.
  const std::string kOrphanedAttachmentDir = files::JoinPath(
      database_path_.path(), files::JoinPath(kCrashpadAttachmentsDir, kCrashpadUUIDString));
  files::CreateDirectory(kOrphanedAttachmentDir);
  const std::vector<std::string> attachment_subdirs = GetAttachmentSubdirsInDatabase();
  EXPECT_THAT(attachment_subdirs, ElementsAre(kCrashpadUUIDString));

  // We generate a crash report with its own attachment.
  EXPECT_TRUE(FileOneCrashReportWithSingleAttachment("an attachment").is_ok());

  // We check that only one set of attachments is present and different than the
  // prior set (the name of the directory is the local crash report ID).
  const std::vector<std::string> new_attachment_subdirs = GetAttachmentSubdirsInDatabase();
  EXPECT_THAT(new_attachment_subdirs, Not(UnorderedElementsAreArray(attachment_subdirs)));
}

TEST_F(CrashpadAgentTest, Succeed_OnConcurrentReports) {
  ResetAgentDefaultConfig(std::vector<bool>(10, kUploadSuccessful));
  // We generate ten crash reports before runnning the loop to make sure that one crash
  // report filing doesn't clean up the concurrent crash reports being filed.
  const size_t kNumReports = 10;

  std::vector<fit::result<void, zx_status_t>> results;
  for (size_t i = 0; i < kNumReports; ++i) {
    CrashReport report;
    report.set_program_name(kProgramName);
    agent_->File(std::move(report),
                 [&results](fit::result<void, zx_status_t> result) { results.push_back(result); });
  }

  ASSERT_TRUE(RunLoopUntilIdle());
  EXPECT_EQ(results.size(), kNumReports);
  for (const auto result : results) {
    EXPECT_TRUE(result.is_ok());
  }
}

TEST_F(CrashpadAgentTest, Succeed_OnFailedUpload) {
  ResetAgent(
      Config{/*crashpad_database=*/
             {
                 /*path=*/database_path_.path(),
                 /*max_size_in_kb=*/kMaxTotalReportSizeInKb,
             },
             /*crash_server=*/
             {
                 /*upload_policy=*/CrashServerConfig::UploadPolicy::ENABLED,
                 /*url=*/std::make_unique<std::string>(kStubCrashServerUrl),
             }},
      std::make_unique<StubCrashServer>(std::vector<bool>({kUploadFailed})));

  EXPECT_TRUE(FileOneCrashReport().is_ok());
}

TEST_F(CrashpadAgentTest, Succeed_OnDisabledUpload) {
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  ResetAgent(Config{/*crashpad_database=*/
                    {
                        /*path=*/database_path_.path(),
                        /*max_size_in_kb=*/kMaxTotalReportSizeInKb,
                    },
                    /*crash_server=*/
                    {
                        /*upload_policy=*/CrashServerConfig::UploadPolicy::DISABLED,
                        /*url=*/nullptr,
                    }});

  EXPECT_TRUE(FileOneCrashReport().is_ok());
}

TEST_F(CrashpadAgentTest, Succeed_OnNoFeedbackAttachments) {
  ResetAgentDefaultConfig({kUploadSuccessful});
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProviderReturnsNoAttachment>());
  EXPECT_TRUE(FileOneCrashReportWithSingleAttachment().is_ok());
  CheckAttachmentsInDatabase({kSingleAttachmentKey});
  CheckAnnotationsOnServer();
}

TEST_F(CrashpadAgentTest, Succeed_OnNoFeedbackAnnotations) {
  ResetAgentDefaultConfig({kUploadSuccessful});
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProviderReturnsNoAnnotation>());
  EXPECT_TRUE(FileOneCrashReportWithSingleAttachment().is_ok());
  CheckAttachmentsInDatabase({kSingleAttachmentKey});
  CheckAnnotationsOnServer();
}

TEST_F(CrashpadAgentTest, Succeed_OnNoFeedbackData) {
  ResetAgentDefaultConfig({kUploadSuccessful});
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProviderReturnsNoData>());
  EXPECT_TRUE(FileOneCrashReportWithSingleAttachment().is_ok());
  CheckAttachmentsInDatabase({kSingleAttachmentKey});
  CheckAnnotationsOnServer();
}

TEST_F(CrashpadAgentTest, Succeed_OnNoFeedbackDataProvider) {
  ResetAgentDefaultConfig({kUploadSuccessful});
  // We pass a nullptr stub so there will be no fuchsia.feedback.DataProvider service to connect to.
  ResetFeedbackDataProvider(nullptr);
  EXPECT_TRUE(FileOneCrashReportWithSingleAttachment().is_ok());
  CheckAttachmentsInDatabase({kSingleAttachmentKey});
  CheckAnnotationsOnServer();
}

TEST_F(CrashpadAgentTest, Succeed_OnFeedbackDataProviderTakingTooLong) {
  ResetAgentDefaultConfig({kUploadSuccessful});
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProviderNeverReturning>());
  fit::result<void, zx_status_t> result = FileOneCrashReportWithSingleAttachment();
  RunLoopFor(zx::sec(10));
  EXPECT_TRUE(result.is_ok());
  CheckAttachmentsInDatabase({kSingleAttachmentKey});
  CheckAnnotationsOnServer();
}

TEST_F(CrashpadAgentTest, Check_OneFeedbackDataProviderConnectionPerAnalysis) {
  const size_t num_calls = 5u;
  ResetAgentDefaultConfig(std::vector<bool>(num_calls, true));
  // We use a stub that returns no data as we are not interested in the payload, just the number of
  // different connections to the stub.
  ResetFeedbackDataProvider(std::make_unique<StubFeedbackDataProviderReturnsNoData>());

  for (size_t i = 0; i < num_calls; i++) {
    FileOneCrashReportWithSingleAttachment();
  }

  EXPECT_EQ(total_num_feedback_data_provider_bindings(), num_calls);
  EXPECT_EQ(current_num_feedback_data_provider_bindings(), 0u);
}

TEST_F(CrashpadAgentTest, Check_InitialInspectTree) {
  ResetAgentDefaultConfig();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(NameMatches(kInspectConfigName)),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(
                        AllOf(NameMatches(kCrashpadDatabaseKey),
                              PropertyList(UnorderedElementsAreArray({
                                  StringIs(kCrashpadDatabasePathKey, database_path_.path()),
                                  UintIs(kCrashpadDatabaseMaxSizeInKbKey, kMaxTotalReportSizeInKb),
                              })))),
                    NodeMatches(
                        AllOf(NameMatches(kCrashServerKey),
                              PropertyList(UnorderedElementsAreArray({
                                  StringIs(kCrashServerUploadPolicyKey,
                                           ToString(CrashServerConfig::UploadPolicy::ENABLED)),
                                  StringIs(kCrashServerUrlKey, kStubCrashServerUrl),
                              })))),
                }))),
          NodeMatches(AllOf(NameMatches(kInspectSettingsName),
                            PropertyList(ElementsAre(StringIs(
                                "upload_policy", ToString(Settings::UploadPolicy::ENABLED)))))),
          NodeMatches(NameMatches(kInspectReportsName)))));
}

TEST_F(CrashpadAgentTest, Check_InspectTreeAfterSuccessfulUpload) {
  ResetAgentDefaultConfig({kUploadSuccessful});
  EXPECT_TRUE(FileOneCrashReport().is_ok());

  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches(kInspectReportsName)),
          ChildrenMatch(ElementsAre(AllOf(
              NodeMatches(NameMatches(kProgramName)),
              ChildrenMatch(ElementsAre(AllOf(
                  NodeMatches(PropertyList(ElementsAre(StringIs("creation_time", Not(IsEmpty()))))),
                  ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                      NameMatches("crash_server"), PropertyList(UnorderedElementsAreArray({
                                                       StringIs("creation_time", Not(IsEmpty())),
                                                       StringIs("id", kStubServerReportId),
                                                   }))))))))))))))));
}

}  // namespace
}  // namespace feedback

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"feedback", "test"});
  return RUN_ALL_TESTS();
}
