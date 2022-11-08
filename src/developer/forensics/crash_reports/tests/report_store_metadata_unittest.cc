// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/report_store_metadata.h"

#include <lib/syslog/cpp/macros.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/scoped_memfs_manager.h"
#include "src/developer/forensics/utils/storage_size.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics {
namespace crash_reports {
namespace {

using ::testing::UnorderedElementsAreArray;
namespace fs = std::filesystem;

class ReportStoreMetadataTest : public ::testing::Test {
 public:
  ReportStoreMetadataTest() : metadata_(tmp_dir_.path(), StorageSize::Megabytes(1u)) {}

  bool WriteAttachment(const std::string& program, const ReportId report_id, const std::string& key,
                       const std::string& data) {
    namespace fs = std::filesystem;

    fs::path path(tmp_dir_.path());

    std::error_code ec;
    fs::create_directory(path /= program, ec);
    if (ec) {
      return false;
    }

    fs::create_directory(path /= std::to_string(report_id), ec);
    if (ec) {
      return false;
    }

    std::ofstream(path /= key) << data;
    return true;
  }

 protected:
  ReportStoreMetadata& metadata() { return metadata_; }

  std::string ProgramPath(const std::string& program) {
    return fs::path(tmp_dir_.path()) / program;
  }

  std::string ReportPath(const std::string& program, const ReportId report_id) {
    return fs::path(ProgramPath(program)) / std::to_string(report_id);
  }

 private:
  files::ScopedTempDir tmp_dir_;
  ReportStoreMetadata metadata_;
};

TEST_F(ReportStoreMetadataTest, RecreateFromFilesystem_Reports) {
  const std::string kValue("value");
  const StorageSize kValueSize(StorageSize::Bytes(kValue.size()));

  // Add reports to the filesystem.
  ASSERT_TRUE(WriteAttachment("program 1", /*report_id=*/0, "key 1", kValue));
  ASSERT_TRUE(WriteAttachment("program 1", /*report_id=*/0, "key 2", kValue));
  ASSERT_TRUE(WriteAttachment("program 1", /*report_id=*/1, "key 3", kValue));
  ASSERT_TRUE(WriteAttachment("program 1", /*report_id=*/2, "key 4", kValue));
  metadata().RecreateFromFilesystem();

  EXPECT_THAT(metadata().Reports(), UnorderedElementsAreArray({0, 1, 2}));

  EXPECT_TRUE(metadata().Contains(0));
  EXPECT_TRUE(metadata().Contains(1));
  EXPECT_TRUE(metadata().Contains(2));

  EXPECT_EQ(metadata().CurrentSize(), kValueSize * 4);
  EXPECT_EQ(metadata().ReportSize(0), kValueSize * 2);
  EXPECT_EQ(metadata().ReportSize(1), kValueSize);
  EXPECT_EQ(metadata().ReportSize(2), kValueSize);

  EXPECT_EQ(metadata().ReportDirectory(0), ReportPath("program 1", 0));
  EXPECT_EQ(metadata().ReportDirectory(1), ReportPath("program 1", 1));
  EXPECT_EQ(metadata().ReportDirectory(2), ReportPath("program 1", 2));

  EXPECT_THAT(metadata().ReportAttachments(0), UnorderedElementsAreArray({"key 1", "key 2"}));
  EXPECT_THAT(metadata().ReportAttachments(1), UnorderedElementsAreArray({"key 3"}));
  EXPECT_THAT(metadata().ReportAttachments(2), UnorderedElementsAreArray({"key 4"}));

  // Add more reports to the filesystem.
  ASSERT_TRUE(WriteAttachment("program 2", /*report_id=*/3, "key 1", kValue));
  ASSERT_TRUE(WriteAttachment("program 2", /*report_id=*/3, "key 2", kValue));
  ASSERT_TRUE(WriteAttachment("program 2", /*report_id=*/4, "key 3", kValue));
  ASSERT_TRUE(WriteAttachment("program 2", /*report_id=*/5, "key 4", kValue));
  metadata().RecreateFromFilesystem();

  EXPECT_THAT(metadata().Reports(), UnorderedElementsAreArray({0, 1, 2, 3, 4, 5}));
  EXPECT_TRUE(metadata().Contains(0));
  EXPECT_TRUE(metadata().Contains(1));
  EXPECT_TRUE(metadata().Contains(2));
  EXPECT_TRUE(metadata().Contains(3));
  EXPECT_TRUE(metadata().Contains(4));
  EXPECT_TRUE(metadata().Contains(5));

  EXPECT_EQ(metadata().CurrentSize(), kValueSize * 8);
  EXPECT_EQ(metadata().ReportSize(0), kValueSize * 2);
  EXPECT_EQ(metadata().ReportSize(1), kValueSize);
  EXPECT_EQ(metadata().ReportSize(2), kValueSize);
  EXPECT_EQ(metadata().ReportSize(3), kValueSize * 2);
  EXPECT_EQ(metadata().ReportSize(4), kValueSize);
  EXPECT_EQ(metadata().ReportSize(5), kValueSize);

  EXPECT_EQ(metadata().ReportDirectory(0), ReportPath("program 1", 0));
  EXPECT_EQ(metadata().ReportDirectory(1), ReportPath("program 1", 1));
  EXPECT_EQ(metadata().ReportDirectory(2), ReportPath("program 1", 2));
  EXPECT_EQ(metadata().ReportDirectory(3), ReportPath("program 2", 3));
  EXPECT_EQ(metadata().ReportDirectory(4), ReportPath("program 2", 4));
  EXPECT_EQ(metadata().ReportDirectory(5), ReportPath("program 2", 5));

  EXPECT_THAT(metadata().ReportAttachments(0), UnorderedElementsAreArray({"key 1", "key 2"}));
  EXPECT_THAT(metadata().ReportAttachments(1), UnorderedElementsAreArray({"key 3"}));
  EXPECT_THAT(metadata().ReportAttachments(2), UnorderedElementsAreArray({"key 4"}));
  EXPECT_THAT(metadata().ReportAttachments(3), UnorderedElementsAreArray({"key 1", "key 2"}));
  EXPECT_THAT(metadata().ReportAttachments(4), UnorderedElementsAreArray({"key 3"}));
  EXPECT_THAT(metadata().ReportAttachments(5), UnorderedElementsAreArray({"key 4"}));
}

TEST_F(ReportStoreMetadataTest, RecreateFromFilesystem_Programs) {
  const std::string kValue("value");

  // Add reports to the filesystem.
  ASSERT_TRUE(WriteAttachment("program 1", /*report_id=*/0, "key 1", kValue));
  ASSERT_TRUE(WriteAttachment("program 1", /*report_id=*/0, "key 2", kValue));
  ASSERT_TRUE(WriteAttachment("program 1", /*report_id=*/1, "key 3", kValue));
  ASSERT_TRUE(WriteAttachment("program 1", /*report_id=*/2, "key 4", kValue));
  metadata().RecreateFromFilesystem();

  EXPECT_THAT(metadata().Programs(), UnorderedElementsAreArray({
                                         "program 1",
                                     }));
  EXPECT_THAT(metadata().ProgramReports("program 1"), UnorderedElementsAreArray({0, 1, 2}));
  EXPECT_EQ(metadata().ProgramDirectory("program 1"), ProgramPath("program 1"));

  EXPECT_EQ(metadata().ReportProgram(0), "program 1");
  EXPECT_EQ(metadata().ReportProgram(1), "program 1");
  EXPECT_EQ(metadata().ReportProgram(2), "program 1");

  // Add more reports to the filesystem.
  ASSERT_TRUE(WriteAttachment("program 2", /*report_id=*/3, "key 1", kValue));
  ASSERT_TRUE(WriteAttachment("program 2", /*report_id=*/3, "key 2", kValue));
  ASSERT_TRUE(WriteAttachment("program 2", /*report_id=*/4, "key 3", kValue));
  ASSERT_TRUE(WriteAttachment("program 2", /*report_id=*/5, "key 4", kValue));
  metadata().RecreateFromFilesystem();

  EXPECT_THAT(metadata().Programs(), UnorderedElementsAreArray({
                                         "program 1",
                                         "program 2",
                                     }));
  EXPECT_THAT(metadata().ProgramReports("program 1"), UnorderedElementsAreArray({0, 1, 2}));
  EXPECT_THAT(metadata().ProgramReports("program 2"), UnorderedElementsAreArray({3, 4, 5}));
  EXPECT_EQ(metadata().ProgramDirectory("program 1"), ProgramPath("program 1"));
  EXPECT_EQ(metadata().ProgramDirectory("program 2"), ProgramPath("program 2"));

  EXPECT_EQ(metadata().ReportProgram(0), "program 1");
  EXPECT_EQ(metadata().ReportProgram(1), "program 1");
  EXPECT_EQ(metadata().ReportProgram(2), "program 1");
  EXPECT_EQ(metadata().ReportProgram(3), "program 2");
  EXPECT_EQ(metadata().ReportProgram(4), "program 2");
  EXPECT_EQ(metadata().ReportProgram(5), "program 2");
}

TEST_F(ReportStoreMetadataTest, AddAndDelete) {
  metadata().Add(0, "program 1", {"key 1", "key 2"}, StorageSize::Bytes(10));

  ASSERT_TRUE(metadata().Contains(0));
  ASSERT_TRUE(metadata().Contains("program 1"));

  metadata().Delete(0);

  ASSERT_FALSE(metadata().Contains(0));
  ASSERT_FALSE(metadata().Contains("program 1"));
}

TEST_F(ReportStoreMetadataTest, RecreateFromFilesystem_FailsInitially) {
  testing::ScopedMemFsManager scoped_mem_fs;
  ReportStoreMetadata metadata("/cache/delayed/path", StorageSize::Gigabytes(1u));
  ASSERT_FALSE(metadata.IsDirectoryUsable());

  scoped_mem_fs.Create("/cache/delayed/path");
  metadata.RecreateFromFilesystem();
  EXPECT_TRUE(metadata.IsDirectoryUsable());
}

TEST_F(ReportStoreMetadataTest, ReportAttachmentPath_AttachmentExists) {
  metadata().Add(0, "program 1", {"key 1", "key 2"}, StorageSize::Bytes(10));

  const auto path = metadata().ReportAttachmentPath(0, "key 1");
  const std::string expected_path = files::JoinPath(ReportPath("program 1", 0), "key 1");

  EXPECT_EQ(path, expected_path);
}

TEST_F(ReportStoreMetadataTest, ReportAttachmentPath_AttachmentDoesNotExist) {
  metadata().Add(0, "program 1", {"key 1", "key 2"}, StorageSize::Bytes(10));

  const auto path = metadata().ReportAttachmentPath(0, "key 3");
  EXPECT_FALSE(path.has_value());
}

TEST_F(ReportStoreMetadataTest, IncreaseSize) {
  const ReportId id = 0;
  metadata().Add(id, "program 1", {"key 1", "key 2"}, StorageSize::Bytes(1));

  const StorageSize original_size = metadata().CurrentSize();
  metadata().IncreaseSize(id, StorageSize::Bytes(2));

  EXPECT_EQ(metadata().CurrentSize(), original_size + StorageSize::Bytes(2));
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
