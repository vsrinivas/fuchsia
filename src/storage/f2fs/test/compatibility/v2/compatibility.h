// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_TEST_COMPATIBILITY_V2_COMPATIBILITY_H_
#define SRC_STORAGE_F2FS_TEST_COMPATIBILITY_V2_COMPATIBILITY_H_

#include <lib/fdio/fdio.h>
#include <lib/fit/defer.h>

#include <cinttypes>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <fbl/ref_ptr.h>

#include "src/storage/f2fs/f2fs.h"
#include "src/storage/f2fs/test/compatibility/v2/file_backed_block_device.h"
#include "src/virtualization/tests/lib/guest_test.h"

namespace f2fs {

constexpr size_t kTestBlockSize = 4096;
constexpr size_t kTestBlockCount = 25600;
constexpr size_t kTestBlockDeviceSize = kTestBlockSize * kTestBlockCount;

const std::string linux_path_prefix = "//";

class F2fsDebianGuest;

class TestFile {
 public:
  virtual ~TestFile() = default;

  virtual bool IsValid() const = 0;

  virtual ssize_t Read(void* buf, size_t count) = 0;
  virtual ssize_t Write(const void* buf, size_t count) = 0;
  virtual int Fchmod(mode_t mode) = 0;
  virtual int Fstat(struct stat* file_stat) = 0;
  virtual int Ftruncate(off_t len) = 0;
  virtual int Fallocate(int mode, off_t offset, off_t len) = 0;
};

class LinuxTestFile : public TestFile {
 public:
  explicit LinuxTestFile() {}

  bool IsValid() const final { return false; }

  ssize_t Read(void* buf, size_t count) final { return -1; }
  ssize_t Write(const void* buf, size_t count) final { return -1; }
  int Fchmod(mode_t mode) final { return -1; }
  int Fstat(struct stat* file_stat) final { return -1; }
  int Ftruncate(off_t len) final { return -1; }
  int Fallocate(int mode, off_t offset, off_t len) final { return -1; }
};

class FuchsiaTestFile : public TestFile {
 public:
  explicit FuchsiaTestFile(fbl::RefPtr<VnodeF2fs> vnode) : vnode_(std::move(vnode)) {}
  ~FuchsiaTestFile() {
    if (vnode_ != nullptr) {
      vnode_->Close();
    }
  }

  bool IsValid() const final { return (vnode_ != nullptr); }

  ssize_t Read(void* buf, size_t count) final { return -1; }
  ssize_t Write(const void* buf, size_t count) final { return -1; }
  int Fchmod(mode_t mode) final { return -1; }
  int Fstat(struct stat* file_stat) final { return -1; }
  int Ftruncate(off_t len) final { return -1; }
  int Fallocate(int mode, off_t offset, off_t len) final { return -1; }

  VnodeF2fs* GetRawVnodePtr() { return vnode_.get(); }

 private:
  fbl::RefPtr<VnodeF2fs> vnode_;
  // TODO: Add Lseek to adjust |offset_|
  [[maybe_unused]] size_t offset_ = 0;
};

class CompatibilityTestOperator {
 public:
  explicit CompatibilityTestOperator(std::string_view test_device) : test_device_(test_device) {}
  virtual ~CompatibilityTestOperator() = default;

  virtual void Mkfs() = 0;
  virtual void Fsck() = 0;
  virtual void Mount() = 0;
  virtual void Umount() = 0;

  virtual void Mkdir(std::string_view path, mode_t mode) = 0;
  // Return value is 0 on success, -1 on error.
  virtual int Rmdir(std::string_view path) = 0;
  virtual std::unique_ptr<TestFile> Open(std::string_view path, int flags, mode_t mode) = 0;
  virtual void Rename(std::string_view oldpath, std::string_view newpath) = 0;

 protected:
  const std::string test_device_;
};

class LinuxOperator : public CompatibilityTestOperator {
 public:
  explicit LinuxOperator(std::string_view test_device, F2fsDebianGuest* debian_guest)
      : CompatibilityTestOperator(test_device), debian_guest_(debian_guest) {}

  void Mkfs() final { Mkfs(std::string_view{""}); }
  void Mkfs(std::string_view opt);
  void Fsck() final;
  void Mount() final { Mount(std::string_view{""}); }
  void Mount(std::string_view opt);
  void Umount() final;

  void Mkdir(std::string_view path, mode_t mode) final;
  int Rmdir(std::string_view path) final { return -1; }
  std::unique_ptr<TestFile> Open(std::string_view path, int flags, mode_t mode) final {
    return std::unique_ptr<TestFile>(new LinuxTestFile());
  }
  void Rename(std::string_view oldpath, std::string_view newpath) final {}

  zx_status_t Execute(const std::vector<std::string>& argv, std::string* result = nullptr);
  void ExecuteWithAssert(const std::vector<std::string>& argv, std::string* result = nullptr);
  std::string ConvertPath(std::string_view path);

 private:
  F2fsDebianGuest* debian_guest_;
  const std::string mount_path_ = "compat_mnt";
};

class FuchsiaOperator : public CompatibilityTestOperator {
 public:
  explicit FuchsiaOperator(std::string_view test_device, size_t block_count, size_t block_size)
      : CompatibilityTestOperator(test_device), block_count_(block_count), block_size_(block_size) {
    auto fd = fbl::unique_fd(open(test_device_.c_str(), O_RDWR));
    auto device = std::make_unique<FileBackedBlockDevice>(std::move(fd), block_count_, block_size_);
    bool read_only = false;
    auto bc_or = CreateBcache(std::move(device), &read_only);
    if (bc_or.is_ok()) {
      bc_ = std::move(*bc_or);
    }
    loop_.StartThread();
  }
  ~FuchsiaOperator() {
    loop_.RunUntilIdle();
    loop_.Quit();
    loop_.JoinThreads();
  }

  void Mkfs() final { Mkfs(MkfsOptions{}); }
  void Mkfs(MkfsOptions opt);
  void Fsck() final;
  void Mount() final { Mount(MountOptions{}); }
  void Mount(MountOptions opt);
  void Umount() final;

  void Mkdir(std::string_view path, mode_t mode) final {}
  int Rmdir(std::string_view path) final { return -1; }
  std::unique_ptr<TestFile> Open(std::string_view path, int flags, mode_t mode) final;
  void Rename(std::string_view oldpath, std::string_view newpath) final {}

 private:
  size_t block_count_;
  size_t block_size_;
  std::unique_ptr<Bcache> bc_;
  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  std::unique_ptr<F2fs> fs_;
  fbl::RefPtr<VnodeF2fs> root_;
};

const std::string test_device_id = "f2fs_test_device";

class F2fsDebianGuest : public DebianEnclosedGuest {
 public:
  explicit F2fsDebianGuest(async::Loop& loop) : DebianEnclosedGuest(loop) {}

  zx_status_t BuildLaunchInfo(GuestLaunchInfo* launch_info) override {
    zx_status_t status = DebianEnclosedGuest::BuildLaunchInfo(launch_info);
    if (status != ZX_OK) {
      return status;
    }

    // Disable other virtio devices to ensure there's enough space on the PCI
    // bus, and to simplify slot assignment.
    launch_info->config.set_default_net(false);
    launch_info->config.set_virtio_balloon(false);
    launch_info->config.set_virtio_gpu(false);
    launch_info->config.set_virtio_rng(false);
    launch_info->config.set_virtio_sound(false);
    launch_info->config.set_virtio_vsock(false);

    auto* cfg = &launch_info->config;

    std::vector<fuchsia::virtualization::BlockSpec> block_specs;

    std::string guest_path = "/tmp/guest-test.XXXXXX";

    fbl::unique_fd fd(mkstemp(guest_path.data()));
    guest_path_ = guest_path;
    if (!fd) {
      FX_LOGS(ERROR) << "Failed to create temporary file";
      return ZX_ERR_IO;
    }
    if (auto status = ftruncate(fd.get(), kTestBlockDeviceSize); status != ZX_OK) {
      return status;
    }

    zx::channel channel;
    status = fdio_get_service_handle(fd.release(), channel.reset_and_get_address());
    if (status != ZX_OK) {
      return status;
    }
    block_specs.emplace_back(
        fuchsia::virtualization::BlockSpec{.id = test_device_id,
                                           .mode = fuchsia::virtualization::BlockMode::READ_WRITE,
                                           .format = fuchsia::virtualization::BlockFormat::FILE,
                                           .client = std::move(channel)});
    cfg->set_block_devices(std::move(block_specs));

    linux_operator_ = std::make_unique<LinuxOperator>(linux_device_path_, this);
    fuchsia_operator_ =
        std::make_unique<FuchsiaOperator>(guest_path_, kTestBlockCount, kTestBlockSize);

    return ZX_OK;
  }

  const std::string& GuestPath() { return guest_path_; }
  const std::string& LinuxDevicePath() { return linux_device_path_; }

  LinuxOperator& GetLinuxOperator() { return *linux_operator_; }
  FuchsiaOperator& GetFuchsiaOperator() { return *fuchsia_operator_; }

 private:
  std::string guest_path_;
  // Could be a different path on aarch64
  const std::string linux_device_path_ = "/dev/disk/by-id/virtio-" + test_device_id;

  std::unique_ptr<LinuxOperator> linux_operator_;
  std::unique_ptr<FuchsiaOperator> fuchsia_operator_;
};

fs::VnodeConnectionOptions ConvertFlag(int flags);

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_TEST_COMPATIBILITY_V2_COMPATIBILITY_H_
