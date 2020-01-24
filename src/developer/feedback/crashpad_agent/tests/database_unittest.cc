// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/database.h"

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/timekeeper/test_clock.h>

#include <utility>

#include "sdk/lib/inspect/testing/cpp/inspect.h"
#include "src/developer/feedback/crashpad_agent/info/info_context.h"
#include "src/developer/feedback/crashpad_agent/tests/crashpad_database_gremlin.h"
#include "src/developer/feedback/testing/cobalt_test_fixture.h"
#include "src/developer/feedback/testing/stubs/stub_cobalt_logger_factory.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/developer/feedback/utils/cobalt_metrics.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/path.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using cobalt_registry::kCrashMetricId;
using cobalt_registry::kCrashUploadAttemptsMetricId;
using crashpad::UUID;
using inspect::testing::ChildrenMatch;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;
using inspect::testing::UintIs;
using testing::ByRef;
using testing::Contains;
using testing::ElementsAre;
using testing::Eq;
using testing::IsEmpty;
using testing::IsSubsetOf;
using testing::IsSupersetOf;
using testing::Key;
using testing::Not;
using testing::Pair;
using testing::UnorderedElementsAreArray;

constexpr zx::time_utc kTime((zx::hour(7) + zx::min(14) + zx::sec(52)).get());
constexpr char kTimeStr[] = "1970-01-01 07:14:52 GMT";

constexpr uint64_t kMaxTotalReportsSizeInKb = 1024u;

constexpr char kCrashpadDatabasePath[] = "/tmp/crashes";
constexpr char kCrashpadAttachmentsDir[] = "attachments";
constexpr char kCrashpadCompletedDir[] = "completed";
constexpr char kCrashpadPendingDir[] = "pending";

constexpr char kCrashReportExtension[] = "dmp";
constexpr char kMetadataExtension[] = "meta";
constexpr char kAnnotationKey[] = "annotation.key";
constexpr char kAnnotationValue[] = "annotation.value";
constexpr char kAttachmentKey[] = "attachment.key";
constexpr char kAttachmentValue[] = "attachment.value";

constexpr char kCrashpadUUIDString[] = "00000000-0000-0000-0000-000000000001";

fuchsia::mem::Buffer BuildAttachment(const std::string& value) {
  fuchsia::mem::Buffer attachment;
  FXL_CHECK(fsl::VmoFromString(value, &attachment));
  return attachment;
}

std::map<std::string, fuchsia::mem::Buffer> CreateAttachments(
    const std::map<std::string, std::string>& attachments) {
  std::map<std::string, fuchsia::mem::Buffer> new_attachments;
  for (const auto& [key, attachment] : attachments) {
    new_attachments[key] = BuildAttachment(attachment);
  }
  return new_attachments;
}

std::string GenerateString(const uint64_t string_size_in_kb) {
  std::string str;
  for (size_t i = 0; i < string_size_in_kb * 1024; ++i) {
    str.push_back(static_cast<char>(i % 128));
  }
  return str;
}

class DatabaseTest : public UnitTestFixture, public CobaltTestFixture {
 public:
  DatabaseTest() : UnitTestFixture(), CobaltTestFixture(/*unit_test_fixture*/ this) {}

  void SetUp() override {
    inspector_ = std::make_unique<inspect::Inspector>();
    info_context_ =
        std::make_shared<InfoContext>(&inspector_->GetRoot(), clock_, dispatcher(), services());

    ASSERT_TRUE(SetUpDatabase(/*max_size_in_kb=*/kMaxTotalReportsSizeInKb));
    SetUpCobaltLoggerFactory(std::make_unique<StubCobaltLoggerFactory>());
    RunLoopUntilIdle();
  }

  void TearDown() override {
    ASSERT_TRUE(files::DeletePath(kCrashpadDatabasePath, /*recursive=*/true));
  }

 protected:
  bool SetUpDatabase(const uint64_t max_size_in_kb) {
    auto new_database = Database::TryCreate(info_context_, max_size_in_kb);
    if (!new_database) {
      return false;
    }
    database_ = std::move(new_database);
    attachments_dir_ = files::JoinPath(kCrashpadDatabasePath, kCrashpadAttachmentsDir);
    completed_dir_ = files::JoinPath(kCrashpadDatabasePath, kCrashpadCompletedDir);
    pending_dir_ = files::JoinPath(kCrashpadDatabasePath, kCrashpadPendingDir);
    return true;
  }

  std::vector<std::string> GetAttachmentsDirContents() {
    return GetDirectoryContents(attachments_dir_);
  }

  std::vector<std::string> GetCompletedDirContents() {
    return GetDirectoryContents(completed_dir_);
  }

  std::vector<std::string> GetPendingDirContents() { return GetDirectoryContents(pending_dir_); }

  std::string GetMetadataFilepath(const UUID& local_report_id) {
    return AddExtension(local_report_id.ToString(), kMetadataExtension);
  }

  std::string GetMinidumpFilepath(const UUID& local_report_id) {
    return AddExtension(local_report_id.ToString(), kCrashReportExtension);
  }

  void MakeNewReportOrDie(UUID* local_report_id) {
    MakeNewReportOrDie(/*attachments=*/{}, local_report_id);
  }

  void MakeNewReportOrDie(const std::map<std::string, std::string>& attachments,
                          UUID* local_report_id) {
    MakeNewReportOrDie(attachments, /*minidump=*/std::nullopt, /*annotations=*/{}, local_report_id);
  }

  void MakeNewReportOrDie(const std::map<std::string, std::string>& attachments,
                          const std::optional<std::string>& minidump,
                          const std::map<std::string, std::string>& annotations,
                          UUID* local_report_id) {
    std::optional<fuchsia::mem::Buffer> mem_buf_minidump =
        minidump ? std::optional<fuchsia::mem::Buffer>(BuildAttachment(*minidump)) : std::nullopt;
    ASSERT_TRUE(database_->MakeNewReport(CreateAttachments(attachments), mem_buf_minidump,
                                         annotations, local_report_id));

    if (!attachments.empty() || minidump.has_value()) {
      ASSERT_THAT(GetAttachmentsDirContents(), Contains(local_report_id->ToString()));
    }

    ASSERT_THAT(GetPendingDirContents(), IsSupersetOf({
                                             GetMetadataFilepath(*local_report_id),
                                             GetMinidumpFilepath(*local_report_id),
                                         }));
  }

  inspect::Hierarchy InspectTree() {
    auto result = inspect::ReadFromVmo(inspector_->DuplicateVmo());
    FXL_CHECK(result.is_ok());
    return result.take_value();
  }

 private:
  std::vector<std::string> GetDirectoryContents(const std::string& path) {
    std::vector<std::string> contents;
    FXL_CHECK(files::ReadDirContents(path, &contents));
    RemoveCurrentDirectory(&contents);
    return contents;
  }

  void RemoveCurrentDirectory(std::vector<std::string>* contents) {
    contents->erase(std::remove(contents->begin(), contents->end(), "."), contents->end());
  }

  std::string AddExtension(const std::string& filename, const std::string& extension) {
    return filename + "." + extension;
  }

 protected:
  timekeeper::TestClock clock_;
  std::shared_ptr<InfoContext> info_context_;
  std::string attachments_dir_;

 private:
  std::unique_ptr<inspect::Inspector> inspector_;
  std::string completed_dir_;
  std::string pending_dir_;

 protected:
  std::unique_ptr<Database> database_;
};

TEST_F(DatabaseTest, Check_DatabaseIsEmpty_OnPruneDatabaseWithZeroSize) {
  // Set up the database with a max size of 0, meaning any reports in the database with size > 0
  // will get garbage collected.
  SetUpDatabase(/*max_size_in_kb=*/0u);

  // Add a crash report.
  UUID local_report_id;
  MakeNewReportOrDie(
      /*attachments=*/{{kAttachmentKey, kAttachmentValue}}, &local_report_id);

  // Check that garbage collection occurs correctly.
  EXPECT_EQ(database_->GarbageCollect(), 1u);
  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          CobaltEvent(CrashState::kGarbageCollected),
                                      }));

  EXPECT_TRUE(GetAttachmentsDirContents().empty());
  EXPECT_TRUE(GetPendingDirContents().empty());
}

TEST_F(DatabaseTest, Check_DatabaseHasOnlyOneReport_OnPruneDatabaseWithSizeForOnlyOneReport) {
  // We set up the database with a max size equivalent to the expected size of a report plus the
  // value of a rather large attachment.
  const uint64_t crash_log_size_in_kb = 2u * kMaxTotalReportsSizeInKb;
  const std::string large_string = GenerateString(crash_log_size_in_kb);
  SetUpDatabase(/*max_size_in_kb=*/kMaxTotalReportsSizeInKb + crash_log_size_in_kb);

  // Add a crash report.
  UUID local_report_id_1;
  MakeNewReportOrDie(
      /*attachments=*/{{kAttachmentKey, large_string}}, &local_report_id_1);

  // Add a crash report.
  UUID local_report_id_2;
  MakeNewReportOrDie(
      /*attachments=*/{{kAttachmentKey, large_string}}, &local_report_id_2);

  // Check that the contents of the new report are present.
  ASSERT_THAT(GetAttachmentsDirContents(), UnorderedElementsAreArray({
                                               local_report_id_1.ToString(),
                                               local_report_id_2.ToString(),
                                           }));

  ASSERT_THAT(GetPendingDirContents(), UnorderedElementsAreArray({
                                           GetMetadataFilepath(local_report_id_1),
                                           GetMinidumpFilepath(local_report_id_1),
                                           GetMetadataFilepath(local_report_id_2),
                                           GetMinidumpFilepath(local_report_id_2),
                                       }));

  // Check that garbage collection occurs correctly.
  EXPECT_EQ(database_->GarbageCollect(), 1u);
  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          CobaltEvent(CrashState::kGarbageCollected),
                                      }));

  // We cannot expect the set of attachments, the completed reports, and the pending reports to be
  // different than the first set as the real-time clock could go back in time between the
  // generation of the two reports and then the second report would actually be older than the
  // first report and be the one that was pruned, cf. fxb/37067.
  EXPECT_THAT(GetAttachmentsDirContents(), Not(IsEmpty()));
  EXPECT_THAT(GetPendingDirContents(), Not(IsEmpty()));
}

TEST_F(DatabaseTest, Check_DatabaseHasNoOrphanedAttachments) {
  // We generate an orphan attachment and check it's in the database.
  const std::string kOrphanedAttachmentDir = files::JoinPath(attachments_dir_, kCrashpadUUIDString);
  files::CreateDirectory(kOrphanedAttachmentDir);
  ASSERT_THAT(GetAttachmentsDirContents(), ElementsAre(kCrashpadUUIDString));
  ASSERT_TRUE(GetPendingDirContents().empty());

  // Add a crash report.
  UUID local_report_id;
  MakeNewReportOrDie(
      /*attachments=*/{{kAttachmentKey, kAttachmentValue}}, &local_report_id);

  ASSERT_THAT(GetAttachmentsDirContents(), UnorderedElementsAreArray({
                                               std::string(kCrashpadUUIDString),
                                               local_report_id.ToString(),
                                           }));

  ASSERT_THAT(GetPendingDirContents(), UnorderedElementsAreArray({
                                           GetMetadataFilepath(local_report_id),
                                           GetMinidumpFilepath(local_report_id),
                                       }));

  // Check that garbage collection occurs correctly.
  EXPECT_EQ(database_->GarbageCollect(), 0u);

  EXPECT_THAT(GetAttachmentsDirContents(), ElementsAre(local_report_id.ToString()));
}

TEST_F(DatabaseTest, Check_GetUploadReport) {
  // Add a crash report.
  UUID local_report_id;
  MakeNewReportOrDie(
      /*attachments=*/{{kAttachmentKey, kAttachmentValue}}, std::nullopt,
      /*annotations=*/{{kAnnotationKey, kAnnotationValue}}, &local_report_id);

  // Get a report and check its contents are correct.
  auto upload_report = database_->GetUploadReport(local_report_id);
  EXPECT_THAT(upload_report->GetAnnotations(), ElementsAre(Pair(kAnnotationKey, kAnnotationValue)));
  EXPECT_THAT(upload_report->GetAttachments(), ElementsAre(Key(kAttachmentKey)));
}

TEST_F(DatabaseTest, Check_AttachmentsContainMinidump) {
  // Add a crash report with a minidump.
  UUID local_report_id;
  MakeNewReportOrDie(
      /*attachments=*/{{kAttachmentKey, kAttachmentValue}},
      /*minidump=*/"minidump",
      /*annotations=*/{{kAnnotationKey, kAnnotationValue}}, &local_report_id);

  // Get a report and check its contents are correct.
  auto upload_report = database_->GetUploadReport(local_report_id);
  ASSERT_TRUE(upload_report);
  EXPECT_THAT(upload_report->GetAnnotations(), ElementsAre(Pair(kAnnotationKey, kAnnotationValue)));
  EXPECT_THAT(upload_report->GetAttachments(), UnorderedElementsAreArray({
                                                   Key(kAttachmentKey),
                                                   Key("uploadFileMinidump"),
                                               }));
}

TEST_F(DatabaseTest, Check_ReportInCompleted_MarkAsUploaded) {
  // Add a crash report.
  UUID local_report_id;
  MakeNewReportOrDie(&local_report_id);

  auto upload_report = database_->GetUploadReport(local_report_id);
  ASSERT_TRUE(upload_report);

  database_->IncrementUploadAttempt(local_report_id);

  // Mark a report as uploaded and check that it's in completed/.
  ASSERT_TRUE(database_->MarkAsUploaded(std::move(upload_report), "server_report_id"));
  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          CobaltEvent(CrashState::kUploaded),
                                          CobaltEvent(UploadAttemptState::kUploadAttempt, 1u),
                                          CobaltEvent(UploadAttemptState::kUploaded, 1u),
                                      }));

  EXPECT_THAT(GetCompletedDirContents(), UnorderedElementsAreArray({
                                             GetMetadataFilepath(local_report_id),
                                             GetMinidumpFilepath(local_report_id),
                                         }));
}

TEST_F(DatabaseTest, Check_ReportInCompleted_Archive) {
  // Add a crash report.
  UUID local_report_id;
  MakeNewReportOrDie(&local_report_id);

  // Archive a report and check it's in completed/.
  ASSERT_TRUE(database_->Archive(local_report_id));
  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          CobaltEvent(CrashState::kArchived),
                                      }));
}

TEST_F(DatabaseTest, Attempt_GetUploadReport_AfterMarkAsCompleted) {
  // Add a crash report.
  UUID local_report_id;
  MakeNewReportOrDie(
      /*attachments=*/{{kAttachmentKey, kAttachmentValue}}, &local_report_id);

  auto upload_report = database_->GetUploadReport(local_report_id);
  ASSERT_TRUE(upload_report);

  database_->IncrementUploadAttempt(local_report_id);

  // Mark a report as uploaded and check that it's in completed/.
  ASSERT_TRUE(database_->MarkAsUploaded(std::move(upload_report), "server_report_id"));
  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          CobaltEvent(CrashState::kUploaded),
                                          CobaltEvent(UploadAttemptState::kUploadAttempt, 1u),
                                          CobaltEvent(UploadAttemptState::kUploaded, 1u),
                                      }));

  EXPECT_EQ(database_->GetUploadReport(local_report_id), nullptr);
}

TEST_F(DatabaseTest, Attempt_GetUploadReport_AfterArchive) {
  // Add a crash report.
  UUID local_report_id;
  MakeNewReportOrDie(
      /*attachments=*/{{kAttachmentKey, kAttachmentValue}}, &local_report_id);

  ASSERT_TRUE(database_->Archive(local_report_id));
  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          CobaltEvent(CrashState::kArchived),
                                      }));

  EXPECT_EQ(database_->GetUploadReport(local_report_id), nullptr);
}

TEST_F(DatabaseTest, Attempt_GetUploadReport_AfterReportIsPruned) {
  // We set up the database with a max size equivalent to the expected size of a report plus the
  // value of a rather large attachment.
  const uint64_t crash_log_size_in_kb = 2u * kMaxTotalReportsSizeInKb;
  const std::string large_string = GenerateString(crash_log_size_in_kb);
  SetUpDatabase(/*max_size_in_kb=*/kMaxTotalReportsSizeInKb + crash_log_size_in_kb);

  // Add a crash report.
  UUID local_report_id_1;
  MakeNewReportOrDie(
      /*attachments=*/{{kAttachmentKey, large_string}}, &local_report_id_1);

  // Add a crash report.
  UUID local_report_id_2;
  MakeNewReportOrDie(
      /*attachments=*/{{kAttachmentKey, large_string}}, &local_report_id_2);

  // Check that the contents of the new report are present.
  ASSERT_THAT(GetAttachmentsDirContents(), UnorderedElementsAreArray({
                                               local_report_id_1.ToString(),
                                               local_report_id_2.ToString(),
                                           }));

  ASSERT_THAT(GetPendingDirContents(), UnorderedElementsAreArray({
                                           GetMetadataFilepath(local_report_id_1),
                                           GetMinidumpFilepath(local_report_id_1),
                                           GetMetadataFilepath(local_report_id_2),
                                           GetMinidumpFilepath(local_report_id_2),
                                       }));

  // Check that garbage collection occurs correctly.
  EXPECT_EQ(database_->GarbageCollect(), 1u);
  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          CobaltEvent(CrashState::kGarbageCollected),
                                      }));

  // Get the |UUID| of the pruned report and attempt to get a report for it.
  ASSERT_THAT(GetAttachmentsDirContents(),
              IsSubsetOf({local_report_id_1.ToString(), local_report_id_2.ToString()}));

  UUID pruned_report = GetAttachmentsDirContents()[0] == local_report_id_1.ToString()
                           ? local_report_id_2
                           : local_report_id_1;
  EXPECT_EQ(database_->GetUploadReport(pruned_report), nullptr);
}

TEST_F(DatabaseTest, Attempt_Archive_AfterReportIsPruned) {
  // We set up the database with a max size equivalent to the expected size of a report plus the
  // value of a rather large attachment.
  const uint64_t crash_log_size_in_kb = 2u * kMaxTotalReportsSizeInKb;
  const std::string large_string = GenerateString(crash_log_size_in_kb);
  SetUpDatabase(/*max_size_in_kb=*/kMaxTotalReportsSizeInKb + crash_log_size_in_kb);

  // Add a crash report.
  UUID local_report_id_1;
  MakeNewReportOrDie(
      /*attachments=*/{{kAttachmentKey, large_string}}, &local_report_id_1);

  // Add a crash report.
  UUID local_report_id_2;
  MakeNewReportOrDie(
      /*attachments=*/{{kAttachmentKey, large_string}}, &local_report_id_2);

  // Check that the contents of the new report are present.
  ASSERT_THAT(GetAttachmentsDirContents(), UnorderedElementsAreArray({
                                               local_report_id_1.ToString(),
                                               local_report_id_2.ToString(),
                                           }));

  ASSERT_THAT(GetPendingDirContents(), UnorderedElementsAreArray({
                                           GetMetadataFilepath(local_report_id_1),
                                           GetMinidumpFilepath(local_report_id_1),
                                           GetMetadataFilepath(local_report_id_2),
                                           GetMinidumpFilepath(local_report_id_2),
                                       }));

  // Check that garbage collection occurs correctly.
  EXPECT_EQ(database_->GarbageCollect(), 1u);
  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          CobaltEvent(CrashState::kGarbageCollected),
                                      }));

  // Determine the |UUID| of the pruned report and attempt to archive it.
  ASSERT_THAT(GetAttachmentsDirContents(),
              IsSubsetOf({local_report_id_1.ToString(), local_report_id_2.ToString()}));

  UUID pruned_report = GetAttachmentsDirContents()[0] == local_report_id_1.ToString()
                           ? local_report_id_2
                           : local_report_id_1;
  EXPECT_FALSE(database_->Archive(pruned_report));
}

TEST_F(DatabaseTest, Check_InspectTree_ReportUploaded) {
  clock_.Set(kTime);

  // Add a crash report.
  UUID local_report_id;
  MakeNewReportOrDie(&local_report_id);

  // Get the upload report.
  auto upload_report = database_->GetUploadReport(local_report_id);
  ASSERT_TRUE(upload_report);

  // Add the report to Inspect.
  EXPECT_TRUE(info_context_->InspectManager().AddReport("program", local_report_id.ToString()));

  // Mark the report as uploaded and check the Inspect tree.
  database_->IncrementUploadAttempt(local_report_id);
  EXPECT_TRUE(database_->MarkAsUploaded(std::move(upload_report), "server_report_id"));
  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          CobaltEvent(CrashState::kUploaded),
                                          CobaltEvent(UploadAttemptState::kUploadAttempt, 1u),
                                          CobaltEvent(UploadAttemptState::kUploaded, 1u),
                                      }));
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(Contains(AllOf(
                  NodeMatches(NameMatches("reports")),
                  ChildrenMatch(ElementsAre(AllOf(
                      NodeMatches(NameMatches("program")),
                      ChildrenMatch(ElementsAre(AllOf(
                          NodeMatches(AllOf(NameMatches(local_report_id.ToString()),
                                            PropertyList(UnorderedElementsAreArray({
                                                StringIs("creation_time", kTimeStr),
                                                StringIs("final_state", "uploaded"),
                                                UintIs("upload_attempts", 1u),
                                            })))),
                          ChildrenMatch(ElementsAre(NodeMatches(AllOf(
                              NameMatches("crash_server"), PropertyList(UnorderedElementsAreArray({
                                                               StringIs("creation_time", kTimeStr),
                                                               StringIs("id", "server_report_id"),
                                                           }))))))))))))))));
}

TEST_F(DatabaseTest, Check_InspectTree_ReportArchived) {
  clock_.Set(kTime);

  // Add a crash report.
  UUID local_report_id;
  MakeNewReportOrDie(&local_report_id);

  // Add the report to Inspect.
  EXPECT_TRUE(info_context_->InspectManager().AddReport("program", local_report_id.ToString()));

  // Archive the report and check the Inspect tree.
  EXPECT_TRUE(database_->Archive(local_report_id));
  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          CobaltEvent(CrashState::kArchived),
                                      }));
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(Contains(AllOf(
          NodeMatches(NameMatches("reports")),
          ChildrenMatch(ElementsAre(AllOf(
              NodeMatches(NameMatches("program")),
              ChildrenMatch(ElementsAre(AllOf(NodeMatches(AllOf(
                  NameMatches(local_report_id.ToString()), PropertyList(UnorderedElementsAreArray({
                                                               StringIs("creation_time", kTimeStr),
                                                               StringIs("final_state", "archived"),
                                                           }))))))))))))));
}

TEST_F(DatabaseTest, Check_InspectTree_ReportGarbageCollected) {
  // Set up the database with a max size of 0, meaning any reports in the database with size > 0
  // will get garbage collected.
  SetUpDatabase(/*max_size_in_kb=*/0u);
  clock_.Set(kTime);

  // Add a crash report.
  UUID local_report_id;
  MakeNewReportOrDie(
      /*attachments=*/{{kAttachmentKey, kAttachmentValue}}, &local_report_id);

  // Add the report to Inpsect.
  EXPECT_TRUE(info_context_->InspectManager().AddReport("program", local_report_id.ToString()));

  // Check that garbage collection occurs correctly and check the Inspect tree.
  EXPECT_EQ(database_->GarbageCollect(), 1u);
  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          CobaltEvent(CrashState::kGarbageCollected),
                                      }));
  EXPECT_THAT(InspectTree(),
              ChildrenMatch(IsSupersetOf({
                  AllOf(NodeMatches(NameMatches("reports")),
                        ChildrenMatch(
                            ElementsAre(AllOf(NodeMatches(NameMatches("program")),
                                              ChildrenMatch(ElementsAre(AllOf(NodeMatches(AllOf(
                                                  NameMatches(local_report_id.ToString()),
                                                  PropertyList(UnorderedElementsAreArray({
                                                      StringIs("creation_time", kTimeStr),
                                                      StringIs("final_state", "garbage_collected"),
                                                  }))))))))))),
                  AllOf(NodeMatches(AllOf(NameMatches("database"),
                                          PropertyList(UnorderedElementsAreArray({
                                              UintIs("max_crashpad_database_size_in_kb", 0u),
                                              UintIs("num_reports_cleaned", 0u),
                                              UintIs("num_reports_pruned", 1u),
                                          })))),
                        ChildrenMatch(IsEmpty())),

              })));
}

TEST_F(DatabaseTest, Check_CobaltLogsInitializeFailure) {
  CrashpadDatabaseGremlin gremlin(kCrashpadDatabasePath);

  gremlin.BreakInitialize();

  EXPECT_FALSE(SetUpDatabase(/*max_size_in_kb=*/1024u));

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          CobaltEvent(CrashpadFunctionError::kInitializeDatabase),
                                      }));
}

TEST_F(DatabaseTest, Check_CobaltLogsPrepareNewCrashReportFailure) {
  UUID uuid;
  CrashpadDatabaseGremlin gremlin(kCrashpadDatabasePath);

  gremlin.BreakPrepareNewCrashReport();

  EXPECT_FALSE(database_->MakeNewReport(/*attachments=*/{}, /*minidump=*/std::nullopt,
                                        /*annotations=*/{}, &uuid));
  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  CobaltEvent(CrashpadFunctionError::kPrepareNewCrashReport),
              }));
}

TEST_F(DatabaseTest, Check_CobaltLogsFinishedWritingCrashReportFailure) {
  UUID uuid;
  CrashpadDatabaseGremlin gremlin(kCrashpadDatabasePath);

  gremlin.BreakFinishedWritingCrashReport();

  EXPECT_FALSE(database_->MakeNewReport(/*attachments=*/{}, /*minidump=*/std::nullopt,
                                        /*annotations=*/{}, &uuid));
  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  CobaltEvent(CrashpadFunctionError::kFinishedWritingCrashReport),
              }));
}

TEST_F(DatabaseTest, Check_CobaltLogsGetReportForUploadingFailure) {
  UUID uuid;
  CrashpadDatabaseGremlin gremlin(kCrashpadDatabasePath);

  MakeNewReportOrDie(&uuid);

  gremlin.DeletePendingReport(uuid);

  EXPECT_FALSE(database_->GetUploadReport(uuid));

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(),
              UnorderedElementsAreArray({
                  CobaltEvent(CrashpadFunctionError::kGetReportForUploading),
              }));
}

TEST_F(DatabaseTest, Check_CobaltLogsRecordUploadCompleteFailure) {
  UUID uuid;
  std::unique_ptr<UploadReport> upload_report;
  CrashpadDatabaseGremlin gremlin(kCrashpadDatabasePath);

  MakeNewReportOrDie(&uuid);
  upload_report = database_->GetUploadReport(uuid);

  gremlin.BreakRecordUploadComplete();

  EXPECT_FALSE(database_->MarkAsUploaded(std::move(upload_report), "server_report_id"));

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          CobaltEvent(CrashState::kUploaded),
                                          CobaltEvent(UploadAttemptState::kUploaded, 0u),
                                          CobaltEvent(CrashpadFunctionError::kRecordUploadComplete),
                                      }));
}

TEST_F(DatabaseTest, Check_CobaltLogsSkipReportUploadFailure) {
  UUID uuid;
  CrashpadDatabaseGremlin gremlin(kCrashpadDatabasePath);

  MakeNewReportOrDie(&uuid);

  gremlin.BreakSkipReportUpload();

  EXPECT_FALSE(database_->Archive(uuid));

  RunLoopUntilIdle();
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          CobaltEvent(CrashState::kArchived),
                                          CobaltEvent(CrashpadFunctionError::kSkipReportUpload),
                                      }));
}

}  // namespace
}  // namespace feedback
