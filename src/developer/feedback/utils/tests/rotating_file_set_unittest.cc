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

TEST_F(RotatingFileSetTest, Check_SingleFileInSet) {
  const std::vector<const std::string> file_paths = {
      files::JoinPath(RootDirectory(), "file0.txt"),
  };

  RotatingFileSet buffer(file_paths, FileSize::Megabytes(1));

  buffer.Write("line1\n");
  buffer.Write("line2\n");

  std::string file_contents;
  ReadFileContents(file_paths[0], &file_contents);
  EXPECT_EQ(file_contents, "line1\nline2\n");
}

TEST_F(RotatingFileSetTest, Check_SingleFileInSet_WritesClearFile) {
  const std::vector<const std::string> file_paths = {
      files::JoinPath(RootDirectory(), "file0.txt"),
  };

  std::string file_contents;

  RotatingFileSet buffer(file_paths, FileSize::Bytes(6));

  buffer.Write("line1\n");

  ReadFileContents(file_paths[0], &file_contents);
  EXPECT_EQ(file_contents, "line1\n");

  buffer.Write("line2\n");

  ReadFileContents(file_paths[0], &file_contents);
  EXPECT_EQ(file_contents, "line2\n");
}

TEST_F(RotatingFileSetTest, Check_MultipleFilesInSet_WriteRotateFiles) {
  const std::vector<const std::string> file_paths = {
      files::JoinPath(RootDirectory(), "file0.txt"),
      files::JoinPath(RootDirectory(), "file1.txt"),
      files::JoinPath(RootDirectory(), "file2.txt"),
  };

  std::string file_contents;

  RotatingFileSet buffer(file_paths, FileSize::Bytes(6) * file_paths.size());

  buffer.Write("line1\n");

  ReadFileContents(file_paths[0], &file_contents);
  EXPECT_EQ(file_contents, "line1\n");

  buffer.Write("line2\n");

  ReadFileContents(file_paths[0], &file_contents);
  EXPECT_EQ(file_contents, "line2\n");
  ReadFileContents(file_paths[1], &file_contents);
  EXPECT_EQ(file_contents, "line1\n");

  buffer.Write("line3\n");

  ReadFileContents(file_paths[0], &file_contents);
  EXPECT_EQ(file_contents, "line3\n");
  ReadFileContents(file_paths[1], &file_contents);
  EXPECT_EQ(file_contents, "line2\n");
  ReadFileContents(file_paths[2], &file_contents);
  EXPECT_EQ(file_contents, "line1\n");
}

TEST_F(RotatingFileSetTest, Check_MultipleFilesInSet_ManyRotations) {
  const std::vector<const std::string> file_paths = {
      files::JoinPath(RootDirectory(), "file0.txt"),
      files::JoinPath(RootDirectory(), "file1.txt"),
      files::JoinPath(RootDirectory(), "file2.txt"),
  };

  RotatingFileSet buffer(file_paths, FileSize::Bytes(6) * file_paths.size());

  buffer.Write("line1\n");
  buffer.Write("line2\n");
  buffer.Write("line3\n");
  buffer.Write("line4\n");
  buffer.Write("line5\n");

  std::string file_contents;
  ReadFileContents(file_paths[0], &file_contents);
  EXPECT_EQ(file_contents, "line5\n");
  ReadFileContents(file_paths[1], &file_contents);
  EXPECT_EQ(file_contents, "line4\n");
  ReadFileContents(file_paths[2], &file_contents);
  EXPECT_EQ(file_contents, "line3\n");
}

}  // namespace
}  // namespace feedback
