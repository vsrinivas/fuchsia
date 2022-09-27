// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/report_store.h"

#include <lib/syslog/cpp/macros.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/annotations/annotation_manager.h"
#include "src/developer/forensics/testing/scoped_memfs_manager.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/sized_data.h"
#include "src/developer/forensics/utils/storage_size.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/timekeeper/test_clock.h"
#include "third_party/rapidjson/include/rapidjson/document.h"

namespace forensics {
namespace crash_reports {
namespace {

using inspect::testing::ChildrenMatch;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;
using inspect::testing::UintIs;
using ::testing::IsEmpty;
using ::testing::IsSupersetOf;
using ::testing::UnorderedElementsAreArray;

SizedData MakeSizedData(const std::string& content) {
  return SizedData(content.begin(), content.end());
}

class ReportStoreTest : public UnitTestFixture {
 public:
  ReportStoreTest() : annotation_manager_(dispatcher(), {}) {
    MakeNewStore(StorageSize::Megabytes(1));
  }

 protected:
  void MakeNewStore(const StorageSize max_tmp_size,
                    const StorageSize max_cache_size = StorageSize::Bytes(0),
                    const StorageSize max_archives_size = StorageSize::Megabytes(1)) {
    info_context_ =
        std::make_shared<InfoContext>(&InspectRoot(), &clock_, dispatcher(), services());
    report_store_ = std::make_unique<ReportStore>(
        &tags_, info_context_, &annotation_manager_,
        /*temp_root=*/ReportStore::Root{tmp_dir_.path(), max_tmp_size},
        /*persistent_root=*/ReportStore::Root{cache_dir_.path(), max_cache_size},
        files::JoinPath(tmp_dir_.path(), "garbage_collected_snapshots.txt"), max_archives_size);
  }

  std::optional<ReportId> Add(const std::string& program_shortname,
                              std::vector<ReportId>* garbage_collected_reports) {
    return Add(program_shortname, {}, {}, "", std::nullopt, garbage_collected_reports);
  }

  std::optional<ReportId> Add(const std::string& program_shortname,
                              const std::map<std::string, std::string>& annotations,
                              const std::map<std::string, std::string>& attachments,
                              const std::string& snapshot_uuid,
                              const std::optional<std::string>& minidump,
                              std::vector<ReportId>* garbage_collected_reports) {
    std::map<std::string, SizedData> attachments_data;
    for (const auto& [k, v] : attachments) {
      attachments_data.emplace(k, MakeSizedData(v));
    }

    std::optional<SizedData> minidump_data;
    if (minidump.has_value()) {
      minidump_data = MakeSizedData(minidump.value());
    }

    const ReportId report_id = next_report_id_++;
    auto report = Report(report_id, program_shortname, AnnotationMap(annotations),
                         std::move(attachments_data), snapshot_uuid, std::move(minidump_data));

    if (report_store_->Add(std::move(report), garbage_collected_reports)) {
      return report_id;
    }

    return std::nullopt;
  }

  bool Get(const ReportId& id, std::string* program_shortname,
           std::map<std::string, std::string>* annotations,
           std::map<std::string, std::string>* attachments, std::string* snapshot_uuid,
           std::optional<std::string>* minidump) {
    if (!report_store_->Contains(id)) {
      return false;
    }
    const auto report = report_store_->Get(id);

    *program_shortname = report.ProgramShortname();
    *annotations = report.Annotations().Raw();
    for (const auto& [filename, attachment] : report.Attachments()) {
      (*attachments)[filename] = std::string(attachment.begin(), attachment.end());
    }
    *snapshot_uuid = report.SnapshotUuid();
    if (report.Minidump().has_value()) {
      const auto& value = report.Minidump().value();
      *minidump = std::string(value.begin(), value.end());
    } else {
      *minidump = std::nullopt;
    }
    return true;
  }

  bool ReadTmp(const std::string& program_shortname, const ReportId& id,
               std::map<std::string, std::string>* annotations_out,
               std::map<std::string, std::string>* attachments_out,
               std::optional<std::string>* snapshot_uuid_out,
               std::optional<std::string>* minidump_out) {
    return Read(tmp_dir_.path(), program_shortname, id, annotations_out, attachments_out,
                snapshot_uuid_out, minidump_out);
  }

  bool ReadCache(const std::string& program_shortname, const ReportId& id,
                 std::map<std::string, std::string>* annotations_out,
                 std::map<std::string, std::string>* attachments_out,
                 std::optional<std::string>* snapshot_uuid_out,
                 std::optional<std::string>* minidump_out) {
    return Read(cache_dir_.path(), program_shortname, id, annotations_out, attachments_out,
                snapshot_uuid_out, minidump_out);
  }

  bool Read(const std::string& root_dir, const std::string& program_shortname, const ReportId& id,
            std::map<std::string, std::string>* annotations_out,
            std::map<std::string, std::string>* attachments_out,
            std::optional<std::string>* snapshot_uuid_out,
            std::optional<std::string>* minidump_out) {
    const std::string id_str = std::to_string(id);
    const std::string path = files::JoinPath(files::JoinPath(root_dir, program_shortname), id_str);

    auto ReadFile = [&path](const std::string& filename, std::string* out) {
      return files::ReadFileToString(files::JoinPath(path, filename), out);
    };

    std::vector<std::string> files;
    if (!files::ReadDirContents(path, &files)) {
      return false;
    }

    std::map<std::string, std::string> annotations;
    std::map<std::string, std::string> attachments;

    std::string content;
    for (const auto& file : files) {
      if (file == ".") {
        continue;
      } else if (file == "annotations.json") {
        if (!ReadFile(file, &content)) {
          return false;
        }
        rapidjson::Document json;
        json.Parse(content);
        for (const auto& annotation : json.GetObject()) {
          annotations[annotation.name.GetString()] = annotation.value.GetString();
        }
      } else {
        if (!ReadFile(file, &content)) {
          return false;
        }
        attachments[file] = content;
      }
    }

    // Take the snapshot uuid from the set of attachments, if one is present.
    const std::string snapshot_uuid_filename = "snapshot_uuid.txt";
    if (attachments.find(snapshot_uuid_filename) != attachments.end()) {
      *snapshot_uuid_out = attachments.at(snapshot_uuid_filename);
      attachments.erase(snapshot_uuid_filename);
    } else {
      *snapshot_uuid_out = std::nullopt;
    }

    const std::string minidump_filename = "minidump.dmp";
    if (attachments.find(minidump_filename) != attachments.end()) {
      *minidump_out = attachments.at(minidump_filename);
      attachments.erase(minidump_filename);
    } else {
      *minidump_out = std::nullopt;
    }

    *attachments_out = attachments;
    *annotations_out = annotations;

    return true;
  }

  std::vector<std::string> GetProgramShortnames() {
    std::vector<std::string> programs;
    files::ReadDirContents(tmp_dir_.path(), &programs);

    programs.erase(std::remove_if(programs.begin(), programs.end(),
                                  [](const std::string& filename) { return filename == "."; }),
                   programs.end());
    return programs;
  }
  timekeeper::TestClock clock_;
  LogTags tags_;
  std::shared_ptr<InfoContext> info_context_;

  files::ScopedTempDir tmp_dir_;
  files::ScopedTempDir cache_dir_;
  std::unique_ptr<ReportStore> report_store_;
  feedback::AnnotationManager annotation_manager_;

  ReportId next_report_id_{0};
};

TEST_F(ReportStoreTest, Succeed_AddDefaultsToCache) {
  const std::string expected_program_shortname = "program_shortname";

  const std::map<std::string, std::string> expected_annotations = {
      {"annotation0.cc", "annotation_value0"},
      {"annotation1.txt", "annotation_value1"},
      {"annotation2.zip", "annotation_value2"},
  };

  const std::map<std::string, std::string> expected_attachments = {
      {"attachment_key0", "attachment_value0"},
      {"attachment_key1", "attachment_value1"},
      {"attachment_key2", "attachment_value2"},
  };

  const std::string expected_snapshot_uuid = "snapshot_uuid";
  const std::string expected_minidump = "mindump";

  size_t expected_report_size = expected_snapshot_uuid.size() + expected_minidump.size();
  for (const auto& [k, v] : expected_annotations) {
    expected_report_size += k.size() + v.size() + 11 /*json formatting*/;
  }
  expected_report_size += 5 /*json formatting*/;
  for (const auto& [k, v] : expected_attachments) {
    expected_report_size += v.size();
  }

  MakeNewStore(/*max_tmp_size=*/StorageSize::Bytes(expected_report_size),
               /*max_cache_size=*/StorageSize::Bytes(expected_report_size));

  std::map<std::string, std::string> annotations;
  std::map<std::string, std::string> attachments;
  std::optional<std::string> snapshot_uuid;
  std::optional<std::string> minidump;

  // The first report should be placed under the cache directory.
  std::vector<ReportId> garbage_collected_reports;
  const auto cache_id = Add(expected_program_shortname, expected_annotations, expected_attachments,
                            expected_snapshot_uuid, expected_minidump, &garbage_collected_reports);
  EXPECT_TRUE(cache_id.has_value());
  EXPECT_TRUE(garbage_collected_reports.empty());

  ASSERT_TRUE(report_store_->Contains(cache_id.value()));
  ASSERT_TRUE(ReadCache(expected_program_shortname, cache_id.value(), &annotations, &attachments,
                        &snapshot_uuid, &minidump));

  EXPECT_EQ(expected_annotations, annotations);
  EXPECT_EQ(expected_attachments, attachments);
  ASSERT_TRUE(snapshot_uuid.has_value());
  EXPECT_EQ(expected_snapshot_uuid, snapshot_uuid.value());
  ASSERT_TRUE(minidump.has_value());
  EXPECT_EQ(expected_minidump, minidump.value());

  // The second report should be placed under the tmp directory.
  const auto tmp_id = Add(expected_program_shortname, expected_annotations, expected_attachments,
                          expected_snapshot_uuid, expected_minidump, &garbage_collected_reports);
  EXPECT_TRUE(tmp_id.has_value());
  EXPECT_TRUE(garbage_collected_reports.empty());

  ASSERT_TRUE(report_store_->Contains(tmp_id.value()));
  ASSERT_TRUE(ReadTmp(expected_program_shortname, tmp_id.value(), &annotations, &attachments,
                      &snapshot_uuid, &minidump));

  EXPECT_EQ(expected_annotations, annotations);
  EXPECT_EQ(expected_attachments, attachments);
  ASSERT_TRUE(snapshot_uuid.has_value());
  EXPECT_EQ(expected_snapshot_uuid, snapshot_uuid.value());
  ASSERT_TRUE(minidump.has_value());
  EXPECT_EQ(expected_minidump, minidump.value());
}

TEST_F(ReportStoreTest, Succeed_Get) {
  const std::string expected_program_shortname = "program_shortname";

  const std::map<std::string, std::string> expected_annotations = {
      {"annotation0.cc", "annotation_value0"},
      {"annotation1.txt", "annotation_value1"},
      {"annotation2.zip", "annotation_value2"},
  };

  const std::map<std::string, std::string> expected_attachments = {
      {"attachment_key0", "attachment_value0"},
      {"attachment_key1", "attachment_value1"},
      {"attachment_key2", "attachment_value2"},
  };

  const std::string expected_snapshot_uuid = "snapshot_uuid";

  const std::string expected_minidump = "mindump";

  std::vector<ReportId> garbage_collected_reports;
  const auto id = Add(expected_program_shortname, expected_annotations, expected_attachments,
                      expected_snapshot_uuid, expected_minidump, &garbage_collected_reports);
  ASSERT_TRUE(id.has_value());
  EXPECT_TRUE(garbage_collected_reports.empty());

  std::string program_shortname;
  std::map<std::string, std::string> annotations;
  std::map<std::string, std::string> attachments;
  std::string snapshot_uuid;
  std::optional<std::string> minidump;

  ASSERT_TRUE(
      Get(id.value(), &program_shortname, &annotations, &attachments, &snapshot_uuid, &minidump));

  EXPECT_EQ(expected_program_shortname, program_shortname);
  EXPECT_EQ(expected_annotations, annotations);
  EXPECT_EQ(expected_attachments, attachments);
  EXPECT_EQ(expected_snapshot_uuid, snapshot_uuid);
  ASSERT_TRUE(minidump.has_value());
  EXPECT_EQ(expected_minidump, minidump.value());
}

TEST_F(ReportStoreTest, Fail_ReservedAttachmentKey) {
  std::vector<ReportId> garbage_collected_reports;
  EXPECT_FALSE(Add("program_shortname", /*annotations=*/{},
                   /*attachments=*/{{"annotations.json", ""}},
                   /*snapshot_uuid=*/"",
                   /*minidump=*/std::nullopt, &garbage_collected_reports)
                   .has_value());
  EXPECT_TRUE(garbage_collected_reports.empty());
  EXPECT_FALSE(Add("program_shortname", /*annotations=*/{}, /*attachments=*/{{"minidump.dmp", ""}},
                   /*snapshot_uuid=*/"",
                   /*minidump=*/std::nullopt, &garbage_collected_reports)
                   .has_value());
  EXPECT_TRUE(garbage_collected_reports.empty());
  EXPECT_FALSE(Add("program_shortname", /*annotations=*/{},
                   /*attachments=*/{{"snapshot_uuid.txt", ""}}, /*snapshot_uuid=*/"",
                   /*minidump=*/std::nullopt, &garbage_collected_reports)
                   .has_value());
  EXPECT_TRUE(garbage_collected_reports.empty());
}

TEST_F(ReportStoreTest, Succeed_Remove) {
  std::vector<ReportId> garbage_collected_reports;
  const auto id = Add("program_shortname", /*annotations=*/{}, /*attachments=*/{},
                      /*snapshot_uuid=*/"",
                      /*minidump=*/std::nullopt, &garbage_collected_reports);
  EXPECT_TRUE(id.has_value());
  EXPECT_TRUE(garbage_collected_reports.empty());
  ASSERT_TRUE(report_store_->Contains(id.value()));

  report_store_->Remove(id.value());
  EXPECT_FALSE(report_store_->Contains(id.value()));
  EXPECT_TRUE(GetProgramShortnames().empty());
}

TEST_F(ReportStoreTest, NoCacheGarbageCollection) {
  // Cache only has space for one report.
  MakeNewStore(/*tmp_max_size=*/StorageSize::Bytes(0),
               /*cache_max_size=*/
               StorageSize::Bytes(2u /*the empty annotations.json*/));
  std::vector<ReportId> garbage_collected_reports;

  EXPECT_TRUE(Add("program_name_1", &garbage_collected_reports).has_value());
  EXPECT_FALSE(Add("program_name_2", &garbage_collected_reports).has_value());
}

TEST_F(ReportStoreTest, Succeed_TmpGarbageCollection) {
  // To make this test easier to understand, the below table shows when and why each report is being
  // garbage collected.
  //
  // report_id | program name  | garbage collection order | garbage collection reason
  // ----------------------------------------------------------------------------------------------
  // id1 | program_name1 |            n/a           |         n/a
  // id2 | program_name2 |             3            | oldest report in the store
  // id3 | program_name3 |             1            | program_name3 has the most reports
  // id4 | program_name3 |             2            | program_name3 has the most reports
  // id5 | program_name3 |            n/a           |         n/a
  // id6 | program_name3 |            n/a           |         n/a
  // id7 | program_name2 |            n/a           |         n/a
  // id8 | program_name4 |            n/a           |         n/a

  // We set up the store so it can hold four reports at most.
  MakeNewStore(4 * StorageSize::Bytes(2u /*the empty annotations.json*/));
  std::vector<ReportId> garbage_collected_reports;

  const auto id1 = Add("program_name1", &garbage_collected_reports);
  const auto id2 = Add("program_name2", &garbage_collected_reports);
  const auto id3 = Add("program_name3", &garbage_collected_reports);
  const auto id4 = Add("program_name3", &garbage_collected_reports);

  // Add a report to force garbage collection of the oldest report for program_name3
  const auto id5 = Add("program_name3", &garbage_collected_reports);
  EXPECT_THAT(garbage_collected_reports, UnorderedElementsAreArray({id3.value()}));
  EXPECT_FALSE(report_store_->Contains(id3.value()));

  // Add a report to force garbage collection of the oldest report for program_name3
  const auto id6 = Add("program_name3", &garbage_collected_reports);
  EXPECT_THAT(garbage_collected_reports, UnorderedElementsAreArray({id4.value()}));
  EXPECT_FALSE(report_store_->Contains(id4.value()));

  // Remove the report for program_name1 from the store and add a report for program_name2 so both
  // program_name2 and program_name3 have 2 reports in the store.
  EXPECT_TRUE(report_store_->Remove(id1.value()));
  const auto id7 = Add("program_name2", &garbage_collected_reports);
  EXPECT_TRUE(garbage_collected_reports.empty());

  // Add a report to force garbage collection of the oldest report between program_nam2 and
  // program_name3.
  const auto id8 = Add("program_name4", &garbage_collected_reports);
  EXPECT_THAT(garbage_collected_reports, UnorderedElementsAreArray({id2.value()}));
  EXPECT_FALSE(report_store_->Contains(id2.value()));

  EXPECT_THAT(report_store_->GetReports(), UnorderedElementsAreArray({
                                               id5.value(),
                                               id6.value(),
                                               id7.value(),
                                               id8.value(),
                                           }));
  EXPECT_THAT(GetProgramShortnames(), UnorderedElementsAreArray({
                                          "program_name2",
                                          "program_name3",
                                          "program_name4",
                                      }));
}

TEST_F(ReportStoreTest, Succeed_TmpGarbageCollectionMultipleCollected) {
  // We set up the store so it can hold two empty reports at most.
  MakeNewStore(2 * StorageSize::Bytes(2u /*the empty annotations.json*/));
  std::vector<ReportId> garbage_collected_reports;

  const auto id1 = Add("program_name1", &garbage_collected_reports);
  const auto id2 = Add("program_name2", &garbage_collected_reports);

  // Construct a slightly larger report (one byte added for "m" in minidump) to ensure both previous
  // reports are garbage collected, since this report won't fit otherwise.
  const auto id3 = Add("program_name3", {}, {}, "", "m", &garbage_collected_reports);
  EXPECT_THAT(garbage_collected_reports, UnorderedElementsAreArray({id1.value(), id2.value()}));
  EXPECT_FALSE(report_store_->Contains(id1.value()));
  EXPECT_FALSE(report_store_->Contains(id2.value()));
  EXPECT_TRUE(report_store_->Contains(id3.value()));

  EXPECT_THAT(report_store_->GetReports(), UnorderedElementsAreArray({
                                               id3.value(),
                                           }));
  EXPECT_THAT(GetProgramShortnames(), UnorderedElementsAreArray({
                                          "program_name3",
                                      }));
}

TEST_F(ReportStoreTest, Succeed_RebuildsMetadata) {
  const std::string expected_program_shortname = "program_shortname";

  const std::map<std::string, std::string> expected_annotations = {
      {"annotation_key0", "annotation_value0"},
      {"annotation_key1", "annotation_value1"},
      {"annotation_key2", "annotation_value2"},
  };

  const std::map<std::string, std::string> expected_attachments = {
      {"attachment_key0", "attachment_value0"},
      {"attachment_key1", "attachment_value1"},
      {"attachment_key2", "attachment_value2"},
  };

  const std::string expected_snapshot_uuid = "snapshot_uuid";

  const std::string expected_minidump = "mindump";

  std::vector<ReportId> ids;
  std::vector<ReportId> garbage_collected_reports;
  for (size_t i = 0; i < 5u; ++i) {
    const auto id = Add(expected_program_shortname, expected_annotations, expected_attachments,
                        expected_snapshot_uuid, expected_minidump, &garbage_collected_reports);
    ASSERT_TRUE(id.has_value());
    ids.push_back(id.value());
    EXPECT_TRUE(garbage_collected_reports.empty());
  }

  MakeNewStore(StorageSize::Megabytes(1));

  for (const auto& id : ids) {
    std::string program_shortname;
    std::map<std::string, std::string> annotations;
    std::map<std::string, std::string> attachments;
    std::string snapshot_uuid;
    std::optional<std::string> minidump;

    ASSERT_TRUE(report_store_->Contains(id));
    ASSERT_TRUE(Get(id, &program_shortname, &annotations, &attachments, &snapshot_uuid, &minidump));

    EXPECT_EQ(expected_program_shortname, program_shortname);
    EXPECT_EQ(expected_annotations, annotations);
    EXPECT_EQ(expected_attachments, attachments);
    EXPECT_EQ(expected_snapshot_uuid, snapshot_uuid);
    ASSERT_TRUE(minidump.has_value());
    EXPECT_EQ(expected_minidump, minidump.value());
  }

  // Check the next report added has the expected id.
  const auto id = Add(expected_program_shortname, expected_attachments, expected_attachments,
                      expected_snapshot_uuid, expected_minidump, &garbage_collected_reports);
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(id.value(), ids.back() + 1u);
  EXPECT_TRUE(garbage_collected_reports.empty());
}

TEST_F(ReportStoreTest, Succeed_RebuildCleansEmptyDirectories) {
  std::vector<ReportId> ids;
  std::vector<ReportId> garbage_collected_reports;
  for (size_t i = 0; i < 5u; ++i) {
    const auto id = Add("program_shortname", /*annotations=*/{}, /*attachments=*/{},
                        /*snapshot_uuid=*/"snapshot_uuid",
                        /*minidump=*/"minidump", &garbage_collected_reports);
    ASSERT_TRUE(id.has_value());
    ids.push_back(id.value());
    EXPECT_TRUE(garbage_collected_reports.empty());
  }

  const std::string empty_dir = files::JoinPath(tmp_dir_.path(), "empty");
  ASSERT_TRUE(files::CreateDirectory(empty_dir));

  MakeNewStore(StorageSize::Megabytes(1));

  EXPECT_FALSE(files::IsDirectory(empty_dir));
}

TEST_F(ReportStoreTest, UsesTmpUntilPersistentReady) {
  const std::string expected_program_shortname = "program_shortname";

  const std::map<std::string, std::string> expected_annotations = {
      {"annotation0.cc", "annotation_value0"},
      {"annotation1.txt", "annotation_value1"},
      {"annotation2.zip", "annotation_value2"},
  };

  const std::map<std::string, std::string> expected_attachments = {
      {"attachment_key0", "attachment_value0"},
      {"attachment_key1", "attachment_value1"},
      {"attachment_key2", "attachment_value2"},
  };

  const std::string expected_snapshot_uuid = "snapshot_uuid";
  const std::string expected_minidump = "mindump";

  size_t expected_report_size = expected_snapshot_uuid.size() + expected_minidump.size();
  for (const auto& [k, v] : expected_annotations) {
    expected_report_size += k.size() + v.size() + 11 /*json formatting*/;
  }
  expected_report_size += 5 /*json formatting*/;
  for (const auto& [k, v] : expected_attachments) {
    expected_report_size += v.size();
  }

  // Use directory that |scoped_mem_fs| can create using ScopedMemFsManager::Create, but |store_|
  // can't create using files::CreateDirectory
  const std::string cache_root = "/cache/delayed/path";
  testing::ScopedMemFsManager scoped_mem_fs;
  report_store_ = std::make_unique<ReportStore>(
      &tags_, info_context_, &annotation_manager_,
      /*temp_root=*/ReportStore::Root{tmp_dir_.path(), StorageSize::Bytes(expected_report_size)},
      /*persistent_root=*/ReportStore::Root{cache_root, StorageSize::Bytes(expected_report_size)},
      files::JoinPath(tmp_dir_.path(), "garbage_collected_snapshots.txt"),
      StorageSize::Bytes(expected_report_size));

  std::map<std::string, std::string> annotations;
  std::map<std::string, std::string> attachments;
  std::optional<std::string> snapshot_uuid;
  std::optional<std::string> minidump;

  // The first report should be placed under the tmp directory because the cache directory isn't
  // ready.
  std::vector<ReportId> garbage_collected_reports;
  const auto tmp_id = Add(expected_program_shortname, expected_annotations, expected_attachments,
                          expected_snapshot_uuid, expected_minidump, &garbage_collected_reports);

  ASSERT_TRUE(report_store_->Contains(tmp_id.value()));
  ASSERT_TRUE(ReadTmp(expected_program_shortname, tmp_id.value(), &annotations, &attachments,
                      &snapshot_uuid, &minidump));

  EXPECT_EQ(expected_annotations, annotations);
  EXPECT_EQ(expected_attachments, attachments);
  ASSERT_TRUE(snapshot_uuid.has_value());
  EXPECT_EQ(expected_snapshot_uuid, snapshot_uuid.value());
  ASSERT_TRUE(minidump.has_value());
  EXPECT_EQ(expected_minidump, minidump.value());

  // Create the cache directory so it can be used for the next report.
  scoped_mem_fs.Create(cache_root);

  // The second report should be placed under the cache directory.
  const auto cache_id = Add(expected_program_shortname, expected_annotations, expected_attachments,
                            expected_snapshot_uuid, expected_minidump, &garbage_collected_reports);
  EXPECT_TRUE(cache_id.has_value());
  EXPECT_TRUE(garbage_collected_reports.empty());

  ASSERT_TRUE(report_store_->Contains(cache_id.value()));
  ASSERT_TRUE(Read(cache_root, expected_program_shortname, cache_id.value(), &annotations,
                   &attachments, &snapshot_uuid, &minidump));

  EXPECT_EQ(expected_annotations, annotations);
  EXPECT_EQ(expected_attachments, attachments);
  ASSERT_TRUE(snapshot_uuid.has_value());
  EXPECT_EQ(expected_snapshot_uuid, snapshot_uuid.value());
  ASSERT_TRUE(minidump.has_value());
  EXPECT_EQ(expected_minidump, minidump.value());
}

TEST_F(ReportStoreTest, FallbackToTmp) {
  const std::string expected_program_shortname = "program_shortname";

  const std::map<std::string, std::string> expected_annotations = {
      {"annotation0.cc", "annotation_value0"},
      {"annotation1.txt", "annotation_value1"},
      {"annotation2.zip", "annotation_value2"},
  };

  const std::map<std::string, std::string> expected_attachments = {
      {"attachment_key0", "attachment_value0"},
      {"attachment_key1", "attachment_value1"},
      {"attachment_key2", "attachment_value2"},
  };

  const std::string expected_snapshot_uuid = "snapshot_uuid";
  const std::string expected_minidump = "mindump";

  size_t expected_report_size = expected_snapshot_uuid.size() + expected_minidump.size();
  for (const auto& [k, v] : expected_annotations) {
    expected_report_size += k.size() + v.size() + 11 /*json formatting*/;
  }
  expected_report_size += 5 /*json formatting*/;
  for (const auto& [k, v] : expected_attachments) {
    expected_report_size += v.size();
  }

  MakeNewStore(StorageSize::Bytes(expected_report_size), StorageSize::Bytes(expected_report_size));

  // Create a file under the cache directory where the next report directory should be created.
  const std::string program_path = files::JoinPath(cache_dir_.path(), expected_program_shortname);
  ASSERT_TRUE(files::CreateDirectory(program_path));
  const std::string report_path = files::JoinPath(program_path, std::to_string(next_report_id_));
  ASSERT_TRUE(files::WriteFile(report_path, "n/a"));

  std::map<std::string, std::string> annotations;
  std::map<std::string, std::string> attachments;
  std::optional<std::string> snapshot_uuid;
  std::optional<std::string> minidump;

  // The first report should be placed under the tmp directory because the cache directory isn't
  // ready.
  std::vector<ReportId> garbage_collected_reports;
  const auto tmp_id = Add(expected_program_shortname, expected_annotations, expected_attachments,
                          expected_snapshot_uuid, expected_minidump, &garbage_collected_reports);

  ASSERT_TRUE(report_store_->Contains(tmp_id.value()));
  ASSERT_TRUE(ReadTmp(expected_program_shortname, tmp_id.value(), &annotations, &attachments,
                      &snapshot_uuid, &minidump));

  EXPECT_EQ(expected_annotations, annotations);
  EXPECT_EQ(expected_attachments, attachments);
  ASSERT_TRUE(snapshot_uuid.has_value());
  EXPECT_EQ(expected_snapshot_uuid, snapshot_uuid.value());
  ASSERT_TRUE(minidump.has_value());
  EXPECT_EQ(expected_minidump, minidump.value());
}

TEST_F(ReportStoreTest, Check_InspectTree) {
  const std::string snapshot_uuid = "snapshot_uuid";
  const std::string minidump = "minidump";

  // We set up the store so it can only hold one report at most, evicting the oldest ones first.
  const StorageSize size = StorageSize::Bytes(snapshot_uuid.size() + minidump.size() +
                                              4u /*the empty annotations.json*/);
  MakeNewStore(size);

  std::vector<ReportId> garbage_collected_reports;
  Add("program_name1", /*annotations=*/{}, /*attachments=*/{}, snapshot_uuid, minidump,
      &garbage_collected_reports);
  EXPECT_TRUE(garbage_collected_reports.empty());
  Add("program_name2", /*annotations=*/{}, /*attachments=*/{}, snapshot_uuid, minidump,
      &garbage_collected_reports);
  EXPECT_FALSE(garbage_collected_reports.empty());

  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(IsSupersetOf({
          AllOf(NodeMatches(NameMatches("crash_reporter")),
                ChildrenMatch(IsSupersetOf({
                    AllOf(NodeMatches(AllOf(NameMatches("store"),
                                            PropertyList(UnorderedElementsAreArray({
                                                UintIs("max_size_in_kb", size.ToKilobytes()),
                                                UintIs("num_reports_garbage_collected", 1u),
                                            })))),
                          ChildrenMatch(IsEmpty())),
                }))),
      })));
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
