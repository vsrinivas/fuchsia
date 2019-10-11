// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/database.h"

#include <fuchsia/mem/cpp/fidl.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/test/test_settings.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using crashpad::UUID;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::UnorderedElementsAreArray;

constexpr uint64_t kMaxTotalReportsSizeInKb = 1024u;

constexpr char kCrashpadAttachmentsDir[] = "attachments";
constexpr char kCrashpadCompletedDir[] = "completed";
constexpr char kCrashpadPendingDir[] = "pending";

constexpr char kCrashReportExtension[] = "dmp";
constexpr char kMetadataExtension[] = "meta";
constexpr char kAttachmentKey[] = "attachment.key";
constexpr char kAttachmentValue[] = "attachment.value";

constexpr char kCrashpadUUIDString[] = "00000000-0000-0000-0000-000000000001";

class DatabaseTest : public ::testing::Test {
 protected:
  void SetUpDatabase(CrashpadDatabaseConfig config) {
    auto new_database = Database::TryCreate(config);
    FXL_CHECK(new_database) << "Error creating database";
    database_ = std::move(new_database);
    attachments_dir_ = files::JoinPath(database_path_.path(), kCrashpadAttachmentsDir);
    completed_dir_ = files::JoinPath(database_path_.path(), kCrashpadCompletedDir);
    pending_dir_ = files::JoinPath(database_path_.path(), kCrashpadPendingDir);
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
  files::ScopedTempDir database_path_;
  std::unique_ptr<Database> database_;
  std::string attachments_dir_;

 private:
  std::string completed_dir_;
  std::string pending_dir_;
};

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

TEST_F(DatabaseTest, Check_DatabaseIsEmpty_OnPruneDatabaseWithZeroSize) {
  // Set up the database with a max size of 0, meaning any reports in the database with size > 0
  // will get garbage collected.
  SetUpDatabase(CrashpadDatabaseConfig{/*path=*/database_path_.path(),
                                       /*max_size_in_kb=*/0u});
  // Add a crash report.
  UUID local_report_id;
  ASSERT_TRUE(database_->MakeNewReport(
      /*attachments=*/CreateAttachments({{kAttachmentKey, kAttachmentValue}}), std::nullopt,
      /*annotations=*/{}, &local_report_id));

  ASSERT_THAT(GetAttachmentsDirContents(), UnorderedElementsAreArray({
                                               local_report_id.ToString(),
                                           }));

  ASSERT_THAT(GetPendingDirContents(), UnorderedElementsAreArray({
                                           GetMetadataFilepath(local_report_id),
                                           GetMinidumpFilepath(local_report_id),
                                       }));

  // Check that garbage collection occurs correctly.
  EXPECT_EQ(database_->GarbageCollect(), 1u);

  EXPECT_TRUE(GetAttachmentsDirContents().empty());
  EXPECT_TRUE(GetPendingDirContents().empty());
}

TEST_F(DatabaseTest, Check_DatabaseHasOnlyOneReport_OnPruneDatabaseWithSizeForOnlyOneReport) {
  // We set up the database with a max size equivalent to the expected size of a report plus the
  // value of a rather large attachment.
  const uint64_t crash_log_size_in_kb = 2u * kMaxTotalReportsSizeInKb;
  const std::string large_string = GenerateString(crash_log_size_in_kb);
  SetUpDatabase(
      CrashpadDatabaseConfig{/*path=*/database_path_.path(),
                             /*max_size_in_kb=*/kMaxTotalReportsSizeInKb + crash_log_size_in_kb});

  // Add a crash report.
  UUID local_report_id_1;
  ASSERT_TRUE(database_->MakeNewReport(
      /*attachments=*/CreateAttachments({{kAttachmentKey, large_string}}), std::nullopt,
      /*annotations=*/{}, &local_report_id_1));

  // Check that the contents of the new report are present.
  ASSERT_THAT(GetAttachmentsDirContents(), ElementsAre(local_report_id_1.ToString()));
  ASSERT_THAT(GetPendingDirContents(), UnorderedElementsAreArray({
                                           GetMetadataFilepath(local_report_id_1),
                                           GetMinidumpFilepath(local_report_id_1),
                                       }));

  // Add a crash report.
  UUID local_report_id_2;
  ASSERT_TRUE(database_->MakeNewReport(
      /*attachments=*/CreateAttachments({{kAttachmentKey, large_string}}), std::nullopt,
      /*annotations=*/{}, &local_report_id_2));

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

  // We cannot expect the set of attachments, the completed reports, and the pending reports to be
  // different than the first set as the real-time clock could go back in time between the
  // generation of the two reports and then the second report would actually be older than the first
  // report and be the one that was pruned, cf. fxb/37067.
  EXPECT_THAT(GetAttachmentsDirContents(), Not(IsEmpty()));
  EXPECT_THAT(GetPendingDirContents(), Not(IsEmpty()));
}

TEST_F(DatabaseTest, Check_DatabaseHasNoOrphanedAttachments) {
  SetUpDatabase(CrashpadDatabaseConfig{/*path=*/database_path_.path(),
                                       /*max_size_in_kb=*/kMaxTotalReportsSizeInKb});
  // We generate an orphan attachment and check it's in the database.
  const std::string kOrphanedAttachmentDir = files::JoinPath(attachments_dir_, kCrashpadUUIDString);
  files::CreateDirectory(kOrphanedAttachmentDir);
  ASSERT_THAT(GetAttachmentsDirContents(), ElementsAre(kCrashpadUUIDString));
  ASSERT_TRUE(GetPendingDirContents().empty());

  // Add a crash report.
  UUID local_report_id;
  ASSERT_TRUE(database_->MakeNewReport(
      /*attachments=*/CreateAttachments({{kAttachmentKey, kAttachmentValue}}), std::nullopt,
      /*annotations=*/{}, &local_report_id));

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
