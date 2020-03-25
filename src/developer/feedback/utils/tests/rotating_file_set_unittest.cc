// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/rotating_file_set.h"

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

class RotatingFileSetTest : public testing::Test {
 protected:
  void ReadFileContents(const std::string& file_path, std::string* contents) {
    std::string _contents;
    ASSERT_TRUE(files::ReadFileToString(file_path, &_contents));
    *contents = _contents;
  }

  std::string RootDirectory() { return temp_dir_.path(); }

 private:
  files::ScopedTempDir temp_dir_;
};

TEST_F(RotatingFileSetTest, Writer_SingleFileInSet) {
  const std::vector<const std::string> file_paths = {
      files::JoinPath(RootDirectory(), "file0.txt"),
  };

  RotatingFileSetWriter writer(file_paths, FileSize::Megabytes(1));

  writer.Write("line1\n");
  writer.Write("line2\n");

  std::string file_contents;
  ReadFileContents(file_paths[0], &file_contents);
  EXPECT_EQ(file_contents, "line1\nline2\n");
}

TEST_F(RotatingFileSetTest, Writer_MultipleFilesInSet_ManyRotations) {
  const std::vector<const std::string> file_paths = {
      files::JoinPath(RootDirectory(), "file0.txt"),
      files::JoinPath(RootDirectory(), "file1.txt"),
      files::JoinPath(RootDirectory(), "file2.txt"),
  };

  RotatingFileSetWriter writer(file_paths, FileSize::Bytes(6) * file_paths.size());

  writer.Write("line1\n");
  writer.Write("line2\n");
  writer.Write("line3\n");
  writer.Write("line4\n");
  writer.Write("line5\n");

  std::string file_contents;
  ReadFileContents(file_paths[0], &file_contents);
  EXPECT_EQ(file_contents, "line5\n");
  ReadFileContents(file_paths[1], &file_contents);
  EXPECT_EQ(file_contents, "line4\n");
  ReadFileContents(file_paths[2], &file_contents);
  EXPECT_EQ(file_contents, "line3\n");
}

TEST_F(RotatingFileSetTest, Reader_ConcatenatesCorrectly) {
  const std::vector<const std::string> file_paths = {
      files::JoinPath(RootDirectory(), "file0.txt"),
      files::JoinPath(RootDirectory(), "file1.txt"),
      files::JoinPath(RootDirectory(), "file2.txt"),
  };

  RotatingFileSetWriter writer(file_paths, FileSize::Bytes(6) * file_paths.size());

  writer.Write("line1\n");
  writer.Write("line2\n");
  writer.Write("line3\n");
  writer.Write("line4\n");
  writer.Write("line5\n");

  const std::string output_file = files::JoinPath(RootDirectory(), "output.txt");
  RotatingFileSetReader reader(file_paths);
  EXPECT_TRUE(reader.Concatenate(output_file));

  std::string contents;
  files::ReadFileToString(output_file, &contents);
  EXPECT_EQ(contents, "line3\nline4\nline5\n");
}

TEST_F(RotatingFileSetTest, Reader_ConcatenatesCorrectlyWhenSetContainsEmptyFiles) {
  const std::vector<const std::string> file_paths = {
      files::JoinPath(RootDirectory(), "file0.txt"),
      files::JoinPath(RootDirectory(), "file1.txt"),
      files::JoinPath(RootDirectory(), "file2.txt"),
  };

  RotatingFileSetWriter writer(file_paths, FileSize::Megabytes(6));

  writer.Write("line1\n");
  writer.Write("line2\n");
  writer.Write("line3\n");
  writer.Write("line4\n");
  writer.Write("line5\n");

  const std::string output_file = files::JoinPath(RootDirectory(), "output.txt");
  RotatingFileSetReader reader(file_paths);
  EXPECT_TRUE(reader.Concatenate(output_file));

  std::string contents;
  files::ReadFileToString(output_file, &contents);
  EXPECT_EQ(contents, "line1\nline2\nline3\nline4\nline5\n");
}

TEST_F(RotatingFileSetTest, Reader_ReturnsFalseWhenNoFilesInSet) {
  const std::vector<const std::string> file_paths = {
      files::JoinPath(RootDirectory(), "file0.txt"),
      files::JoinPath(RootDirectory(), "file1.txt"),
      files::JoinPath(RootDirectory(), "file2.txt"),
  };

  const std::string output_file = files::JoinPath(RootDirectory(), "output.txt");
  RotatingFileSetReader reader(file_paths);
  EXPECT_FALSE(reader.Concatenate(output_file));
  EXPECT_FALSE(files::IsFile(output_file));
}

TEST_F(RotatingFileSetTest, Reader_ReturnsFalseWhenAllEmptyFilesInSet) {
  const std::vector<const std::string> file_paths = {
      files::JoinPath(RootDirectory(), "file0.txt"),
      files::JoinPath(RootDirectory(), "file1.txt"),
      files::JoinPath(RootDirectory(), "file2.txt"),
  };

  for (const auto& file_path : file_paths) {
    files::WriteFile(file_path, "", 0);
  }

  const std::string output_file = files::JoinPath(RootDirectory(), "output.txt");
  RotatingFileSetReader reader(file_paths);
  EXPECT_FALSE(reader.Concatenate(output_file));
  EXPECT_FALSE(files::IsFile(output_file));
}

}  // namespace
}  // namespace feedback
