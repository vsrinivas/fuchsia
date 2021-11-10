// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_TEST_COMPATIBILITY_COMPATIBILITY_H_
#define SRC_STORAGE_F2FS_TEST_COMPATIBILITY_COMPATIBILITY_H_

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

std::string GenerateTestPath(std::string_view format);

class TestFile {
 public:
  virtual ~TestFile() = default;

  virtual bool is_valid() = 0;

  virtual ssize_t Read(void *buf, size_t count) = 0;
  virtual ssize_t Write(const void *buf, size_t count) = 0;
  virtual int Fchmod(mode_t mode) = 0;
  virtual int Fstat(struct stat *file_stat) = 0;
  virtual int Ftruncate(off_t len) = 0;
};

class HostTestFile : public TestFile {
 public:
  explicit HostTestFile(int fd) : fd_(fd) {}

  bool is_valid() final;

  ssize_t Read(void *buf, size_t count) final;
  ssize_t Write(const void *buf, size_t count) final;
  int Fchmod(mode_t mode) final;
  int Fstat(struct stat *file_stat) final;
  int Ftruncate(off_t len) final;

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

  bool is_valid() final;

  ssize_t Read(void *buf, size_t count) final;
  ssize_t Write(const void *buf, size_t count) final;
  int Fchmod(mode_t mode) final;
  int Fstat(struct stat *file_stat) final;
  int Ftruncate(off_t len) final;

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
  virtual std::unique_ptr<TestFile> Open(std::string_view path, int flags, mode_t mode) = 0;

 protected:
  const std::string test_image_path_;
};

class HostOperator : public CompatibilityTestOperator {
 public:
  explicit HostOperator(std::string_view test_image_path, std::string_view mount_directory)
      : CompatibilityTestOperator(test_image_path), mount_directory_(mount_directory) {}

  void Mkfs() final;
  void Mount() final;
  void Unmount() final;
  void Fsck() final;

  void Mkdir(std::string_view path, mode_t mode) final;
  std::unique_ptr<TestFile> Open(std::string_view path, int flags, mode_t mode) final;

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

  void Mkfs() final;
  void Mount() final;
  void Unmount() final;
  void Fsck() final;

  void Mkdir(std::string_view path, mode_t mode) final;
  std::unique_ptr<TestFile> Open(std::string_view path, int flags, mode_t mode) final;

 private:
  fbl::unique_fd test_image_fd_;
  uint64_t block_count_;

  std::unique_ptr<F2fs> fs_;
  std::unique_ptr<Bcache> bcache_;
  fbl::RefPtr<VnodeF2fs> root_;
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_TEST_COMPATIBILITY_COMPATIBILITY_H_
