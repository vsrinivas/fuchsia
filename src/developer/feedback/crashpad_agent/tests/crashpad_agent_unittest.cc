// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/crashpad_agent.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <fuchsia/settings/cpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>
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
#include "src/developer/feedback/crashpad_agent/tests/fake_privacy_settings.h"
#include "src/developer/feedback/crashpad_agent/tests/stub_crash_server.h"
#include "src/developer/feedback/crashpad_agent/tests/stub_feedback_data_provider.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
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
using fuchsia::settings::PrivacySettings;
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

constexpr char kCrashpadDatabasePath[] = "/tmp/crashes";

// "attachments" should be kept in sync with the value defined in
// //crashpad/client/crash_report_database_generic.cc
constexpr char kCrashpadAttachmentsDir[] = "attachments";
constexpr char kProgramName[] = "crashing_program";

constexpr char kSingleAttachmentKey[] = "attachment.key";
constexpr char kSingleAttachmentValue[] = "attachment.value";

constexpr bool kUserOptInDataSharing = true;
constexpr bool kUserOptOutDataSharing = false;

Attachment BuildAttachment(const std::string& key, const std::string& value) {
  Attachment attachment;
  attachment.key = key;
  FXL_CHECK(fsl::VmoFromString(value, &attachment.value));
  return attachment;
}

PrivacySettings MakePrivacySettings(const std::optional<bool> user_data_sharing_consent) {
  PrivacySettings settings;
  if (user_data_sharing_consent.has_value()) {
    settings.set_user_data_sharing_consent(user_data_sharing_consent.value());
  }
  return settings;
}

// Unit-tests the implementation of the fuchsia.feedback.CrashReporter FIDL interface.
//
// This does not test the environment service. It directly instantiates the class, without
// connecting through FIDL.
class CrashpadAgentTest : public UnitTestFixture {
  void TearDown() override {
    ASSERT_TRUE(files::DeletePath(kCrashpadDatabasePath, /*recursive=*/true));
  }

 protected:
  // Sets up the underlying agent using the given |config| and |crash_server|.
  void SetUpAgent(Config config, std::unique_ptr<StubCrashServer> crash_server) {
    FXL_CHECK((config.crash_server.url && crash_server) ||
              (!config.crash_server.url && !crash_server));
    crash_server_ = crash_server.get();

    attachments_dir_ = files::JoinPath(kCrashpadDatabasePath, kCrashpadAttachmentsDir);
    inspector_ = std::make_unique<inspect::Inspector>();
    clock_ = std::make_unique<timekeeper::TestClock>();
    inspect_manager_ = std::make_unique<InspectManager>(&inspector_->GetRoot(), clock_.get());
    agent_ = CrashpadAgent::TryCreate(dispatcher(), services(), std::move(config),
                                      std::move(crash_server), inspect_manager_.get());
    FXL_CHECK(agent_);
  }

  // Sets up the underlying agent using the given |config|.
  void SetUpAgent(Config config) {
    FXL_CHECK(!config.crash_server.url);
    return SetUpAgent(std::move(config), /*crash_server=*/nullptr);
  }

  // Sets up the underlying agent using a default config.
  void SetUpAgentDefaultConfig(const std::vector<bool>& upload_attempt_results = {}) {
    SetUpAgent(
        Config{/*crashpad_database=*/
               {
                   /*max_size_in_kb=*/kMaxTotalReportSizeInKb,
               },
               /*crash_server=*/
               {
                   /*upload_policy=*/CrashServerConfig::UploadPolicy::ENABLED,
                   /*url=*/std::make_unique<std::string>(kStubCrashServerUrl),
               }},
        std::make_unique<StubCrashServer>(upload_attempt_results));
  }

  // Sets up the underlying feedback data provider and registers it in the
  // |service_directory_provider_|.
  void SetUpFeedbackDataProvider(std::unique_ptr<StubFeedbackDataProvider> feedback_data_provider) {
    feedback_data_provider_ = std::move(feedback_data_provider);
    if (feedback_data_provider_) {
      InjectServiceProvider(feedback_data_provider_.get());
    }
  }

  // Sets up the underlying privacy settings and registers it in the |service_directory_provider_|.
  void SetUpPrivacySettings(std::unique_ptr<FakePrivacySettings> privacy_settings) {
    privacy_settings_ = std::move(privacy_settings);
    if (privacy_settings_) {
      InjectServiceProvider(privacy_settings_.get());
    }
  }

  // Checks that in the local Crashpad database there is:
  //   * only one set of attachments
  //   * the set of attachment filenames matches the concatenation of
  //     |expected_extra_attachments| and feedback_data_provider_->attachment_bundle_key()
  //   * no attachment is empty
  void CheckAttachmentsInDatabase(
      const std::vector<std::string>& expected_extra_attachment_filenames = {}) {
    const std::vector<std::string> subdirs = GetAttachmentSubdirsInDatabase();
    // We expect a single crash report to have been generated.
    ASSERT_EQ(subdirs.size(), 1u);

    // We expect as attachments the ones returned by the feedback::DataProvider and the extra ones
    // specific to the crash analysis flow under test.
    std::vector<std::string> expected_attachments = expected_extra_attachment_filenames;
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

  // Checks that on the crash server the annotations received match the concatenation of:
  //   * |expected_extra_annotations|
  //   * feedback_data_provider_->annotations()
  //   * default annotations
  //
  // In case of duplicate keys, the value from |expected_extra_annotations| is picked.
  void CheckAnnotationsOnServer(
      const std::map<std::string, testing::Matcher<std::string>>& expected_extra_annotations = {}) {
    ASSERT_TRUE(crash_server_);

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

  // Checks that on the crash server the keys for the attachments received match the
  // concatenation of:
  //   * |expected_extra_attachment_keys|
  //   * feedback_data_provider_->attachment_bundle_key()
  void CheckAttachmentsOnServer(
      const std::vector<std::string>& expected_extra_attachment_keys = {}) {
    ASSERT_TRUE(crash_server_);

    std::vector<std::string> expected_attachment_keys = expected_extra_attachment_keys;
    if (feedback_data_provider_ && feedback_data_provider_->has_attachment_bundle_key()) {
      expected_attachment_keys.push_back(feedback_data_provider_->attachment_bundle_key());
    }

    EXPECT_EQ(crash_server_->latest_attachment_keys().size(), expected_attachment_keys.size());
    for (const auto& key : expected_attachment_keys) {
      EXPECT_THAT(crash_server_->latest_attachment_keys(), testing::Contains(key));
    }
  }

  // Checks that the crash server is still expecting at least one more request.
  //
  // This is useful to check that an upload request hasn't been made as we are using a strict stub.
  void CheckServerStillExpectRequests() {
    ASSERT_TRUE(crash_server_);
    EXPECT_TRUE(crash_server_->ExpectRequest());
  }

  // Files one crash report.
  fit::result<void, zx_status_t> FileOneCrashReport(CrashReport report) {
    FXL_CHECK(agent_ != nullptr) << "agent_ is nullptr. Call SetUpAgent() or one of its variants "
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

  void SetPrivacySettings(std::optional<bool> user_data_sharing_consent) {
    ASSERT_TRUE(privacy_settings_);

    fit::result<void, fuchsia::settings::Error> set_result;
    privacy_settings_->Set(MakePrivacySettings(user_data_sharing_consent),
                           [&set_result](fit::result<void, fuchsia::settings::Error> result) {
                             set_result = std::move(result);
                           });
    EXPECT_TRUE(set_result.is_ok());
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

  void RemoveCurrentDirectory(std::vector<std::string>* dirs) {
    dirs->erase(std::remove(dirs->begin(), dirs->end(), "."), dirs->end());
  }

 protected:
  std::unique_ptr<CrashpadAgent> agent_;

 private:
  std::unique_ptr<StubFeedbackDataProvider> feedback_data_provider_;
  std::unique_ptr<FakePrivacySettings> privacy_settings_;
  StubCrashServer* crash_server_;
  std::string attachments_dir_;
  std::unique_ptr<inspect::Inspector> inspector_;
  std::unique_ptr<timekeeper::TestClock> clock_;
  std::unique_ptr<InspectManager> inspect_manager_;
};

TEST_F(CrashpadAgentTest, Succeed_OnInputCrashReport) {
  SetUpAgentDefaultConfig({kUploadSuccessful});
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());

  ASSERT_TRUE(FileOneCrashReport().is_ok());
  CheckAttachmentsInDatabase();
  CheckAnnotationsOnServer();
  CheckAttachmentsOnServer();
}

TEST_F(CrashpadAgentTest, Succeed_OnInputCrashReportWithAdditionalData) {
  SetUpAgentDefaultConfig({kUploadSuccessful});
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
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
  CheckAttachmentsOnServer({kSingleAttachmentKey});
}

TEST_F(CrashpadAgentTest, Succeed_OnInputCrashReportWithEventId) {
  SetUpAgentDefaultConfig({kUploadSuccessful});
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  CrashReport report;
  report.set_program_name(kProgramName);
  report.set_event_id("some-event-id");

  ASSERT_TRUE(FileOneCrashReport(std::move(report)).is_ok());
  CheckAttachmentsInDatabase();
  CheckAnnotationsOnServer({
      {"comments", "some-event-id"},
  });
  CheckAttachmentsOnServer();
}

TEST_F(CrashpadAgentTest, Succeed_OnInputCrashReportWithProgramUptime) {
  SetUpAgentDefaultConfig({kUploadSuccessful});
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  CrashReport report;
  report.set_program_name(kProgramName);
  const zx::duration uptime =
      zx::hour(3) * 24 + zx::hour(15) + zx::min(33) + zx::sec(17) + zx::msec(54);
  report.set_program_uptime(uptime.get());

  ASSERT_TRUE(FileOneCrashReport(std::move(report)).is_ok());
  CheckAttachmentsInDatabase();
  CheckAnnotationsOnServer({
      {"ptime", std::to_string(uptime.to_msecs())},
  });
  CheckAttachmentsOnServer();
}

TEST_F(CrashpadAgentTest, Succeed_OnGenericInputCrashReport) {
  SetUpAgentDefaultConfig({kUploadSuccessful});
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());

  ASSERT_TRUE(FileOneGenericCrashReport(std::nullopt).is_ok());
  CheckAttachmentsInDatabase();
  CheckAnnotationsOnServer();
  CheckAttachmentsOnServer();
}

TEST_F(CrashpadAgentTest, Succeed_OnGenericInputCrashReportWithSignature) {
  SetUpAgentDefaultConfig({kUploadSuccessful});
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());

  ASSERT_TRUE(FileOneGenericCrashReport("some-signature").is_ok());
  CheckAttachmentsInDatabase();
  CheckAnnotationsOnServer({
      {"signature", "some-signature"},
  });
  CheckAttachmentsOnServer();
}

TEST_F(CrashpadAgentTest, Succeed_OnNativeInputCrashReport) {
  SetUpAgentDefaultConfig({kUploadSuccessful});
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  fuchsia::mem::Buffer minidump;
  fsl::VmoFromString("minidump", &minidump);

  ASSERT_TRUE(FileOneNativeCrashReport(std::move(minidump)).is_ok());
  CheckAttachmentsInDatabase();
  CheckAnnotationsOnServer({
      {"should_process", "true"},
  });
  CheckAttachmentsOnServer({"uploadFileMinidump"});
}

TEST_F(CrashpadAgentTest, Succeed_OnNativeInputCrashReportWithoutMinidump) {
  SetUpAgentDefaultConfig({kUploadSuccessful});
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());

  ASSERT_TRUE(FileOneNativeCrashReport(std::nullopt).is_ok());
  CheckAttachmentsInDatabase();
  CheckAnnotationsOnServer({
      {"signature", "fuchsia-no-minidump"},
  });
  CheckAttachmentsOnServer();
}

TEST_F(CrashpadAgentTest, Succeed_OnDartInputCrashReport) {
  SetUpAgentDefaultConfig({kUploadSuccessful});
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
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
      {"should_process", "true"},
  });
  CheckAttachmentsOnServer({"DartError"});
}

TEST_F(CrashpadAgentTest, Succeed_OnDartInputCrashReportWithoutExceptionData) {
  SetUpAgentDefaultConfig({kUploadSuccessful});
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());

  ASSERT_TRUE(FileOneDartCrashReport(std::nullopt, std::nullopt, std::nullopt).is_ok());
  CheckAttachmentsInDatabase();
  CheckAnnotationsOnServer({
      {"type", "DartError"},
      {"signature", "fuchsia-no-dart-stack-trace"},
  });
  CheckAttachmentsOnServer();
}

TEST_F(CrashpadAgentTest, Fail_OnInvalidInputCrashReport) {
  SetUpAgentDefaultConfig();
  CrashReport report;

  fit::result<void, zx_status_t> out_result;
  agent_->File(std::move(report), [&out_result](fit::result<void, zx_status_t> result) {
    out_result = std::move(result);
  });
  ASSERT_TRUE(out_result.is_error());
}

TEST_F(CrashpadAgentTest, Upload_OnUserAlreadyOptedInDataSharing) {
  SetUpPrivacySettings(std::make_unique<FakePrivacySettings>());
  SetPrivacySettings(kUserOptInDataSharing);
  SetUpAgent(
      Config{/*crashpad_database=*/
             {
                 /*max_size_in_kb=*/kMaxTotalReportSizeInKb,
             },
             /*crash_server=*/
             {
                 /*upload_policy=*/CrashServerConfig::UploadPolicy::READ_FROM_PRIVACY_SETTINGS,
                 /*url=*/std::make_unique<std::string>(kStubCrashServerUrl),
             }},
      std::make_unique<StubCrashServer>(std::vector<bool>({kUploadSuccessful})));
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());

  ASSERT_TRUE(FileOneCrashReport().is_ok());
  CheckAttachmentsInDatabase();
  CheckAnnotationsOnServer();
  CheckAttachmentsOnServer();
}

TEST_F(CrashpadAgentTest, Archive_OnUserAlreadyOptedOutDataSharing) {
  SetUpPrivacySettings(std::make_unique<FakePrivacySettings>());
  SetPrivacySettings(kUserOptOutDataSharing);
  SetUpAgent(
      Config{/*crashpad_database=*/
             {
                 /*max_size_in_kb=*/kMaxTotalReportSizeInKb,
             },
             /*crash_server=*/
             {
                 /*upload_policy=*/CrashServerConfig::UploadPolicy::READ_FROM_PRIVACY_SETTINGS,
                 /*url=*/std::make_unique<std::string>(kStubCrashServerUrl),
             }},
      std::make_unique<StubCrashServer>(std::vector<bool>({})));
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());

  ASSERT_TRUE(FileOneCrashReport().is_ok());
  CheckAttachmentsInDatabase();
}

TEST_F(CrashpadAgentTest, Upload_OnceUserOptInDataSharing) {
  SetUpPrivacySettings(std::make_unique<FakePrivacySettings>());
  SetUpAgent(
      Config{/*crashpad_database=*/
             {
                 /*max_size_in_kb=*/kMaxTotalReportSizeInKb,
             },
             /*crash_server=*/
             {
                 /*upload_policy=*/CrashServerConfig::UploadPolicy::READ_FROM_PRIVACY_SETTINGS,
                 /*url=*/std::make_unique<std::string>(kStubCrashServerUrl),
             }},
      std::make_unique<StubCrashServer>(std::vector<bool>({kUploadSuccessful})));
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());

  ASSERT_TRUE(FileOneCrashReport().is_ok());
  CheckAttachmentsInDatabase();
  CheckServerStillExpectRequests();

  SetPrivacySettings(kUserOptInDataSharing);
  ASSERT_TRUE(RunLoopUntilIdle());

  CheckAnnotationsOnServer();
  CheckAttachmentsOnServer();
}

TEST_F(CrashpadAgentTest, Succeed_OnConcurrentReports) {
  SetUpAgentDefaultConfig(std::vector<bool>(10, kUploadSuccessful));
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
  SetUpAgent(
      Config{/*crashpad_database=*/
             {
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
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProvider>());
  SetUpAgent(Config{/*crashpad_database=*/
                    {
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
  SetUpAgentDefaultConfig({kUploadSuccessful});
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProviderReturnsNoAttachment>());

  EXPECT_TRUE(FileOneCrashReportWithSingleAttachment().is_ok());
  CheckAttachmentsInDatabase({kSingleAttachmentKey});
  CheckAnnotationsOnServer();
  CheckAttachmentsOnServer({kSingleAttachmentKey});
}

TEST_F(CrashpadAgentTest, Succeed_OnNoFeedbackAnnotations) {
  SetUpAgentDefaultConfig({kUploadSuccessful});
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProviderReturnsNoAnnotation>());

  EXPECT_TRUE(FileOneCrashReportWithSingleAttachment().is_ok());
  CheckAttachmentsInDatabase({kSingleAttachmentKey});
  CheckAnnotationsOnServer();
  CheckAttachmentsOnServer({kSingleAttachmentKey});
}

TEST_F(CrashpadAgentTest, Succeed_OnNoFeedbackData) {
  SetUpAgentDefaultConfig({kUploadSuccessful});
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProviderReturnsNoData>());

  EXPECT_TRUE(FileOneCrashReportWithSingleAttachment().is_ok());
  CheckAttachmentsInDatabase({kSingleAttachmentKey});
  CheckAnnotationsOnServer();
  CheckAttachmentsOnServer({kSingleAttachmentKey});
}

TEST_F(CrashpadAgentTest, Succeed_OnNoFeedbackDataProvider) {
  SetUpAgentDefaultConfig({kUploadSuccessful});
  // We pass a nullptr stub so there will be no fuchsia.feedback.DataProvider service to connect to.
  SetUpFeedbackDataProvider(nullptr);

  EXPECT_TRUE(FileOneCrashReportWithSingleAttachment().is_ok());
  CheckAttachmentsInDatabase({kSingleAttachmentKey});
  CheckAnnotationsOnServer();
  CheckAttachmentsOnServer({kSingleAttachmentKey});
}

TEST_F(CrashpadAgentTest, Succeed_OnFeedbackDataProviderTakingTooLong) {
  SetUpAgentDefaultConfig({kUploadSuccessful});
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProviderNeverReturning>());

  fit::result<void, zx_status_t> result = FileOneCrashReportWithSingleAttachment();
  RunLoopFor(zx::sec(30) + zx::sec(5));

  EXPECT_TRUE(result.is_ok());
  CheckAttachmentsInDatabase({kSingleAttachmentKey});
  CheckAnnotationsOnServer();
  CheckAttachmentsOnServer({kSingleAttachmentKey});
}

TEST_F(CrashpadAgentTest, Check_OneFeedbackDataProviderConnectionPerAnalysis) {
  const size_t num_calls = 5u;
  SetUpAgentDefaultConfig(std::vector<bool>(num_calls, true));
  // We use a stub that returns no data as we are not interested in the payload, just the number of
  // different connections to the stub.
  SetUpFeedbackDataProvider(std::make_unique<StubFeedbackDataProviderReturnsNoData>());

  for (size_t i = 0; i < num_calls; i++) {
    FileOneCrashReportWithSingleAttachment();
  }

  EXPECT_EQ(total_num_feedback_data_provider_bindings(), num_calls);
  EXPECT_EQ(current_num_feedback_data_provider_bindings(), 0u);
}

TEST_F(CrashpadAgentTest, Check_InitialInspectTree) {
  SetUpAgentDefaultConfig();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(NameMatches(kInspectConfigName)),
                ChildrenMatch(UnorderedElementsAreArray({
                    NodeMatches(
                        AllOf(NameMatches(kCrashpadDatabaseKey),
                              PropertyList(UnorderedElementsAreArray({
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
  SetUpAgentDefaultConfig({kUploadSuccessful});
  EXPECT_TRUE(FileOneCrashReport().is_ok());

  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches(kInspectReportsName)),
          ChildrenMatch(ElementsAre(AllOf(
              NodeMatches(NameMatches(kProgramName)),
              ChildrenMatch(ElementsAre(AllOf(
                  NodeMatches(PropertyList(UnorderedElementsAreArray({
                      StringIs("creation_time", Not(IsEmpty())),
                      StringIs("final_state", "uploaded"),
                      UintIs("upload_attempts", 1u),
                  }))),
                  ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                      NameMatches("crash_server"), PropertyList(UnorderedElementsAreArray({
                                                       StringIs("creation_time", Not(IsEmpty())),
                                                       StringIs("id", kStubServerReportId),
                                                   }))))))))))))))));
}

}  // namespace
}  // namespace feedback
