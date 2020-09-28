// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/store.h"

#include <lib/syslog/cpp/macros.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/sized_data.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/timekeeper/test_clock.h"
// TODO(fxbug.dev/57392): Move it back to //third_party once unification completes.
#include "zircon/third_party/rapidjson/include/rapidjson/document.h"

namespace forensics {
namespace crash_reports {
namespace {

using inspect::testing::ChildrenMatch;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;
using inspect::testing::UintIs;
using testing::IsEmpty;
using testing::IsSupersetOf;
using testing::UnorderedElementsAreArray;

SizedData MakeSizedData(const std::string& content) {
  return SizedData(content.begin(), content.end());
}

class StoreTest : public UnitTestFixture {
 public:
  StoreTest() { MakeNewStore(StorageSize::Megabytes(1)); }

 protected:
  void MakeNewStore(const StorageSize max_size) {
    info_context_ =
        std::make_shared<InfoContext>(&InspectRoot(), &clock_, dispatcher(), services());
    store_ = std::make_unique<Store>(info_context_, tmp_dir_.path(), max_size);
  }

  std::optional<Store::Uid> Add(const std::string& program_shortname,
                                std::vector<Store::Uid>* garbage_collected_reports) {
    return Add(program_shortname, {}, {}, "", std::nullopt, garbage_collected_reports);
  }

  std::optional<Store::Uid> Add(const std::string& program_shortname,
                                const std::map<std::string, std::string>& annotations,
                                const std::map<std::string, std::string>& attachments,
                                const std::string& snapshot_uuid,
                                const std::optional<std::string>& minidump,
                                std::vector<Store::Uid>* garbage_collected_reports) {
    std::map<std::string, SizedData> attachments_data;
    for (const auto& [k, v] : attachments) {
      attachments_data.emplace(k, MakeSizedData(v));
    }

    std::optional<SizedData> minidump_data;
    if (minidump.has_value()) {
      minidump_data = MakeSizedData(minidump.value());
    }

    auto report = Report(program_shortname, annotations, std::move(attachments_data), snapshot_uuid,
                         std::move(minidump_data));

    return store_->Add(std::move(report), garbage_collected_reports);
  }

  bool Get(const Store::Uid& id, std::string* program_shortname,
           std::map<std::string, std::string>* annotations,
           std::map<std::string, std::string>* attachments, std::string* snapshot_uuid,
           std::optional<std::string>* minidump) {
    const auto report = store_->Get(id);
    if (!report.has_value()) {
      return false;
    }

    *program_shortname = report.value().ProgramShortname();
    *annotations = report.value().Annotations();
    for (const auto& [filename, attachment] : report.value().Attachments()) {
      (*attachments)[filename] = std::string(attachment.begin(), attachment.end());
    }
    *snapshot_uuid = report.value().SnapshotUuid();
    if (report.value().Minidump().has_value()) {
      const auto& value = report.value().Minidump().value();
      *minidump = std::string(value.begin(), value.end());
    } else {
      *minidump = std::nullopt;
    }
    return true;
  }

  bool Read(const std::string& program_shortname, const Store::Uid& id,
            std::map<std::string, std::string>* annotations_out,
            std::map<std::string, std::string>* attachments_out,
            std::optional<std::string>* snapshot_uuid_out,
            std::optional<std::string>* minidump_out) {
    const std::string id_str = std::to_string(id);
    const std::string path =
        files::JoinPath(files::JoinPath(tmp_dir_.path(), program_shortname), id_str);

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
  std::shared_ptr<InfoContext> info_context_;

  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<Store> store_;
};

TEST_F(StoreTest, Succeed_Add) {
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

  std::vector<Store::Uid> garbage_collected_reports;
  const auto id = Add(expected_program_shortname, expected_annotations, expected_attachments,
                      expected_snapshot_uuid, expected_minidump, &garbage_collected_reports);
  EXPECT_TRUE(id.has_value());
  EXPECT_TRUE(garbage_collected_reports.empty());

  std::map<std::string, std::string> annotations;
  std::map<std::string, std::string> attachments;
  std::optional<std::string> snapshot_uuid;
  std::optional<std::string> minidump;

  ASSERT_TRUE(store_->Contains(id.value()));
  ASSERT_TRUE(Read(expected_program_shortname, id.value(), &annotations, &attachments,
                   &snapshot_uuid, &minidump));

  EXPECT_EQ(expected_annotations, annotations);
  EXPECT_EQ(expected_attachments, attachments);
  ASSERT_TRUE(snapshot_uuid.has_value());
  EXPECT_EQ(expected_snapshot_uuid, snapshot_uuid.value());
  ASSERT_TRUE(minidump.has_value());
  EXPECT_EQ(expected_minidump, minidump.value());
}

TEST_F(StoreTest, Succeed_Get) {
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

  std::vector<Store::Uid> garbage_collected_reports;
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

TEST_F(StoreTest, Fail_ReservedAttachmentKey) {
  std::vector<Store::Uid> garbage_collected_reports;
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

TEST_F(StoreTest, Succeed_Remove) {
  std::vector<Store::Uid> garbage_collected_reports;
  const auto id = Add("program_shortname", /*annotations=*/{}, /*attachments=*/{},
                      /*snapshot_uuid=*/"",
                      /*minidump=*/std::nullopt, &garbage_collected_reports);
  EXPECT_TRUE(id.has_value());
  EXPECT_TRUE(garbage_collected_reports.empty());
  ASSERT_TRUE(store_->Contains(id.value()));

  store_->Remove(id.value());
  EXPECT_FALSE(store_->Contains(id.value()));
  EXPECT_TRUE(GetProgramShortnames().empty());
}

TEST_F(StoreTest, Succeed_GarbageCollection) {
  // To make this test easier to understand, the below table shows when and why each report is being
  // garbage collected.
  //
  // uid | program name  | garbage collection order | garbage collection reason
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
  std::vector<Store::Uid> garbage_collected_reports;

  const auto id1 = Add("program_name1", &garbage_collected_reports);
  const auto id2 = Add("program_name2", &garbage_collected_reports);
  const auto id3 = Add("program_name3", &garbage_collected_reports);
  const auto id4 = Add("program_name3", &garbage_collected_reports);

  // Add a report to force garbage collection of the oldest report for program_name3
  const auto id5 = Add("program_name3", &garbage_collected_reports);
  EXPECT_THAT(garbage_collected_reports, UnorderedElementsAreArray({id3.value()}));
  EXPECT_FALSE(store_->Contains(id3.value()));

  // Add a report to force garbage collection of the oldest report for program_name3
  const auto id6 = Add("program_name3", &garbage_collected_reports);
  EXPECT_THAT(garbage_collected_reports, UnorderedElementsAreArray({id4.value()}));
  EXPECT_FALSE(store_->Contains(id4.value()));

  // Remove the report for program_name1 from the store and add a report for program_name2 so both
  // program_name2 and program_name3 have 2 reports in the store.
  EXPECT_TRUE(store_->Remove(id1.value()));
  const auto id7 = Add("program_name2", &garbage_collected_reports);
  EXPECT_TRUE(garbage_collected_reports.empty());

  // Add a report to force garbage collection of the oldest report between program_nam2 and
  // program_name3.
  const auto id8 = Add("program_name4", &garbage_collected_reports);
  EXPECT_THAT(garbage_collected_reports, UnorderedElementsAreArray({id2.value()}));
  EXPECT_FALSE(store_->Contains(id2.value()));

  EXPECT_THAT(store_->GetAllUids(), UnorderedElementsAreArray({
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

TEST_F(StoreTest, Succeed_RebuildsMetadata) {
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

  std::vector<Store::Uid> ids;
  std::vector<Store::Uid> garbage_collected_reports;
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

    ASSERT_TRUE(store_->Contains(id));
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

TEST_F(StoreTest, Succeed_RebuildCleansEmptyDirectories) {
  std::vector<Store::Uid> ids;
  std::vector<Store::Uid> garbage_collected_reports;
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

TEST_F(StoreTest, Check_InspectTree) {
  const std::string snapshot_uuid = "snapshot_uuid";
  const std::string minidump = "minidump";

  // We set up the store so it can only hold one report at most, evicting the oldest ones first.
  const StorageSize size = StorageSize::Bytes(snapshot_uuid.size() + minidump.size() +
                                              4u /*the empty annotations.json*/);
  MakeNewStore(size);

  std::vector<Store::Uid> garbage_collected_reports;
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
