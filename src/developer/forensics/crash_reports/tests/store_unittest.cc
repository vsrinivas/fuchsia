// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/store.h"

#include <lib/syslog/cpp/macros.h>

#include <map>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/utils/sized_data.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "third_party/rapidjson/include/rapidjson/document.h"

namespace forensics {
namespace crash_reports {
namespace {

using testing::UnorderedElementsAreArray;

SizedData MakeSizedData(const std::string& content) {
  return SizedData(content.begin(), content.end());
}

class StoreTest : public testing::Test {
 public:
  StoreTest() : store_(std::make_unique<Store>(tmp_dir_.path(), StorageSize::Megabytes(1))) {}

 protected:
  void MakeNewStore(const StorageSize max_size) {
    store_ = std::make_unique<Store>(tmp_dir_.path(), max_size);
  }

  std::optional<Store::Uid> Add(const std::string& program_shortname,
                                const std::map<std::string, std::string>& annotations,
                                const std::map<std::string, std::string>& attachments,
                                const std::optional<std::string>& minidump) {
    std::map<std::string, SizedData> attachments_data;
    for (const auto& [k, v] : attachments) {
      attachments_data.emplace(k, MakeSizedData(v));
    }

    std::optional<SizedData> minidump_data;
    if (minidump.has_value()) {
      minidump_data = MakeSizedData(minidump.value());
    }

    auto report = Report(program_shortname, annotations, std::move(attachments_data),
                         std::move(minidump_data));

    return store_->Add(std::move(report));
  }

  bool Get(const Store::Uid& id, std::string* program_shortname,
           std::map<std::string, std::string>* annotations,
           std::map<std::string, std::string>* attachments, std::optional<std::string>* minidump) {
    const auto report = store_->Get(id);
    if (!report.has_value()) {
      return false;
    }

    *program_shortname = report.value().ProgramShortname();
    *annotations = report.value().Annotations();
    for (const auto& [filename, attachment] : report.value().Attachments()) {
      (*attachments)[filename] = std::string(attachment.begin(), attachment.end());
    }
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

 private:
  files::ScopedTempDir tmp_dir_;

 protected:
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

  const std::string expected_minidump = "mindump";

  const auto id = Add(expected_program_shortname, expected_annotations, expected_attachments,
                      expected_minidump);
  EXPECT_TRUE(id.has_value());

  std::map<std::string, std::string> annotations;
  std::map<std::string, std::string> attachments;
  std::optional<std::string> minidump;

  ASSERT_TRUE(store_->Contains(id.value()));
  ASSERT_TRUE(Read(expected_program_shortname, id.value(), &annotations, &attachments, &minidump));

  EXPECT_EQ(expected_annotations, annotations);
  EXPECT_EQ(expected_attachments, attachments);
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

  const std::string expected_minidump = "mindump";

  const auto id = Add(expected_program_shortname, expected_annotations, expected_attachments,
                      expected_minidump);
  ASSERT_TRUE(id.has_value());

  std::string program_shortname;
  std::map<std::string, std::string> annotations;
  std::map<std::string, std::string> attachments;
  std::optional<std::string> minidump;

  ASSERT_TRUE(Get(id.value(), &program_shortname, &annotations, &attachments, &minidump));

  EXPECT_EQ(expected_program_shortname, program_shortname);
  EXPECT_EQ(expected_annotations, annotations);
  EXPECT_EQ(expected_attachments, attachments);
  ASSERT_TRUE(minidump.has_value());
  EXPECT_EQ(expected_minidump, minidump.value());
}

TEST_F(StoreTest, Fail_ReservedAttachmentKey) {
  EXPECT_FALSE(Add("program_shortname", /*annotations=*/{},
                   /*attachments=*/{{"annotations.json", ""}},
                   /*minidump=*/std::nullopt)
                   .has_value());
  EXPECT_FALSE(Add("program_shortname", /*annotations=*/{}, /*attachments=*/{{"minidump.dmp", ""}},
                   /*minidump=*/std::nullopt)
                   .has_value());
}

TEST_F(StoreTest, Succeed_Remove) {
  const auto id =
      Add("program_shortname", /*annotations=*/{}, /*attachments=*/{}, /*minidump=*/std::nullopt);
  EXPECT_TRUE(id.has_value());
  ASSERT_TRUE(store_->Contains(id.value()));

  store_->Remove(id.value());
  EXPECT_FALSE(store_->Contains(id.value()));
  EXPECT_TRUE(GetProgramShortnames().empty());
}

TEST_F(StoreTest, Succeed_GarbageCollection) {
  const std::string minidump = "minidump";

  // We set up the store so it can only hold one report at most, evicting the oldest ones first.
  MakeNewStore(StorageSize::Bytes(minidump.size() + 4u /*the empty annotations.json*/));

  const auto id1 = Add("program_name1", /*annotations=*/{}, /*attachments=*/{}, minidump);
  const auto id2 = Add("program_name2", /*annotations=*/{}, /*attachments=*/{}, minidump);

  EXPECT_FALSE(store_->Contains(id1.value()));
  EXPECT_TRUE(store_->Contains(id2.value()));

  EXPECT_THAT(GetProgramShortnames(), UnorderedElementsAreArray({"program_name2"}));

  const auto id3 = Add("program_name3", /*annotations=*/{}, /*attachments=*/{}, minidump);

  EXPECT_FALSE(store_->Contains(id2.value()));
  EXPECT_TRUE(store_->Contains(id3.value()));

  EXPECT_THAT(GetProgramShortnames(), UnorderedElementsAreArray({"program_name3"}));
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
