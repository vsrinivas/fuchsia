// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/copier.h"

#include <unistd.h>
#include <zircon/errors.h>

#include <cerrno>
#include <filesystem>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"

namespace fshost {
namespace {

class CopierTest : public testing::Test {
 public:
  CopierTest()
      : src_dir_(std::filesystem::temp_directory_path() / "copier_test_src"),
        dst_dir_(std::filesystem::temp_directory_path() / "copier_test_dst") {}

  void SetUp() override {
    ASSERT_TRUE(files::CreateDirectory(src_dir_));
    ASSERT_TRUE(files::CreateDirectory(dst_dir_));
  }

  ~CopierTest() override {
    std::filesystem::remove_all(src_dir_);
    std::filesystem::remove_all(dst_dir_);
  }

  std::filesystem::path src_path(const std::filesystem::path& p) { return src_dir() / p; }
  const std::filesystem::path& src_dir() { return src_dir_; }

  std::filesystem::path dst_path(const std::filesystem::path& p) { return dst_dir() / p; }
  const std::filesystem::path& dst_dir() { return dst_dir_; }

  static std::string GetFileContents(const std::filesystem::path& path) {
    std::string contents;
    EXPECT_TRUE(files::ReadFileToString(path, &contents));
    return contents;
  }

  static bool WriteFileContents(const std::filesystem::path& path, const std::string& contents) {
    return files::WriteFile(path, contents.data(), static_cast<ssize_t>(contents.size()));
  }

  static bool DoesFileExist(const std::filesystem::path& path) {
    if (access(path.c_str(), F_OK) != 0) {
      if (errno != ENOENT) {
        ADD_FAILURE() << "IO error";
      }
      return false;
    }
    return true;
  }

 private:
  std::filesystem::path src_dir_;
  std::filesystem::path dst_dir_;
};

TEST_F(CopierTest, Copy) {
  ASSERT_TRUE(WriteFileContents(src_path("file1"), "hello1"));
  ASSERT_TRUE(files::CreateDirectory(src_path("dir")));
  ASSERT_TRUE(WriteFileContents(src_path("dir/file2"), "hello2"));

  fbl::unique_fd fd(open(src_dir().c_str(), O_RDONLY));
  ASSERT_TRUE(fd);

  auto data_or = Copier::Read(std::move(fd));
  ASSERT_TRUE(data_or.is_ok()) << data_or.status_string();

  fd = fbl::unique_fd(open(dst_dir().c_str(), O_RDONLY));
  ASSERT_TRUE(fd);

  EXPECT_EQ(data_or->Write(std::move(fd)), ZX_OK);

  EXPECT_EQ(GetFileContents(dst_path("file1")), "hello1");
  EXPECT_EQ(GetFileContents(dst_path("dir/file2")), "hello2");
}

TEST_F(CopierTest, ReadWithEmptyPathExcludedIsIgnored) {
  ASSERT_TRUE(WriteFileContents(src_path("file1"), "hello1"));
  ASSERT_TRUE(files::CreateDirectory(src_path("dir")));
  ASSERT_TRUE(WriteFileContents(src_path("dir/file2"), "hello2"));

  fbl::unique_fd fd(open(src_dir().c_str(), O_RDONLY));
  ASSERT_TRUE(fd);

  // The empty path would cause everything to be excluded which defeats the purpose of using the
  // copier so it's explicitly ignored.
  std::vector<std::filesystem::path> excluded_prefix_list = {""};
  auto data_or = Copier::Read(std::move(fd), excluded_prefix_list);
  ASSERT_TRUE(data_or.is_ok()) << data_or.status_string();

  fd = fbl::unique_fd(open(dst_dir().c_str(), O_RDONLY));
  ASSERT_TRUE(fd);

  EXPECT_EQ(data_or->Write(std::move(fd)), ZX_OK);

  EXPECT_TRUE(DoesFileExist(dst_path("file1")));
  EXPECT_TRUE(DoesFileExist(dst_path("dir")));
  EXPECT_TRUE(DoesFileExist(dst_path("dir/file2")));
}

TEST_F(CopierTest, ReadWithFileExcludedDoesNotCopyFile) {
  ASSERT_TRUE(files::CreateDirectory(src_path("dir")));
  ASSERT_TRUE(WriteFileContents(src_path("dir/file"), "hello"));

  fbl::unique_fd fd(open(src_dir().c_str(), O_RDONLY));
  ASSERT_TRUE(fd);

  // Exact exclusion match.
  std::vector<std::filesystem::path> excluded_prefix_list = {"dir/file"};
  auto data_or = Copier::Read(std::move(fd), excluded_prefix_list);
  ASSERT_TRUE(data_or.is_ok()) << data_or.status_string();

  fd = fbl::unique_fd(open(dst_dir().c_str(), O_RDONLY));
  ASSERT_TRUE(fd);

  EXPECT_EQ(data_or->Write(std::move(fd)), ZX_OK);

  // The parent directory wasn't matched, only the file was.
  EXPECT_TRUE(DoesFileExist(dst_path("dir")));
  EXPECT_FALSE(DoesFileExist(dst_path("dir/file")));
}

TEST_F(CopierTest, ReadWithParentExcludedDoesNotCopyChild) {
  ASSERT_TRUE(files::CreateDirectory(src_path("dir")));
  ASSERT_TRUE(WriteFileContents(src_path("dir/file"), "hello"));

  fbl::unique_fd fd(open(src_dir().c_str(), O_RDONLY));
  ASSERT_TRUE(fd);

  std::vector<std::filesystem::path> excluded_prefix_list = {"dir"};
  auto data_or = Copier::Read(std::move(fd), excluded_prefix_list);
  ASSERT_TRUE(data_or.is_ok()) << data_or.status_string();

  fd = fbl::unique_fd(open(dst_dir().c_str(), O_RDONLY));
  ASSERT_TRUE(fd);

  EXPECT_EQ(data_or->Write(std::move(fd)), ZX_OK);

  EXPECT_FALSE(DoesFileExist(dst_path("dir")));
  EXPECT_FALSE(DoesFileExist(dst_path("dir/file")));
}

TEST_F(CopierTest, ReadWithGrandparentExcludedDoesNotCopyGrandchild) {
  ASSERT_TRUE(files::CreateDirectory(src_path("dir1")));
  ASSERT_TRUE(files::CreateDirectory(src_path("dir1/dir2")));
  ASSERT_TRUE(WriteFileContents(src_path("dir1/dir2/file"), "hello"));

  fbl::unique_fd fd(open(src_dir().c_str(), O_RDONLY));
  ASSERT_TRUE(fd);

  std::vector<std::filesystem::path> excluded_prefix_list = {"dir1"};
  auto data_or = Copier::Read(std::move(fd), excluded_prefix_list);
  ASSERT_TRUE(data_or.is_ok()) << data_or.status_string();

  fd = fbl::unique_fd(open(dst_dir().c_str(), O_RDONLY));
  ASSERT_TRUE(fd);

  EXPECT_EQ(data_or->Write(std::move(fd)), ZX_OK);

  EXPECT_FALSE(DoesFileExist(dst_path("dir1")));
}

TEST_F(CopierTest, ReadWithExclusionDoesNotMatchPartialNames) {
  ASSERT_TRUE(WriteFileContents(src_path("filename"), "hello"));

  fbl::unique_fd fd(open(src_dir().c_str(), O_RDONLY));
  ASSERT_TRUE(fd);

  // "file" does not match "filename".
  std::vector<std::filesystem::path> excluded_prefix_list = {"file"};
  auto data_or = Copier::Read(std::move(fd), excluded_prefix_list);
  ASSERT_TRUE(data_or.is_ok()) << data_or.status_string();

  fd = fbl::unique_fd(open(dst_dir().c_str(), O_RDONLY));
  ASSERT_TRUE(fd);

  EXPECT_EQ(data_or->Write(std::move(fd)), ZX_OK);

  EXPECT_TRUE(DoesFileExist(dst_path("filename")));
}

TEST_F(CopierTest, ReadWithExclusionDoesNotMatchSuffixes) {
  ASSERT_TRUE(files::CreateDirectory(src_path("dir")));
  ASSERT_TRUE(WriteFileContents(src_path("dir/file"), "hello"));

  fbl::unique_fd fd(open(src_dir().c_str(), O_RDONLY));
  ASSERT_TRUE(fd);

  // "file" does not match "dir/file".
  std::vector<std::filesystem::path> excluded_prefix_list = {"file"};
  auto data_or = Copier::Read(std::move(fd), excluded_prefix_list);
  ASSERT_TRUE(data_or.is_ok()) << data_or.status_string();

  fd = fbl::unique_fd(open(dst_dir().c_str(), O_RDONLY));
  ASSERT_TRUE(fd);

  EXPECT_EQ(data_or->Write(std::move(fd)), ZX_OK);

  EXPECT_TRUE(DoesFileExist(dst_path("dir/file")));
}

TEST_F(CopierTest, InsertFileWorks) {
  Copier copier;
  // File at root.
  EXPECT_EQ(copier.InsertFile("file1.txt", "hello1").status_value(), ZX_OK);
  // Create a parent directory.
  EXPECT_EQ(copier.InsertFile("dir1/file2.txt", "hello2").status_value(), ZX_OK);
  // Create a file in an existing directory.
  EXPECT_EQ(copier.InsertFile("dir1/file3.txt", "hello3").status_value(), ZX_OK);
  // Create a parent directory in another directory.
  EXPECT_EQ(copier.InsertFile("dir1/dir2/file4.txt", "hello4").status_value(), ZX_OK);

  fbl::unique_fd fd = fbl::unique_fd(open(dst_dir().c_str(), O_RDONLY));
  ASSERT_TRUE(fd);
  EXPECT_EQ(copier.Write(std::move(fd)), ZX_OK);

  EXPECT_EQ(GetFileContents(dst_path("file1.txt")), "hello1");
  EXPECT_EQ(GetFileContents(dst_path("dir1/file2.txt")), "hello2");
  EXPECT_EQ(GetFileContents(dst_path("dir1/file3.txt")), "hello3");
  EXPECT_EQ(GetFileContents(dst_path("dir1/dir2/file4.txt")), "hello4");
}

TEST_F(CopierTest, InsertFileWithExistingFileIsAnError) {
  Copier copier;
  EXPECT_EQ(copier.InsertFile("file1.txt", "hello1").status_value(), ZX_OK);
  EXPECT_EQ(copier.InsertFile("file1.txt", "hello2").status_value(), ZX_ERR_ALREADY_EXISTS);

  EXPECT_EQ(copier.InsertFile("dir1/file2.txt", "hello3").status_value(), ZX_OK);
  EXPECT_EQ(copier.InsertFile("dir1/file2.txt", "hello4").status_value(), ZX_ERR_ALREADY_EXISTS);

  fbl::unique_fd fd = fbl::unique_fd(open(dst_dir().c_str(), O_RDONLY));
  ASSERT_TRUE(fd);
  EXPECT_EQ(copier.Write(std::move(fd)), ZX_OK);

  EXPECT_EQ(GetFileContents(dst_path("file1.txt")), "hello1");
  EXPECT_EQ(GetFileContents(dst_path("dir1/file2.txt")), "hello3");
}

TEST_F(CopierTest, InsertFileWithFileAtParentDirectoryIsAnError) {
  Copier copier;
  EXPECT_EQ(copier.InsertFile("foo", "hello1").status_value(), ZX_OK);
  EXPECT_EQ(copier.InsertFile("foo/file1.txt", "hello1").status_value(), ZX_ERR_BAD_STATE);
}

TEST_F(CopierTest, InsertFileWithEndingSlashIsAnError) {
  Copier copier;
  EXPECT_EQ(copier.InsertFile("file1/", "hello1").status_value(), ZX_ERR_INVALID_ARGS);
}

TEST_F(CopierTest, InsertFileWithAbsolutePathIsAnError) {
  Copier copier;
  EXPECT_EQ(copier.InsertFile("/file1.txt", "hello1").status_value(), ZX_ERR_INVALID_ARGS);
}

}  // namespace
}  // namespace fshost
