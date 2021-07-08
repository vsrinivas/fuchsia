// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <gtest/gtest.h>

#include "src/storage/fs_test/fs_test.h"
#include "src/storage/fs_test/test_filesystem.h"
#include "src/storage/memfs/test/memfs_fs_test.h"
#include "src/storage/tools/blobfs-compression/blobfs-compression.h"

namespace blobfs_compress {
namespace {
using namespace fs_test;

}  // namespace

class CliOptionValidationTest : public ::testing::Test {
 public:
  CliOptionValidationTest() : fs_(CreateTestFilesystem()) {}

 protected:
  TestFilesystem fs_;

  void CreateFile(const std::string& file_path, const std::string& file_content = "") {
    fbl::unique_fd fd(open(file_path.c_str(), O_RDWR | O_CREAT, S_IRUSR));
    ASSERT_TRUE(fd.is_valid());
    ASSERT_EQ(write(fd.get(), file_content.c_str(), file_content.length()),
              static_cast<int>(file_content.length()));
    ASSERT_EQ(close(fd.get()), ZX_OK);
  }

 private:
  TestFilesystem CreateTestFilesystem() {
    auto fs_options = memfs::DefaultMemfsTestOptions();
    fs_options.description = "fake_memfs";
    auto fs_or = TestFilesystem::Create(fs_options);
    return std::move(fs_or).value();
  }
};

TEST_F(CliOptionValidationTest, NoSourceFileNoOutputFile) {
  CompressionCliOptionStruct options_missing_source;
  ASSERT_EQ(ValidateCliOptions(options_missing_source), ZX_ERR_INVALID_ARGS);
}

TEST_F(CliOptionValidationTest, OutputFileOnly) {
  CompressionCliOptionStruct options_missing_source = {
      .compressed_file = "test",
  };
  ASSERT_EQ(ValidateCliOptions(options_missing_source), ZX_ERR_INVALID_ARGS);
}

TEST_F(CliOptionValidationTest, ValidSourceFileNoOutputFile) {
  const std::string file_path = fs_.mount_path() + "valid_file";
  CreateFile(file_path, "hello");
  CompressionCliOptionStruct options_valid = {
      .source_file = file_path,
  };
  options_valid.source_file_fd.reset(open(file_path.c_str(), O_RDONLY));
  ASSERT_EQ(ValidateCliOptions(options_valid), ZX_OK);
}

TEST_F(CliOptionValidationTest, ValidEmptyExistingSourceFileNoOutputFile) {
  const std::string file_path = fs_.mount_path() + "valid_empty_file";
  CreateFile(file_path);
  CompressionCliOptionStruct options_valid = {
      .source_file = file_path,
  };
  options_valid.source_file_fd.reset(open(file_path.c_str(), O_RDONLY));
  ASSERT_EQ(ValidateCliOptions(options_valid), ZX_OK);
}

TEST_F(CliOptionValidationTest, SourceFileIsDirectory) {
  const std::string dir_path = fs_.mount_path() + "directory";
  ASSERT_EQ(mkdir(dir_path.c_str(), S_IRUSR), ZX_OK);
  CompressionCliOptionStruct options_valid = {
      .source_file = dir_path,
  };
  options_valid.source_file_fd.reset(open(dir_path.c_str(), O_DIRECTORY | O_RDONLY));
  ASSERT_EQ(ValidateCliOptions(options_valid), ZX_ERR_NOT_FILE);
}

TEST_F(CliOptionValidationTest, ValidSourceFileValidOutputFile) {
  const std::string source_path = fs_.mount_path() + "source_file";
  const std::string output_path = fs_.mount_path() + "output_file";
  CreateFile(source_path, "hello");
  CompressionCliOptionStruct options_valid = {
      .source_file = source_path,
      .compressed_file = output_path,
  };
  options_valid.source_file_fd.reset(open(source_path.c_str(), O_RDONLY));
  options_valid.compressed_file_fd.reset(
      open(output_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, S_IRUSR));
  ASSERT_EQ(ValidateCliOptions(options_valid), ZX_OK);
}

TEST_F(CliOptionValidationTest, ValidSourceFileInvalidOutputFile) {
  const std::string source_path = fs_.mount_path() + "source_file";
  const std::string invalid_output_path = fs_.mount_path() + "output_directory";
  CreateFile(source_path, "hello");
  ASSERT_EQ(mkdir(invalid_output_path.c_str(), S_IRUSR), ZX_OK);
  CompressionCliOptionStruct options_valid = {
      .source_file = source_path,
      .compressed_file = invalid_output_path,
  };
  options_valid.source_file_fd.reset(open(source_path.c_str(), O_RDONLY));
  // Open directory as file.
  options_valid.compressed_file_fd.reset(
      open(invalid_output_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, S_IRUSR));
  ASSERT_EQ(ValidateCliOptions(options_valid), ZX_ERR_BAD_PATH);
}

}  // namespace blobfs_compress
