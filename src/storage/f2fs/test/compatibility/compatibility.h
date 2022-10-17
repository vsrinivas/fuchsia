// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_TEST_COMPATIBILITY_COMPATIBILITY_H_
#define SRC_STORAGE_F2FS_TEST_COMPATIBILITY_COMPATIBILITY_H_

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <variant>

#include <gtest/gtest.h>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

std::string GenerateTestPath(std::string_view format);

class TestFile {
 public:
  virtual ~TestFile() = default;

  virtual bool is_valid() const = 0;

  virtual ssize_t Read(void *buf, size_t count) = 0;
  virtual ssize_t Write(const void *buf, size_t count) = 0;
  virtual int Fchmod(mode_t mode) = 0;
  virtual int Fstat(struct stat *file_stat) = 0;
  virtual int Ftruncate(off_t len) = 0;
  virtual int Fallocate(int mode, off_t offset, off_t len) = 0;
};

class HostTestFile : public TestFile {
 public:
  explicit HostTestFile(int fd) : fd_(fd) {}

  bool is_valid() const final;

  ssize_t Read(void *buf, size_t count) final;
  ssize_t Write(const void *buf, size_t count) final;
  int Fchmod(mode_t mode) final;
  int Fstat(struct stat *file_stat) final;
  int Ftruncate(off_t len) final;
  int Fallocate(int mode, off_t offset, off_t len) final;

 private:
  fbl::unique_fd fd_;
};

class TargetTestFile : public TestFile {
 public:
  explicit TargetTestFile(fbl::RefPtr<VnodeF2fs> vnode) : vnode_(std::move(vnode)) {}
  ~TargetTestFile() {
    if (vnode_ != nullptr) {
      vnode_->Close();
    }
  }

  bool is_valid() const final;

  ssize_t Read(void *buf, size_t count) final;
  ssize_t Write(const void *buf, size_t count) final;
  int Fchmod(mode_t mode) final;
  int Fstat(struct stat *file_stat) final;
  int Ftruncate(off_t len) final;
  int Fallocate(int mode, off_t offset, off_t len) final;

  VnodeF2fs *GetRawVnodePtr() { return vnode_.get(); }

 private:
  fbl::RefPtr<VnodeF2fs> vnode_;
  // TODO: Add Lseek to adjust |offset_|
  size_t offset_ = 0;
};

class CompatibilityTestOperator {
 public:
  explicit CompatibilityTestOperator(std::string_view test_image_path)
      : test_image_path_(test_image_path) {}
  virtual ~CompatibilityTestOperator() = default;

  virtual void Mkfs() = 0;
  virtual void Mount() = 0;
  virtual void Unmount() = 0;
  virtual void Fsck() = 0;

  virtual void Mkdir(std::string_view path, mode_t mode) = 0;
  // Return value is 0 on success, -1 on error.
  virtual int Rmdir(std::string_view path) = 0;
  virtual std::unique_ptr<TestFile> Open(std::string_view path, int flags, mode_t mode) = 0;
  virtual void Rename(std::string_view oldpath, std::string_view newpath) = 0;

 protected:
  const std::string test_image_path_;
};

class HostOperator : public CompatibilityTestOperator {
 public:
  explicit HostOperator(std::string_view test_image_path, std::string_view mount_directory)
      : CompatibilityTestOperator(test_image_path), mount_directory_(mount_directory) {}

  void Mkfs() { Mkfs(std::string_view{}); }
  void Mkfs(std::string_view opt);
  void Mount() { Mount(std::string_view{}); }
  void Mount(std::string_view opt);
  void Unmount() final;
  void Fsck() final;

  void Mkdir(std::string_view path, mode_t mode) final;
  int Rmdir(std::string_view path) final;
  std::unique_ptr<TestFile> Open(std::string_view path, int flags, mode_t mode) final;
  void Rename(std::string_view oldpath, std::string_view newpath) final;

  std::string GetAbsolutePath(std::string_view path) {
    if (path[0] != '/') {
      return std::string(mount_directory_).append("/").append(path);
    }

    return std::string(mount_directory_).append(path);
  }

 private:
  const std::string mount_directory_;
};

class TargetOperator : public CompatibilityTestOperator {
 public:
  explicit TargetOperator(std::string_view test_image_path, fbl::unique_fd test_image_fd,
                          uint64_t block_count)
      : CompatibilityTestOperator(test_image_path),
        test_image_fd_(std::move(test_image_fd)),
        block_count_(block_count) {}

  void Mkfs() final { Mkfs(MkfsOptions{}); }
  void Mkfs(MkfsOptions opt);
  void Mount() final { Mount(MountOptions{}); }
  void Mount(MountOptions opt);
  void Unmount() final;
  void Fsck() final;

  void Mkdir(std::string_view path, mode_t mode) final;
  int Rmdir(std::string_view path) final;
  std::unique_ptr<TestFile> Open(std::string_view path, int flags, mode_t mode) final;
  void Rename(std::string_view oldpath, std::string_view newpath) final;

 protected:
  zx::result<std::pair<fbl::RefPtr<fs::Vnode>, std::string>> GetLastDirVnodeAndFileName(
      std::string_view absolute_path);

 private:
  fbl::unique_fd test_image_fd_;
  uint64_t block_count_;

  std::unique_ptr<F2fs> fs_;
  std::unique_ptr<Bcache> bcache_;
  fbl::RefPtr<VnodeF2fs> root_;
};

inline constexpr std::string_view kTestFileFormat = "f2fs_file.XXXXXX";

class CompatibilityTest : public testing::Test {
 public:
  CompatibilityTest() {
    constexpr uint64_t kBlockCount = 819200;  // 400MB
    constexpr uint64_t kDiskSize = kBlockCount * kDefaultSectorSize;

    test_image_path_ = GenerateTestPath(kTestFileFormat);
    fbl::unique_fd test_image_fd_ = fbl::unique_fd(mkstemp(test_image_path_.data()));
    ftruncate(test_image_fd_.get(), kDiskSize);

    mount_directory_ = GenerateTestPath(kTestFileFormat);
    mkdtemp(mount_directory_.data());

    host_operator_ = std::make_unique<HostOperator>(test_image_path_, mount_directory_);
    target_operator_ =
        std::make_unique<TargetOperator>(test_image_path_, std::move(test_image_fd_), kBlockCount);
  }

  ~CompatibilityTest() {
    unlink(test_image_path_.c_str());
    rmdir(mount_directory_.c_str());
  }

 protected:
  std::string test_image_path_;
  std::string mount_directory_;

  std::unique_ptr<HostOperator> host_operator_;
  std::unique_ptr<TargetOperator> target_operator_;
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_TEST_COMPATIBILITY_COMPATIBILITY_H_
