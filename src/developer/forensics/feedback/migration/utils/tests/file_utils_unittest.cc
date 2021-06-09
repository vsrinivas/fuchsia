// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/migration/utils/file_utils.h"

#include <lib/syslog/cpp/macros.h>

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fbl/unique_fd.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace forensics {
namespace feedback {
namespace {

using ::testing::IsEmpty;
using ::testing::UnorderedElementsAreArray;

TEST(FileUtilsTests, IntoInterfaceHandle) {
  {
    fbl::unique_fd fd(fbl::unique_fd::InvalidValue());
    EXPECT_FALSE(IntoInterfaceHandle(std::move(fd)).is_valid());
  }

  {
    files::ScopedTempDir dir;
    fbl::unique_fd fd(open(dir.path().c_str(), O_DIRECTORY | O_RDWR, 0777));
    EXPECT_TRUE(IntoInterfaceHandle(std::move(fd)).is_valid());
  }
}

TEST(FileUtilsTests, IntoFd) {
  {
    fbl::unique_fd fd(fbl::unique_fd::InvalidValue());
    EXPECT_FALSE(IntoFd(IntoInterfaceHandle(std::move(fd))).is_valid());
  }

  {
    files::ScopedTempDir dir;
    fbl::unique_fd fd(open(dir.path().c_str(), O_DIRECTORY | O_RDWR, 0777));
    EXPECT_TRUE(IntoFd(IntoInterfaceHandle(std::move(fd))).is_valid());
  }
}

std::string IntoRelativePath(std::string absolute_path, files::ScopedTempDir& root) {
  FX_CHECK(absolute_path.find(root.path()) == 0);
  absolute_path.erase(0, root.path().size());
  return files::JoinPath(".", absolute_path);
}

TEST(FileUtilsTests, CopyFile) {
  files::ScopedTempDir source_root;
  files::ScopedTempDir sink_root;

  fbl::unique_fd source_root_fd(open(source_root.path().c_str(), O_DIRECTORY | O_RDONLY, 0777));
  fbl::unique_fd sink_root_fd(open(sink_root.path().c_str(), O_DIRECTORY | O_RDWR, 0777));

  ASSERT_TRUE(source_root_fd.is_valid());
  ASSERT_TRUE(sink_root_fd.is_valid());

  // Combinations of valid and invalid file descriptors.
  EXPECT_FALSE(
      CopyFile(source_root_fd, fbl::unique_fd(fbl::unique_fd::InvalidValue()), "unused-path"));
  EXPECT_TRUE(
      CopyFile(fbl::unique_fd(fbl::unique_fd::InvalidValue()), sink_root_fd, "unused-path"));

  // Copy a directory.
  {
    std::string path;
    ASSERT_TRUE(source_root.NewTempDir(&path));
    path = IntoRelativePath(path, source_root);
    EXPECT_FALSE(CopyFile(source_root_fd, sink_root_fd, path));
  }

  // File directly under the root.
  {
    std::string path;
    ASSERT_TRUE(source_root.NewTempFileWithData("file one", &path));

    path = IntoRelativePath(path, source_root);
    EXPECT_TRUE(CopyFile(source_root_fd, sink_root_fd, path));
    EXPECT_TRUE(files::IsFileAt(sink_root_fd.get(), path));

    std::string content;
    ASSERT_TRUE(files::ReadFileToStringAt(sink_root_fd.get(), path, &content));
    EXPECT_EQ(content, "file one");
  }

  // Files in a nested directory.
  {
    std::string dir_path;
    ASSERT_TRUE(source_root.NewTempDir(&dir_path));

    std::string path_one = files::JoinPath(dir_path, "file_one");
    ASSERT_TRUE(files::WriteFile(path_one, "file one"));

    path_one = IntoRelativePath(path_one, source_root);
    EXPECT_TRUE(CopyFile(source_root_fd, sink_root_fd, path_one));
    EXPECT_TRUE(files::IsFileAt(sink_root_fd.get(), path_one));

    std::string path_two = files::JoinPath(dir_path, "file_two");
    ASSERT_TRUE(files::WriteFile(path_two, "file two"));

    path_two = IntoRelativePath(path_two, source_root);
    EXPECT_TRUE(CopyFile(source_root_fd, sink_root_fd, path_two));
    EXPECT_TRUE(files::IsFileAt(sink_root_fd.get(), path_two));

    std::string content;
    ASSERT_TRUE(files::ReadFileToStringAt(sink_root_fd.get(), path_one, &content));
    EXPECT_EQ(content, "file one");

    ASSERT_TRUE(files::ReadFileToStringAt(sink_root_fd.get(), path_two, &content));
    EXPECT_EQ(content, "file two");
  }
}

TEST(FileUtilsTests, GetNestedDirectories) {
  files::ScopedTempDir root;
  std::vector<std::string> expected_dirs({"."});
  auto AddDir = [&](const std::string& relative_path) {
    const std::string path = files::JoinPath(root.path(), relative_path);
    expected_dirs.push_back(IntoRelativePath(path, root));
    return files::CreateDirectory(path);
  };
  auto AddFile = [&](const std::string& relative_path) {
    return files::WriteFile(files::JoinPath(root.path(), relative_path), "unused-content");
  };

  ASSERT_TRUE(AddDir("dir0"));

  ASSERT_TRUE(AddDir("dir1"));
  ASSERT_TRUE(AddFile("dir1/file0"));
  ASSERT_TRUE(AddDir("dir1/dir0"));
  ASSERT_TRUE(AddFile("dir1/dir0/file0"));
  ASSERT_TRUE(AddDir("dir1/dir1"));
  ASSERT_TRUE(AddFile("dir1/dir1/file0"));
  ASSERT_TRUE(AddFile("dir1/dir1/file1"));

  ASSERT_TRUE(AddDir("dir2"));
  ASSERT_TRUE(AddDir("dir2/dir0"));
  ASSERT_TRUE(AddDir("dir2/dir1"));
  ASSERT_TRUE(AddDir("dir2/dir1/dir0"));
  ASSERT_TRUE(AddDir("dir2/dir1/dir1"));
  ASSERT_TRUE(AddDir("dir2/dir2"));
  ASSERT_TRUE(AddDir("dir2/dir2/dir0"));
  ASSERT_TRUE(AddDir("dir2/dir2/dir1"));
  ASSERT_TRUE(AddDir("dir2/dir2/dir2"));

  fbl::unique_fd fd(open(root.path().c_str(), O_DIRECTORY | O_RDONLY, 0777));
  ASSERT_TRUE(fd.is_valid());

  std::vector<std::string> dirs;
  ASSERT_TRUE(GetNestedDirectories(fd, &dirs));
  EXPECT_THAT(dirs, UnorderedElementsAreArray(expected_dirs));
}

TEST(FileUtilsTests, GetNestedFiles) {
  files::ScopedTempDir root;

  std::vector<std::string> expected_files;
  auto AddDir = [&](const std::string& relative_path) {
    return files::CreateDirectory(files::JoinPath(root.path(), relative_path));
  };
  auto AddFile = [&](const std::string& relative_path) {
    const std::string path = files::JoinPath(root.path(), relative_path);
    expected_files.push_back(IntoRelativePath(path, root));
    return files::WriteFile(path, "unused-content");
  };

  ASSERT_TRUE(AddFile("file0.txt"));

  ASSERT_TRUE(AddDir("dir0"));
  ASSERT_TRUE(AddFile("dir0/file0.txt"));

  ASSERT_TRUE(AddDir("dir1/dir0"));
  ASSERT_TRUE(AddFile("dir1/dir0/file0.txt"));
  ASSERT_TRUE(AddFile("dir1/dir0/file1.txt"));

  ASSERT_TRUE(AddDir("dir1/dir1"));
  ASSERT_TRUE(AddFile("dir1/dir1/file0.txt"));
  ASSERT_TRUE(AddFile("dir1/dir1/file1.txt"));

  fbl::unique_fd fd(open(root.path().c_str(), O_DIRECTORY | O_RDONLY, 0777));
  ASSERT_TRUE(fd.is_valid());

  std::vector<std::string> files;
  ASSERT_TRUE(GetNestedFiles(fd, &files));
  EXPECT_THAT(files, UnorderedElementsAreArray(expected_files));
}

TEST(FileUtilsTests, Migrate) {
  files::ScopedTempDir source_root;
  files::ScopedTempDir sink_root;

  fbl::unique_fd source_root_fd(fbl::unique_fd::InvalidValue());
  fbl::unique_fd sink_root_fd(fbl::unique_fd::InvalidValue());

  // Invalid |sink_root_fd|.
  ASSERT_FALSE(Migrate(source_root_fd, sink_root_fd));

  // Invalid |source_root_fd|.
  sink_root_fd.reset(open(sink_root.path().c_str(), O_DIRECTORY | O_RDWR, 0777));
  ASSERT_TRUE(Migrate(source_root_fd, sink_root_fd));

  source_root_fd.reset(open(source_root.path().c_str(), O_DIRECTORY | O_RDONLY, 0777));

  // Empty directory
  {
    ASSERT_TRUE(Migrate(source_root_fd, sink_root_fd));

    std::vector<std::string> source_dirs;
    ASSERT_TRUE(GetNestedDirectories(source_root_fd, &source_dirs));

    std::vector<std::string> sink_dirs;
    ASSERT_TRUE(GetNestedDirectories(sink_root_fd, &sink_dirs));

    EXPECT_EQ(source_dirs, sink_dirs);
  }

  // Expected files and directories after migration.
  std::vector<std::string> expected_dirs({"."});
  std::vector<std::string> expected_files;

  // Utility methods.
  auto Reset = [&] {
    expected_dirs.erase(std::remove(expected_dirs.begin(), expected_dirs.end(), "."),
                        expected_dirs.end());
    for (const auto& dir : expected_dirs) {
      if (!files::DeletePathAt(source_root_fd.get(), dir, /*recursive=*/true)) {
        return false;
      }
    }

    for (const auto& dir : expected_dirs) {
      if (!files::DeletePathAt(sink_root_fd.get(), dir, /*recursive=*/true)) {
        return false;
      }
    }

    expected_dirs = {"."};
    expected_files = {};

    return true;
  };
  auto AddDir = [&](const std::string& relative_path) {
    const std::string path = files::JoinPath(source_root.path(), relative_path);
    expected_dirs.push_back(IntoRelativePath(path, source_root));
    return files::CreateDirectory(path);
  };
  auto AddFile = [&](const std::string& relative_path) {
    const std::string path = files::JoinPath(source_root.path(), relative_path);
    expected_files.push_back(IntoRelativePath(path, source_root));
    return files::WriteFile(path, "unused-content");
  };

  // Check directory structure
  {
    Reset();

    ASSERT_TRUE(AddDir("dir0"));
    ASSERT_TRUE(AddDir("dir1"));
    ASSERT_TRUE(AddDir("dir2"));

    ASSERT_TRUE(AddDir("dir0/dir0"));
    ASSERT_TRUE(AddDir("dir1/dir0"));
    ASSERT_TRUE(AddDir("dir1/dir1"));

    ASSERT_TRUE(Migrate(source_root_fd, sink_root_fd));

    std::vector<std::string> sink_dirs;
    ASSERT_TRUE(GetNestedDirectories(sink_root_fd, &sink_dirs));
    EXPECT_THAT(sink_dirs, UnorderedElementsAreArray(expected_dirs));

    std::vector<std::string> source_dirs;
    ASSERT_TRUE(GetNestedDirectories(source_root_fd, &source_dirs));
    EXPECT_THAT(source_dirs, UnorderedElementsAreArray({"."}));
  }

  // Check files.
  {
    Reset();

    ASSERT_TRUE(AddDir("dir0"));
    ASSERT_TRUE(AddDir("dir1"));
    ASSERT_TRUE(AddDir("dir2"));

    ASSERT_TRUE(AddDir("dir0/dir0"));
    ASSERT_TRUE(AddDir("dir1/dir0"));
    ASSERT_TRUE(AddDir("dir1/dir1"));

    ASSERT_TRUE(AddFile("file0"));

    ASSERT_TRUE(AddFile("dir0/file0"));
    ASSERT_TRUE(AddFile("dir0/dir0/file0"));

    ASSERT_TRUE(AddFile("dir1/file0"));
    ASSERT_TRUE(AddFile("dir1/dir0/file0"));
    ASSERT_TRUE(AddFile("dir1/dir1/file1"));
    ASSERT_TRUE(AddFile("dir1/dir1/file0"));
    ASSERT_TRUE(AddFile("dir1/dir0/file1"));

    ASSERT_TRUE(AddFile("dir2/file0"));
    ASSERT_TRUE(AddFile("dir2/file1"));
    ASSERT_TRUE(AddFile("dir2/file2"));

    ASSERT_TRUE(Migrate(source_root_fd, sink_root_fd));

    std::vector<std::string> sink_dirs;
    ASSERT_TRUE(GetNestedDirectories(sink_root_fd, &sink_dirs));
    EXPECT_THAT(sink_dirs, UnorderedElementsAreArray(expected_dirs));

    std::vector<std::string> source_dirs;
    ASSERT_TRUE(GetNestedDirectories(source_root_fd, &source_dirs));
    EXPECT_THAT(source_dirs, UnorderedElementsAreArray({"."}));

    std::vector<std::string> sink_files;
    ASSERT_TRUE(GetNestedFiles(sink_root_fd, &sink_files));
    EXPECT_THAT(sink_files, UnorderedElementsAreArray(expected_files));

    std::vector<std::string> source_files;
    ASSERT_TRUE(GetNestedFiles(source_root_fd, &source_files));
    EXPECT_THAT(source_files, IsEmpty());

    for (const auto& file : sink_files) {
      std::string content;
      ASSERT_TRUE(files::ReadFileToStringAt(sink_root_fd.get(), file, &content));
      EXPECT_EQ(content, "unused-content");
    }
  }
}

}  // namespace
}  // namespace feedback
}  // namespace forensics
