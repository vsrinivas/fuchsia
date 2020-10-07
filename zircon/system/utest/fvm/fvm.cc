// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/partition/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/unsafe.h>
#include <lib/memfs/memfs.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/fifo.h>
#include <lib/zx/vmo.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <zircon/device/block.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <climits>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <utility>

#include <blobfs/format.h>
#include <block-client/client.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/function.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fs-management/fvm.h>
#include <fs-management/mount.h>
#include <fvm/format.h>
#include <fvm/fvm-check.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

#include "src/storage/minfs/format.h"
#include "zircon/errors.h"

#define FVM_DRIVER_LIB "/boot/driver/fvm.so"
#define STRLEN(s) (sizeof(s) / sizeof((s)[0]))

namespace {

/////////////////////// Helper functions for creating FVM:

using filesystem_info_t = fuchsia_io_FilesystemInfo;
using volume_info_t = fuchsia_hardware_block_volume_VolumeInfo;

constexpr char kTmpfsPath[] = "/fvm-tmp";
constexpr char kMountPath[] = "/fvm-tmp/minfs_test_mountpath";
constexpr char kTestDevPath[] = "/fake/dev";

typedef struct {
  const char* name;
  size_t number;
} partition_entry_t;

class FvmTest : public zxtest::Test {
 public:
  void SetUp() override {
    devmgr_launcher::Args args = devmgr_integration_test::IsolatedDevmgr::DefaultArgs();
    args.disable_block_watcher = true;
    args.disable_netsvc = true;
    args.driver_search_paths.push_back("/boot/driver");
    args.path_prefix = "/pkg/";
    ASSERT_OK(devmgr_integration_test::IsolatedDevmgr::Create(std::move(args), &devmgr_));
    ASSERT_OK(
        wait_for_device_at(devfs_root().get(), "misc/ramctl", zx::duration::infinite().get()));

    ASSERT_OK(loop_.StartThread());
    zx::channel memfs_root;
    ASSERT_OK(
        memfs_create_filesystem(loop_.dispatcher(), &memfs_, memfs_root.reset_and_get_address()));

    fdio_ns_t* name_space;
    ASSERT_OK(fdio_ns_get_installed(&name_space));

    ASSERT_OK(fdio_ns_bind(name_space, kTmpfsPath, memfs_root.release()));
    ASSERT_OK(fdio_ns_bind_fd(name_space, kTestDevPath, devmgr_.devfs_root().get()));

    ASSERT_EQ(mkdir(kMountPath, 0666), 0);
    mounting_options_.register_fs = false;
  }

  const fbl::unique_fd& devfs_root() const { return devmgr_.devfs_root(); }

  void TearDown() override {
    fdio_ns_t* name_space;
    ASSERT_OK(fdio_ns_get_installed(&name_space));
    ASSERT_OK(fdio_ns_unbind(name_space, kTestDevPath));
    ASSERT_OK(fdio_ns_unbind(name_space, kTmpfsPath));

    sync_completion_t unmounted;
    memfs_free_filesystem(memfs_, &unmounted);
    ASSERT_OK(ramdisk_destroy(ramdisk_));
    ASSERT_OK(sync_completion_wait(&unmounted, zx::duration::infinite().get()));
  }

  fbl::unique_fd fvm_device() const { return fbl::unique_fd(open(fvm_driver_path_, O_RDWR)); }

  const char* fvm_path() const { return fvm_driver_path_; }

  fbl::unique_fd ramdisk_device() const { return fbl::unique_fd(open(ramdisk_path_, O_RDWR)); }

  const ramdisk_client* ramdisk() const { return ramdisk_; }

  const char* ramdisk_path() const { return ramdisk_path_; }

  void FVMRebind(const partition_entry_t* entries, size_t entry_count);

  void CreateFVM(uint64_t block_size, uint64_t block_count, uint64_t slice_size);

  void CreateRamdisk(uint64_t block_size, uint64_t block_count);

 protected:
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  devmgr_integration_test::IsolatedDevmgr devmgr_;
  memfs_filesystem_t* memfs_ = nullptr;
  ramdisk_client_t* ramdisk_ = nullptr;
  mount_options_t mounting_options_ = default_mount_options;
  char ramdisk_path_[PATH_MAX] = {};
  char fvm_driver_path_[PATH_MAX] = {};
};

void FvmTest::CreateRamdisk(uint64_t block_size, uint64_t block_count) {
  ASSERT_OK(ramdisk_create_at(devfs_root().get(), block_size, block_count, &ramdisk_));
  snprintf(ramdisk_path_, PATH_MAX, "%s/%s", kTestDevPath, ramdisk_get_path(ramdisk_));
}

void FvmTest::CreateFVM(uint64_t block_size, uint64_t block_count, uint64_t slice_size) {
  CreateRamdisk(block_size, block_count);

  fbl::unique_fd fd(open(ramdisk_path_, O_RDWR));
  ASSERT_TRUE(fd);

  ASSERT_OK(fvm_init_preallocated(fd.get(), block_count * block_size, block_count * block_size,
                                  slice_size));

  zx::channel fvm_channel;
  ASSERT_OK(fdio_get_service_handle(fd.get(), fvm_channel.reset_and_get_address()));

  auto resp = ::llcpp::fuchsia::device::Controller::Call::Bind(
      zx::unowned_channel(fvm_channel.get()),
      ::fidl::StringView(FVM_DRIVER_LIB, STRLEN(FVM_DRIVER_LIB)));
  ASSERT_OK(resp.status());
  ASSERT_TRUE(resp->result.is_response());
  fvm_channel.reset();

  snprintf(fvm_driver_path_, PATH_MAX, "%s/fvm", ramdisk_path_);
  ASSERT_OK(wait_for_device(fvm_driver_path_, zx::duration::infinite().get()));
}

void FvmTest::FVMRebind(const partition_entry_t* entries, size_t entry_count) {
  fdio_cpp::UnownedFdioCaller disk_caller(ramdisk_get_block_fd(ramdisk_));

  auto resp = ::llcpp::fuchsia::device::Controller::Call::Rebind(
      zx::unowned_channel(disk_caller.borrow_channel()),
      ::fidl::StringView(FVM_DRIVER_LIB, STRLEN(FVM_DRIVER_LIB)));
  ASSERT_OK(resp.status());
  ASSERT_TRUE(resp->result.is_response());

  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/fvm", ramdisk_path_);
  ASSERT_OK(wait_for_device(path, zx::duration::infinite().get()));

  for (size_t i = 0; i < entry_count; i++) {
    snprintf(path, sizeof(path), "%s/fvm/%s-p-%zu/block", ramdisk_path_, entries[i].name,
             entries[i].number);
    ASSERT_OK(wait_for_device(path, zx::duration::infinite().get()));
  }
}

void FVMCheckSliceSize(const fbl::unique_fd& fd, size_t expected_slice_size) {
  ASSERT_TRUE(fd);
  volume_info_t volume_info;
  ASSERT_EQ(fvm_query(fd.get(), &volume_info), ZX_OK, "Failed to query fvm\n");
  ASSERT_EQ(expected_slice_size, volume_info.slice_size, "Unexpected slice size\n");
}

void FVMCheckAllocatedCount(const fbl::unique_fd& fd, size_t expected_allocated,
                            size_t expected_total) {
  volume_info_t volume_info;
  ASSERT_EQ(fvm_query(fd.get(), &volume_info), ZX_OK);
  ASSERT_EQ(volume_info.pslice_total_count, expected_total);
  ASSERT_EQ(volume_info.pslice_allocated_count, expected_allocated);
}

enum class ValidationResult {
  Valid,
  Corrupted,
};

void ValidateFVM(fbl::unique_fd fd, ValidationResult result = ValidationResult::Valid) {
  ASSERT_TRUE(fd);
  fdio_cpp::UnownedFdioCaller disk_caller(fd.get());
  fuchsia_hardware_block_BlockInfo block_info;
  zx_status_t status;
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(disk_caller.borrow_channel(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  fvm::Checker checker(std::move(fd), block_info.block_size, true);
  switch (result) {
    case ValidationResult::Valid:
      ASSERT_TRUE(checker.Validate());
      break;
    default:
      ASSERT_FALSE(checker.Validate());
  }
}

/////////////////////// Helper functions, definitions

constexpr uint8_t kTestUniqueGUID[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                       0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
constexpr uint8_t kTestUniqueGUID2[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

// Intentionally avoid aligning these GUIDs with
// the actual system GUIDs; otherwise, limited versions
// of Fuchsia may attempt to actually mount these
// partitions automatically.

// clang-format off
#define GUID_TEST_DATA_VALUE                                                                       \
    {                                                                                              \
        0xAA, 0xFF, 0xBB, 0x00, 0x33, 0x44, 0x88, 0x99,                                            \
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,                                            \
    }

#define GUID_TEST_BLOB_VALUE                                                                       \
    {                                                                                              \
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,                                            \
        0xAA, 0xFF, 0xBB, 0x00, 0x33, 0x44, 0x88, 0x99,                                            \
    }

#define GUID_TEST_SYS_VALUE                                                                        \
    {                                                                                              \
        0xEE, 0xFF, 0xBB, 0x00, 0x33, 0x44, 0x88, 0x99,                                            \
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,                                            \
    }
// clang-format on

constexpr char kTestPartName1[] = "data";
constexpr uint8_t kTestPartGUIDData[] = {0xAA, 0xFF, 0xBB, 0x00, 0x33, 0x44, 0x88, 0x99,
                                         0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};

constexpr char kTestPartName2[] = "blob";
constexpr uint8_t kTestPartGUIDBlob[] = GUID_TEST_BLOB_VALUE;

constexpr char kTestPartName3[] = "system";
constexpr uint8_t kTestPartGUIDSystem[] = GUID_TEST_SYS_VALUE;

class VmoBuf;

class VmoClient : public fbl::RefCounted<VmoClient> {
 public:
  static void Create(int fd, fbl::RefPtr<VmoClient>* out);

  ~VmoClient();
  void CheckWrite(VmoBuf* vbuf, size_t buf_off, size_t dev_off, size_t len);
  void CheckRead(VmoBuf* vbuf, size_t buf_off, size_t dev_off, size_t len);
  void Transaction(block_fifo_request_t* requests, size_t count) {
    ASSERT_OK(block_fifo_txn(client_, &requests[0], count));
  }

  int fd() const { return fd_; }
  groupid_t group() { return 0; }

 private:
  int fd_;
  fuchsia_hardware_block_BlockInfo info_;
  fifo_client_t* client_;
};

class VmoBuf {
 public:
  static void Create(fbl::RefPtr<VmoClient> client, size_t size, std::unique_ptr<VmoBuf>* out) {
    std::unique_ptr<uint8_t[]> buf(new uint8_t[size]);

    zx::vmo vmo;
    ASSERT_EQ(zx::vmo::create(size, 0, &vmo), ZX_OK);
    zx::vmo xfer_vmo;
    ASSERT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &xfer_vmo), ZX_OK);

    fdio_cpp::UnownedFdioCaller disk_connection(client->fd());
    zx::unowned_channel channel(disk_connection.borrow_channel());
    fuchsia_hardware_block_VmoId vmoid;
    zx_status_t status;
    ASSERT_EQ(
        fuchsia_hardware_block_BlockAttachVmo(channel->get(), xfer_vmo.release(), &status, &vmoid),
        ZX_OK);
    ASSERT_EQ(status, ZX_OK);

    std::unique_ptr<VmoBuf> vb(
        new VmoBuf(std::move(client), std::move(vmo), std::move(buf), vmoid));
    *out = std::move(vb);
  }

  ~VmoBuf() {
    if (vmo_.is_valid()) {
      block_fifo_request_t request;
      request.group = client_->group();
      request.vmoid = vmoid_.id;
      request.opcode = BLOCKIO_CLOSE_VMO;
      client_->Transaction(&request, 1);
    }
  }

 private:
  friend VmoClient;

  VmoBuf(fbl::RefPtr<VmoClient> client, zx::vmo vmo, std::unique_ptr<uint8_t[]> buf,
         fuchsia_hardware_block_VmoId vmoid)
      : client_(std::move(client)), vmo_(std::move(vmo)), buf_(std::move(buf)), vmoid_(vmoid) {}

  fbl::RefPtr<VmoClient> client_;
  zx::vmo vmo_;
  std::unique_ptr<uint8_t[]> buf_;
  fuchsia_hardware_block_VmoId vmoid_;
};

void VmoClient::Create(int fd, fbl::RefPtr<VmoClient>* out) {
  fbl::RefPtr<VmoClient> vc = fbl::AdoptRef(new VmoClient());

  fdio_cpp::UnownedFdioCaller disk_connection(fd);
  zx::unowned_channel channel(disk_connection.borrow_channel());
  zx_status_t status;

  zx::fifo fifo;
  ASSERT_EQ(
      fuchsia_hardware_block_BlockGetFifo(channel->get(), &status, fifo.reset_and_get_address()),
      ZX_OK);
  ASSERT_EQ(status, ZX_OK);

  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(channel->get(), &status, &vc->info_), ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(block_fifo_create_client(fifo.release(), &vc->client_), ZX_OK);
  vc->fd_ = fd;
  *out = std::move(vc);
}

VmoClient::~VmoClient() {
  fdio_cpp::UnownedFdioCaller disk_connection(fd());
  zx_status_t status;
  fuchsia_hardware_block_BlockCloseFifo(disk_connection.borrow_channel(), &status);
  block_fifo_release_client(client_);
}

void VmoClient::CheckWrite(VmoBuf* vbuf, size_t buf_off, size_t dev_off, size_t len) {
  // Write to the client-side buffer
  for (size_t i = 0; i < len; i++)
    vbuf->buf_[i + buf_off] = static_cast<uint8_t>(rand());

  // Write to the registered VMO
  ASSERT_EQ(vbuf->vmo_.write(&vbuf->buf_[buf_off], buf_off, len), ZX_OK);

  // Write to the block device
  block_fifo_request_t request;
  request.group = group();
  request.vmoid = vbuf->vmoid_.id;
  request.opcode = BLOCKIO_WRITE;
  ASSERT_EQ(len % info_.block_size, 0);
  ASSERT_EQ(buf_off % info_.block_size, 0);
  ASSERT_EQ(dev_off % info_.block_size, 0);
  request.length = static_cast<uint32_t>(len / info_.block_size);
  request.vmo_offset = buf_off / info_.block_size;
  request.dev_offset = dev_off / info_.block_size;
  Transaction(&request, 1);
}

void VmoClient::CheckRead(VmoBuf* vbuf, size_t buf_off, size_t dev_off, size_t len) {
  // Create a comparison buffer
  fbl::AllocChecker ac;
  std::unique_ptr<uint8_t[]> out(new (&ac) uint8_t[len]);
  ASSERT_TRUE(ac.check());
  memset(out.get(), 0, len);

  // Read from the block device
  block_fifo_request_t request;
  request.group = group();
  request.vmoid = vbuf->vmoid_.id;
  request.opcode = BLOCKIO_READ;
  ASSERT_EQ(len % info_.block_size, 0);
  ASSERT_EQ(buf_off % info_.block_size, 0);
  ASSERT_EQ(dev_off % info_.block_size, 0);
  request.length = static_cast<uint32_t>(len / info_.block_size);
  request.vmo_offset = buf_off / info_.block_size;
  request.dev_offset = dev_off / info_.block_size;
  Transaction(&request, 1);

  // Read from the registered VMO
  ASSERT_EQ(vbuf->vmo_.read(out.get(), buf_off, len), ZX_OK);

  ASSERT_EQ(memcmp(&vbuf->buf_[buf_off], out.get(), len), 0);
}

void CheckWrite(int fd, size_t off, size_t len, uint8_t* buf) {
  for (size_t i = 0; i < len; i++) {
    buf[i] = static_cast<uint8_t>(rand());
  }
  ASSERT_EQ(lseek(fd, off, SEEK_SET), static_cast<ssize_t>(off));
  ASSERT_EQ(write(fd, buf, len), static_cast<ssize_t>(len));
}

void CheckRead(int fd, size_t off, size_t len, const uint8_t* in) {
  fbl::AllocChecker ac;
  std::unique_ptr<uint8_t[]> out(new (&ac) uint8_t[len]);
  ASSERT_TRUE(ac.check());
  memset(out.get(), 0, len);
  ASSERT_EQ(lseek(fd, off, SEEK_SET), static_cast<ssize_t>(off));
  ASSERT_EQ(read(fd, out.get(), len), static_cast<ssize_t>(len));
  ASSERT_EQ(memcmp(in, out.get(), len), 0);
}

void CheckWriteReadBlock(int fd, size_t block, size_t count) {
  fdio_cpp::UnownedFdioCaller disk_connection(fd);
  zx_status_t status;
  fuchsia_hardware_block_BlockInfo block_info;
  ASSERT_EQ(
      fuchsia_hardware_block_BlockGetInfo(disk_connection.borrow_channel(), &status, &block_info),
      ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  size_t len = block_info.block_size * count;
  size_t off = block_info.block_size * block;
  std::unique_ptr<uint8_t[]> in(new uint8_t[len]);
  CheckWrite(fd, off, len, in.get());
  CheckRead(fd, off, len, in.get());
}

void CheckNoAccessBlock(int fd, size_t block, size_t count) {
  fdio_cpp::UnownedFdioCaller disk_connection(fd);
  zx_status_t status;
  fuchsia_hardware_block_BlockInfo block_info;
  ASSERT_EQ(
      fuchsia_hardware_block_BlockGetInfo(disk_connection.borrow_channel(), &status, &block_info),
      ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  std::unique_ptr<uint8_t[]> buf(new uint8_t[block_info.block_size * count]);
  size_t len = block_info.block_size * count;
  size_t off = block_info.block_size * block;
  for (size_t i = 0; i < len; i++)
    buf[i] = static_cast<uint8_t>(rand());
  ASSERT_EQ(lseek(fd, off, SEEK_SET), static_cast<ssize_t>(off));
  ASSERT_EQ(write(fd, buf.get(), len), -1);
  ASSERT_EQ(lseek(fd, off, SEEK_SET), static_cast<ssize_t>(off));
  ASSERT_EQ(read(fd, buf.get(), len), -1);
}

void CheckDeadConnection(int fd) {
  lseek(fd, 0, SEEK_SET);
  bool is_dead = ((errno == EBADF) || (errno == EPIPE));
  ASSERT_TRUE(is_dead);
}

void Upgrade(const fdio_cpp::FdioCaller& caller, const uint8_t* old_guid, const uint8_t* new_guid,
             zx_status_t result) {
  fuchsia_hardware_block_partition_GUID old_guid_fidl;
  memcpy(&old_guid_fidl.value, old_guid, fuchsia_hardware_block_partition_GUID_LENGTH);
  fuchsia_hardware_block_partition_GUID new_guid_fidl;
  memcpy(&new_guid_fidl.value, new_guid, fuchsia_hardware_block_partition_GUID_LENGTH);

  zx_status_t status;
  zx_status_t io_status = fuchsia_hardware_block_volume_VolumeManagerActivate(
      caller.borrow_channel(), &old_guid_fidl, &new_guid_fidl, &status);
  ASSERT_EQ(ZX_OK, io_status);
  ASSERT_EQ(result, status);
}

/////////////////////// Actual tests:

// Test initializing the FVM on a partition that is smaller than a slice
TEST_F(FvmTest, TestTooSmall) {
  uint64_t block_size = 512;
  uint64_t block_count = (1 << 15);

  CreateRamdisk(block_size, block_count);
  fbl::unique_fd fd(ramdisk_device());
  ASSERT_TRUE(fd);
  size_t slice_size = block_size * block_count;
  ASSERT_EQ(fvm_init(fd.get(), slice_size), ZX_ERR_NO_SPACE);
  ValidateFVM(ramdisk_device(), ValidationResult::Corrupted);
}

// Test initializing the FVM on a large partition, with metadata size > the max transfer size
TEST_F(FvmTest, TestLarge) {
  char fvm_path[PATH_MAX];
  uint64_t block_size = 512;
  uint64_t block_count = 8 * (1 << 20);
  CreateRamdisk(block_size, block_count);
  fbl::unique_fd fd(ramdisk_device());
  ASSERT_TRUE(fd);

  size_t slice_size = 16 * (1 << 10);
  fvm::Header fvm_header =
      fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions, block_size * block_count, slice_size);

  fdio_cpp::UnownedFdioCaller disk_connection(fd.get());
  zx::unowned_channel channel(disk_connection.borrow_channel());
  zx_status_t status;
  fuchsia_hardware_block_BlockInfo block_info;
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(channel->get(), &status, &block_info), ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_LT(block_info.max_transfer_size, fvm_header.GetMetadataAllocatedBytes());

  ASSERT_EQ(fvm_init(fd.get(), slice_size), ZX_OK);

  auto resp = ::llcpp::fuchsia::device::Controller::Call::Bind(
      zx::unowned_channel(channel->get()),
      ::fidl::StringView(FVM_DRIVER_LIB, STRLEN(FVM_DRIVER_LIB)));
  ASSERT_OK(resp.status());
  ASSERT_TRUE(resp->result.is_response());

  snprintf(fvm_path, sizeof(fvm_path), "%s/fvm", ramdisk_path());
  ASSERT_OK(wait_for_device(fvm_path, zx::duration::infinite().get()));
  ValidateFVM(ramdisk_device());
}

// Load and unload an empty FVM
TEST_F(FvmTest, TestEmpty) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  FVMCheckSliceSize(fvm_device(), kSliceSize);
  ValidateFVM(ramdisk_device());
}

// Test allocating a single partition
TEST_F(FvmTest, TestAllocateOne) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd = fvm_device();
  ASSERT_TRUE(fd);

  // Allocate one VPart
  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  request.slice_count = 1;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  memcpy(request.type, kTestPartGUIDData, BLOCK_GUID_LEN);
  fbl::unique_fd vp_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(vp_fd);

  // Check that the name matches what we provided
  char name[fvm::kMaxVPartitionNameLength + 1];
  fdio_cpp::UnownedFdioCaller partition_connection(vp_fd.get());

  zx_status_t status;
  size_t actual;
  ASSERT_EQ(fuchsia_hardware_block_partition_PartitionGetName(partition_connection.borrow_channel(),
                                                              &status, name, sizeof(name), &actual),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  name[actual] = '\0';
  ASSERT_EQ(memcmp(name, kTestPartName1, strlen(kTestPartName1)), 0);

  // Check that we can read from / write to it.
  CheckWriteReadBlock(vp_fd.get(), 0, 1);

  // Try accessing the block again after closing / re-opening it.
  ASSERT_EQ(close(vp_fd.release()), 0);
  vp_fd.reset(open_partition_with_devfs(devfs_root().get(), kTestUniqueGUID, kTestPartGUIDData, 0,
                                        nullptr));
  ASSERT_TRUE(vp_fd, "Couldn't re-open Data VPart");
  CheckWriteReadBlock(vp_fd.get(), 0, 1);

  ASSERT_EQ(close(vp_fd.release()), 0);
  ASSERT_EQ(close(fd.release()), 0);
  FVMCheckSliceSize(fvm_device(), kSliceSize);
  ValidateFVM(ramdisk_device());
}

// Test allocating a collection of partitions
TEST_F(FvmTest, TestAllocateMany) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd = fvm_device();
  ASSERT_TRUE(fd);

  // Test allocation of multiple VPartitions
  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  request.slice_count = 1;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  memcpy(request.type, kTestPartGUIDData, BLOCK_GUID_LEN);
  fbl::unique_fd data_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(data_fd);

  strcpy(request.name, kTestPartName2);
  memcpy(request.type, kTestPartGUIDBlob, BLOCK_GUID_LEN);
  fbl::unique_fd blob_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(blob_fd);

  strcpy(request.name, kTestPartName3);
  memcpy(request.type, kTestPartGUIDSystem, BLOCK_GUID_LEN);
  fbl::unique_fd sys_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(sys_fd);

  CheckWriteReadBlock(data_fd.get(), 0, 1);
  CheckWriteReadBlock(blob_fd.get(), 0, 1);
  CheckWriteReadBlock(sys_fd.get(), 0, 1);

  ASSERT_EQ(close(data_fd.release()), 0);
  ASSERT_EQ(close(blob_fd.release()), 0);
  ASSERT_EQ(close(sys_fd.release()), 0);
  ASSERT_EQ(close(fd.release()), 0);
  FVMCheckSliceSize(fvm_device(), kSliceSize);
  ValidateFVM(ramdisk_device());
}

// Test allocating additional slices to a vpartition.
TEST_F(FvmTest, TestVPartitionExtend) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd = fvm_device();
  ASSERT_TRUE(fd);

  volume_info_t volume_info;
  ASSERT_EQ(fvm_query(fd.get(), &volume_info), ZX_OK);
  size_t slice_size = volume_info.slice_size;
  constexpr uint64_t kDiskSize = kBlockSize * kBlockCount;
  size_t slices_total = fvm::UsableSlicesCount(kDiskSize, slice_size);
  size_t slices_left = slices_total;

  FVMCheckAllocatedCount(fd, slices_total - slices_left, slices_total);

  // Allocate one VPart
  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  size_t slice_count = 1;
  request.slice_count = slice_count;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  memcpy(request.type, kTestPartGUIDData, BLOCK_GUID_LEN);
  fbl::unique_fd vp_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(vp_fd, "Couldn't open Volume");
  slices_left--;
  FVMCheckAllocatedCount(fd, slices_total - slices_left, slices_total);

  // Confirm that the disk reports the correct number of slices
  fdio_cpp::FdioCaller partition_caller(std::move(vp_fd));
  zx::unowned_channel partition_channel(partition_caller.borrow_channel());
  fuchsia_hardware_block_BlockInfo block_info;
  zx_status_t status;
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(block_info.block_count * block_info.block_size, slice_size * slice_count);

  // Try re-allocating an already allocated vslice
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), 0, 1, &status),
            ZX_OK);
  ASSERT_NE(status, ZX_OK, "Expected request failure");
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(block_info.block_count * block_info.block_size, slice_size * slice_count);

  // Try again with a portion of the request which is unallocated
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), 0, 2, &status),
            ZX_OK);
  ASSERT_NE(status, ZX_OK, "Expected request failure");

  // Allocate OBSCENELY too many slices
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), slice_count,
                                                       std::numeric_limits<size_t>::max(), &status),
            ZX_OK);
  ASSERT_NE(status, ZX_OK, "Expected request failure");

  // Allocate slices at a too-large offset
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(
                partition_channel->get(), std::numeric_limits<size_t>::max(), 1, &status),
            ZX_OK);
  ASSERT_NE(status, ZX_OK, "Expected request failure");

  // Attempt to allocate slightly too many slices
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), slice_count,
                                                       slices_left + 1, &status),
            ZX_OK);
  ASSERT_NE(status, ZX_OK, "Expected request failure");

  // The number of free slices should be unchanged.
  FVMCheckAllocatedCount(fd, slices_total - slices_left, slices_total);

  // Allocate exactly the remaining number of slices
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), slice_count,
                                                       slices_left, &status),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);

  slice_count += slices_left;
  slices_left = 0;
  FVMCheckAllocatedCount(fd, slices_total - slices_left, slices_total);
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(block_info.block_count * block_info.block_size, slice_size * slice_count);

  // We can't allocate any more to this VPartition
  ASSERT_EQ(
      fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), slice_count, 1, &status),
      ZX_OK);
  ASSERT_NE(status, ZX_OK, "Expected request failure");

  // We can't allocate a new VPartition
  strcpy(request.name, kTestPartName2);
  memcpy(request.type, kTestPartGUIDBlob, BLOCK_GUID_LEN);
  ASSERT_LT(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request), 0,
            "Expected VPart allocation failure");

  FVMCheckSliceSize(fvm_device(), kSliceSize);
  ValidateFVM(ramdisk_device());
}

// Test allocating very sparse VPartition
TEST_F(FvmTest, TestVPartitionExtendSparse) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd = fvm_device();
  ASSERT_TRUE(fd);

  size_t slices_left = fvm::UsableSlicesCount(kBlockSize * kBlockCount, kSliceSize);

  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  request.slice_count = 1;
  slices_left--;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  memcpy(request.type, kTestPartGUIDData, BLOCK_GUID_LEN);
  fbl::unique_fd vp_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(vp_fd);
  CheckWriteReadBlock(vp_fd.get(), 0, 1);

  // Double check that we can access a block at this vslice address
  // (this isn't always possible; for certain slice sizes, blocks may be
  // allocatable / freeable, but not addressable).
  size_t bno = (fvm::kMaxVSlices - 1) * (kSliceSize / kBlockSize);
  ASSERT_EQ(bno / (kSliceSize / kBlockSize), (fvm::kMaxVSlices - 1), "bno overflowed");
  ASSERT_EQ((bno * kBlockSize) / kBlockSize, bno, "block access will overflow");

  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd.get());
  zx::unowned_channel partition_channel(partition_caller.borrow_channel());
  zx_status_t status;

  // Try allocating at a location that's slightly too large
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), fvm::kMaxVSlices,
                                                       1, &status),
            ZX_OK);
  ASSERT_NE(status, ZX_OK, "Expected request failure");

  // Try allocating at the largest offset
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(),
                                                       fvm::kMaxVSlices - 1, 1, &status),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  CheckWriteReadBlock(vp_fd.get(), bno, 1);

  // Try freeing beyond largest offset
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeShrink(partition_channel->get(), fvm::kMaxVSlices,
                                                       1, &status),
            ZX_OK);
  ASSERT_NE(status, ZX_OK, "Expected request failure");
  CheckWriteReadBlock(vp_fd.get(), bno, 1);

  // Try freeing at the largest offset
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeShrink(partition_channel->get(),
                                                       fvm::kMaxVSlices - 1, 1, &status),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  CheckNoAccessBlock(vp_fd.get(), bno, 1);

  ASSERT_EQ(close(vp_fd.release()), 0);
  ASSERT_EQ(close(fd.release()), 0);
  FVMCheckSliceSize(fvm_device(), kSliceSize);
  ValidateFVM(ramdisk_device());
}

// Test removing slices from a VPartition.
TEST_F(FvmTest, TestVPartitionShrink) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd(fvm_device());
  ASSERT_TRUE(fd);

  volume_info_t volume_info;
  ASSERT_EQ(fvm_query(fd.get(), &volume_info), ZX_OK);
  size_t slice_size = volume_info.slice_size;
  const size_t kDiskSize = kBlockSize * kBlockCount;
  size_t slices_total = fvm::UsableSlicesCount(kDiskSize, slice_size);
  size_t slices_left = slices_total;

  FVMCheckAllocatedCount(fd, slices_total - slices_left, slices_total);

  // Allocate one VPart
  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  size_t slice_count = 1;
  request.slice_count = slice_count;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  memcpy(request.type, kTestPartGUIDData, BLOCK_GUID_LEN);
  fbl::unique_fd vp_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(vp_fd, "Couldn't open Volume");
  slices_left--;

  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd.get());
  zx::unowned_channel partition_channel(partition_caller.borrow_channel());
  zx_status_t status;

  // Confirm that the disk reports the correct number of slices
  fuchsia_hardware_block_BlockInfo block_info;
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(block_info.block_count * block_info.block_size, slice_size * slice_count);
  CheckWriteReadBlock(vp_fd.get(), (slice_size / block_info.block_size) - 1, 1);
  CheckNoAccessBlock(vp_fd.get(), (slice_size / block_info.block_size) - 1, 2);
  FVMCheckAllocatedCount(fd, slices_total - slices_left, slices_total);

  // Try shrinking the 0th vslice
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeShrink(partition_channel->get(), 0, 1, &status),
            ZX_OK);
  ASSERT_NE(status, ZX_OK, "Expected request failure");

  // Try no-op requests (length = 0).
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), 1, 0, &status),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeShrink(partition_channel->get(), 1, 0, &status),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(block_info.block_count * block_info.block_size, slice_size * slice_count);

  // Try again with a portion of the request which is unallocated
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeShrink(partition_channel->get(), 1, 2, &status),
            ZX_OK);
  ASSERT_NE(status, ZX_OK);
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(block_info.block_count * block_info.block_size, slice_size * slice_count);
  FVMCheckAllocatedCount(fd, slices_total - slices_left, slices_total);

  // Allocate exactly the remaining number of slices
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), slice_count,
                                                       slices_left, &status),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  slice_count += slices_left;
  slices_left = 0;

  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(block_info.block_count * block_info.block_size, slice_size * slice_count);
  CheckWriteReadBlock(vp_fd.get(), (slice_size / block_info.block_size) - 1, 1);
  CheckWriteReadBlock(vp_fd.get(), (slice_size / block_info.block_size) - 1, 2);
  FVMCheckAllocatedCount(fd, slices_total - slices_left, slices_total);

  // We can't allocate any more to this VPartition
  ASSERT_EQ(
      fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), slice_count, 1, &status),
      ZX_OK);
  ASSERT_NE(status, ZX_OK);

  // Try to shrink off the end (okay, since SOME of the slices are allocated)
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeShrink(partition_channel->get(), 1, slice_count + 3,
                                                       &status),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  FVMCheckAllocatedCount(fd, 1, slices_total);

  // The same request to shrink should now fail (NONE of the slices are
  // allocated)
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeShrink(partition_channel->get(), 1, slice_count - 1,
                                                       &status),
            ZX_OK);
  ASSERT_NE(status, ZX_OK);
  FVMCheckAllocatedCount(fd, 1, slices_total);

  // ... unless we re-allocate and try again.
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), 1, slice_count - 1,
                                                       &status),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeShrink(partition_channel->get(), 1, slice_count - 1,
                                                       &status),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);

  FVMCheckSliceSize(fvm_device(), kSliceSize);
  ValidateFVM(ramdisk_device());
}

// Test splitting a contiguous slice extent into multiple parts
TEST_F(FvmTest, TestVPartitionSplit) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);

  fbl::unique_fd fd(fvm_device());
  ASSERT_TRUE(fd);

  volume_info_t volume_info;
  ASSERT_EQ(fvm_query(fd.get(), &volume_info), ZX_OK);
  size_t slice_size = volume_info.slice_size;
  size_t slices_left = fvm::UsableSlicesCount(kBlockSize * kBlockCount, kSliceSize);

  // Allocate one VPart
  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  size_t slice_count = 5;
  request.slice_count = slice_count;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  memcpy(request.type, kTestPartGUIDData, BLOCK_GUID_LEN);
  fbl::unique_fd vp_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(vp_fd);
  slices_left--;

  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd.get());
  zx::unowned_channel partition_channel(partition_caller.borrow_channel());

  // Confirm that the disk reports the correct number of slices
  fuchsia_hardware_block_BlockInfo block_info;
  zx_status_t status;
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(block_info.block_count * block_info.block_size, slice_size * slice_count);

  extend_request_t reset_erequest;
  reset_erequest.offset = 1;
  reset_erequest.length = slice_count - 1;
  extend_request_t mid_erequest;
  mid_erequest.offset = 2;
  mid_erequest.length = 1;
  extend_request_t start_erequest;
  start_erequest.offset = 1;
  start_erequest.length = 1;
  extend_request_t end_erequest;
  end_erequest.offset = 3;
  end_erequest.length = slice_count - 3;

  auto verifyExtents = [&](bool start, bool mid, bool end) {
    size_t start_block = start_erequest.offset * (slice_size / block_info.block_size);
    size_t mid_block = mid_erequest.offset * (slice_size / block_info.block_size);
    size_t end_block = end_erequest.offset * (slice_size / block_info.block_size);

    if (start) {
      CheckWriteReadBlock(vp_fd.get(), start_block, 1);
    } else {
      CheckNoAccessBlock(vp_fd.get(), start_block, 1);
    }
    if (mid) {
      CheckWriteReadBlock(vp_fd.get(), mid_block, 1);
    } else {
      CheckNoAccessBlock(vp_fd.get(), mid_block, 1);
    }
    if (end) {
      CheckWriteReadBlock(vp_fd.get(), end_block, 1);
    } else {
      CheckNoAccessBlock(vp_fd.get(), end_block, 1);
    }
    return true;
  };

  auto doExtend = [](const zx::unowned_channel& partition_channel, extend_request_t request) {
    zx_status_t status;
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), request.offset,
                                                         request.length, &status),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);
  };

  auto doShrink = [](const zx::unowned_channel& partition_channel, extend_request_t request) {
    zx_status_t status;
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeShrink(partition_channel->get(), request.offset,
                                                         request.length, &status),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);
  };

  // We should be able to split the extent.
  verifyExtents(true, true, true);
  doShrink(partition_channel, mid_erequest);
  verifyExtents(true, false, true);
  doShrink(partition_channel, start_erequest);
  verifyExtents(false, false, true);
  doShrink(partition_channel, end_erequest);
  verifyExtents(false, false, false);

  doExtend(partition_channel, reset_erequest);

  doShrink(partition_channel, start_erequest);
  verifyExtents(false, true, true);
  doShrink(partition_channel, mid_erequest);
  verifyExtents(false, false, true);
  doShrink(partition_channel, end_erequest);
  verifyExtents(false, false, false);

  doExtend(partition_channel, reset_erequest);

  doShrink(partition_channel, end_erequest);
  verifyExtents(true, true, false);
  doShrink(partition_channel, mid_erequest);
  verifyExtents(true, false, false);
  doShrink(partition_channel, start_erequest);
  verifyExtents(false, false, false);

  doExtend(partition_channel, reset_erequest);

  doShrink(partition_channel, end_erequest);
  verifyExtents(true, true, false);
  doShrink(partition_channel, start_erequest);
  verifyExtents(false, true, false);
  doShrink(partition_channel, mid_erequest);
  verifyExtents(false, false, false);

  // We should also be able to combine extents
  doExtend(partition_channel, mid_erequest);
  verifyExtents(false, true, false);
  doExtend(partition_channel, start_erequest);
  verifyExtents(true, true, false);
  doExtend(partition_channel, end_erequest);
  verifyExtents(true, true, true);

  doShrink(partition_channel, reset_erequest);

  doExtend(partition_channel, end_erequest);
  verifyExtents(false, false, true);
  doExtend(partition_channel, mid_erequest);
  verifyExtents(false, true, true);
  doExtend(partition_channel, start_erequest);
  verifyExtents(true, true, true);

  doShrink(partition_channel, reset_erequest);

  doExtend(partition_channel, end_erequest);
  verifyExtents(false, false, true);
  doExtend(partition_channel, start_erequest);
  verifyExtents(true, false, true);
  doExtend(partition_channel, mid_erequest);
  verifyExtents(true, true, true);

  ASSERT_EQ(close(vp_fd.release()), 0);
  ASSERT_EQ(close(fd.release()), 0);
  FVMCheckSliceSize(fvm_device(), kSliceSize);
  ValidateFVM(ramdisk_device());
}

// Test removing VPartitions within an FVM
TEST_F(FvmTest, TestVPartitionDestroy) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd = fvm_device();
  ASSERT_TRUE(fd);

  // Test allocation of multiple VPartitions
  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  request.slice_count = 1;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  memcpy(request.type, kTestPartGUIDData, BLOCK_GUID_LEN);
  fbl::unique_fd data_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(data_fd);
  fdio_cpp::UnownedFdioCaller data_caller(data_fd.get());
  zx::unowned_channel data_channel(data_caller.borrow_channel());

  strcpy(request.name, kTestPartName2);
  memcpy(request.type, kTestPartGUIDBlob, BLOCK_GUID_LEN);
  fbl::unique_fd blob_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(blob_fd);
  fdio_cpp::UnownedFdioCaller blob_caller(blob_fd.get());
  zx::unowned_channel blob_channel(blob_caller.borrow_channel());

  strcpy(request.name, kTestPartName3);
  memcpy(request.type, kTestPartGUIDSystem, BLOCK_GUID_LEN);
  fbl::unique_fd sys_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(sys_fd);
  fdio_cpp::UnownedFdioCaller sys_caller(sys_fd.get());
  zx::unowned_channel sys_channel(sys_caller.borrow_channel());

  // We can access all three...
  CheckWriteReadBlock(data_fd.get(), 0, 1);
  CheckWriteReadBlock(blob_fd.get(), 0, 1);
  CheckWriteReadBlock(sys_fd.get(), 0, 1);

  // But not after we destroy the blob partition.
  zx_status_t status;
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeDestroy(blob_channel->get(), &status), ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  CheckWriteReadBlock(data_fd.get(), 0, 1);
  CheckWriteReadBlock(sys_fd.get(), 0, 1);
  CheckDeadConnection(blob_fd.get());

  // Destroy the other two VPartitions.
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeDestroy(data_channel->get(), &status), ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  CheckWriteReadBlock(sys_fd.get(), 0, 1);
  CheckDeadConnection(data_fd.get());

  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeDestroy(sys_channel->get(), &status), ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  CheckDeadConnection(sys_fd.get());

  ASSERT_EQ(close(fd.release()), 0);

  FVMCheckSliceSize(fvm_device(), kSliceSize);
}

TEST_F(FvmTest, TestVPartitionQuery) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd(fvm_device());
  ASSERT_TRUE(fd);

  // Allocate partition
  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  request.slice_count = 10;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  memcpy(request.type, kTestPartGUIDData, BLOCK_GUID_LEN);
  fbl::unique_fd part_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(part_fd);
  fdio_cpp::FdioCaller partition_caller(std::move(part_fd));
  zx::unowned_channel partition_channel(partition_caller.borrow_channel());

  // Create non-contiguous extent.
  zx_status_t status;
  uint64_t offset = 20;
  uint64_t length = 10;
  ASSERT_EQ(
      fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), offset, length, &status),
      ZX_OK);
  ASSERT_EQ(status, ZX_OK);

  volume_info_t volume_info;
  ASSERT_EQ(fvm_query(fd.get(), &volume_info), ZX_OK);

  // Query various vslice ranges
  uint64_t start_slices[6];
  start_slices[0] = 0;
  start_slices[1] = 10;
  start_slices[2] = 20;
  start_slices[3] = 50;
  start_slices[4] = 25;
  start_slices[5] = 15;

  // Check response from partition query
  fuchsia_hardware_block_volume_VsliceRange
      ranges[fuchsia_hardware_block_volume_MAX_SLICE_REQUESTS];
  size_t actual_ranges_count;
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeQuerySlices(partition_channel->get(), start_slices,
                                                            std::size(start_slices), &status,
                                                            ranges, &actual_ranges_count),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(actual_ranges_count, std::size(start_slices));
  ASSERT_TRUE(ranges[0].allocated);
  ASSERT_EQ(ranges[0].count, 10);
  ASSERT_FALSE(ranges[1].allocated);
  ASSERT_EQ(ranges[1].count, 10);
  ASSERT_TRUE(ranges[2].allocated);
  ASSERT_EQ(ranges[2].count, 10);
  ASSERT_FALSE(ranges[3].allocated);
  ASSERT_EQ(ranges[3].count, volume_info.vslice_count - 50);
  ASSERT_TRUE(ranges[4].allocated);
  ASSERT_EQ(ranges[4].count, 5);
  ASSERT_FALSE(ranges[5].allocated);
  ASSERT_EQ(ranges[5].count, 5);

  // Merge the extents!
  offset = 10;
  length = 10;
  ASSERT_EQ(
      fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), offset, length, &status),
      ZX_OK);
  ASSERT_EQ(status, ZX_OK);

  // Check partition query response again after extend
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeQuerySlices(partition_channel->get(), start_slices,
                                                            std::size(start_slices), &status,
                                                            ranges, &actual_ranges_count),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(actual_ranges_count, std::size(start_slices));
  ASSERT_TRUE(ranges[0].allocated);
  ASSERT_EQ(ranges[0].count, 30);
  ASSERT_TRUE(ranges[1].allocated);
  ASSERT_EQ(ranges[1].count, 20);
  ASSERT_TRUE(ranges[2].allocated);
  ASSERT_EQ(ranges[2].count, 10);
  ASSERT_FALSE(ranges[3].allocated);
  ASSERT_EQ(ranges[3].count, volume_info.vslice_count - 50);
  ASSERT_TRUE(ranges[4].allocated);
  ASSERT_EQ(ranges[4].count, 5);
  ASSERT_TRUE(ranges[5].allocated);
  ASSERT_EQ(ranges[5].count, 15);

  start_slices[0] = volume_info.vslice_count + 1;
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeQuerySlices(partition_channel->get(), start_slices,
                                                            std::size(start_slices), &status,
                                                            ranges, &actual_ranges_count),
            ZX_OK);
  ASSERT_EQ(status, ZX_ERR_OUT_OF_RANGE);

  ASSERT_EQ(close(fd.release()), 0);
  FVMCheckSliceSize(fvm_device(), kSliceSize);
}

// Test allocating and accessing slices which are allocated contiguously.
TEST_F(FvmTest, TestSliceAccessContiguous) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd = fvm_device();
  ASSERT_TRUE(fd);

  volume_info_t volume_info;
  ASSERT_EQ(fvm_query(fd.get(), &volume_info), ZX_OK);
  size_t slice_size = volume_info.slice_size;

  // Allocate one VPart
  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  request.slice_count = 1;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  memcpy(request.type, kTestPartGUIDData, BLOCK_GUID_LEN);
  fbl::unique_fd vp_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(vp_fd);

  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd.get());
  zx::unowned_channel partition_channel(partition_caller.borrow_channel());
  fuchsia_hardware_block_BlockInfo block_info;
  zx_status_t status;
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);

  // This is the last 'accessible' block.
  size_t last_block = (slice_size / block_info.block_size) - 1;

  {
    fbl::RefPtr<VmoClient> vc;
    VmoClient::Create(vp_fd.get(), &vc);
    std::unique_ptr<VmoBuf> vb;
    VmoBuf::Create(vc, block_info.block_size * 2, &vb);
    vc->CheckWrite(vb.get(), 0, block_info.block_size * last_block, block_info.block_size);
    vc->CheckRead(vb.get(), 0, block_info.block_size * last_block, block_info.block_size);

    // Try writing out of bounds -- check that we don't have access.
    CheckNoAccessBlock(vp_fd.get(), (slice_size / block_info.block_size) - 1, 2);
    CheckNoAccessBlock(vp_fd.get(), slice_size / block_info.block_size, 1);

    // Attempt to access the next contiguous slice
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), 1, 1, &status),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);

    // Now we can access the next slice...
    vc->CheckWrite(vb.get(), block_info.block_size, block_info.block_size * (last_block + 1),
                   block_info.block_size);
    vc->CheckRead(vb.get(), block_info.block_size, block_info.block_size * (last_block + 1),
                  block_info.block_size);
    // ... We can still access the previous slice...
    vc->CheckRead(vb.get(), 0, block_info.block_size * last_block, block_info.block_size);
    // ... And we can cross slices
    vc->CheckRead(vb.get(), 0, block_info.block_size * last_block, block_info.block_size * 2);
  }

  ASSERT_EQ(close(vp_fd.release()), 0);
  ASSERT_EQ(close(fd.release()), 0);
  FVMCheckSliceSize(fvm_device(), kSliceSize);
}

// Test allocating and accessing multiple (3+) slices at once.
TEST_F(FvmTest, TestSliceAccessMany) {
  // The size of a slice must be carefully constructed for this test
  // so that we can hold multiple slices in memory without worrying
  // about hitting resource limits.
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 20;
  constexpr uint64_t kBlocksPerSlice = 256;
  constexpr uint64_t kSliceSize = kBlocksPerSlice * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd(fvm_device());
  ASSERT_TRUE(fd);

  volume_info_t volume_info;
  ASSERT_EQ(fvm_query(fd.get(), &volume_info), ZX_OK);
  ASSERT_EQ(volume_info.slice_size, kSliceSize);

  // Allocate one VPart
  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  request.slice_count = 1;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  memcpy(request.type, kTestPartGUIDData, BLOCK_GUID_LEN);
  fbl::unique_fd vp_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(vp_fd);

  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd.get());
  zx::unowned_channel partition_channel(partition_caller.borrow_channel());
  fuchsia_hardware_block_BlockInfo block_info;
  zx_status_t status;
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(block_info.block_size, kBlockSize);

  {
    fbl::RefPtr<VmoClient> vc;
    VmoClient::Create(vp_fd.get(), &vc);
    std::unique_ptr<VmoBuf> vb;
    VmoBuf::Create(vc, kSliceSize * 3, &vb);

    // Access the first slice
    vc->CheckWrite(vb.get(), 0, 0, kSliceSize);
    vc->CheckRead(vb.get(), 0, 0, kSliceSize);

    // Try writing out of bounds -- check that we don't have access.
    CheckNoAccessBlock(vp_fd.get(), kBlocksPerSlice - 1, 2);
    CheckNoAccessBlock(vp_fd.get(), kBlocksPerSlice, 1);

    // Attempt to access the next contiguous slices
    uint64_t offset = 1;
    uint64_t length = 2;
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), offset, length,
                                                         &status),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);

    // Now we can access the next slices...
    vc->CheckWrite(vb.get(), kSliceSize, kSliceSize, 2 * kSliceSize);
    vc->CheckRead(vb.get(), kSliceSize, kSliceSize, 2 * kSliceSize);
    // ... We can still access the previous slice...
    vc->CheckRead(vb.get(), 0, 0, kSliceSize);
    // ... And we can cross slices for reading.
    vc->CheckRead(vb.get(), 0, 0, 3 * kSliceSize);

    // Also, we can cross slices for writing.
    vc->CheckWrite(vb.get(), 0, 0, 3 * kSliceSize);
    vc->CheckRead(vb.get(), 0, 0, 3 * kSliceSize);

    // Additionally, we can access "parts" of slices in a multi-slice
    // operation. Here, read one block into the first slice, and read
    // up to the last block in the final slice.
    vc->CheckWrite(vb.get(), 0, kBlockSize, 3 * kSliceSize - 2 * kBlockSize);
    vc->CheckRead(vb.get(), 0, kBlockSize, 3 * kSliceSize - 2 * kBlockSize);
  }

  ASSERT_EQ(close(vp_fd.release()), 0);
  ASSERT_EQ(close(fd.release()), 0);
  FVMCheckSliceSize(fvm_device(), kSliceSize);
  ValidateFVM(ramdisk_device());
}

// Test allocating and accessing slices which are allocated
// virtually contiguously (they appear sequential to the client) but are
// actually noncontiguous on the FVM partition.
TEST_F(FvmTest, TestSliceAccessNonContiguousPhysical) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = kBlockSize * 64;
  constexpr uint64_t kDiskSize = kBlockSize * kBlockCount;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd(fvm_device());
  ASSERT_TRUE(fd);

  volume_info_t volume_info;
  ASSERT_EQ(fvm_query(fd.get(), &volume_info), ZX_OK);

  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  request.slice_count = 1;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);

  constexpr size_t kNumVParts = 3;
  typedef struct vdata {
    fbl::unique_fd fd;
    uint8_t guid[BLOCK_GUID_LEN];
    char name[32];
    size_t slices_used;
  } vdata_t;

  vdata_t vparts[kNumVParts] = {
      {fbl::unique_fd(), GUID_TEST_DATA_VALUE, "data", request.slice_count},
      {fbl::unique_fd(), GUID_TEST_BLOB_VALUE, "blob", request.slice_count},
      {fbl::unique_fd(), GUID_TEST_SYS_VALUE, "sys", request.slice_count},
  };

  for (size_t i = 0; i < std::size(vparts); i++) {
    strcpy(request.name, vparts[i].name);
    memcpy(request.type, vparts[i].guid, BLOCK_GUID_LEN);
    vparts[i].fd.reset(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
    ASSERT_TRUE(vparts[i].fd);
  }

  fdio_cpp::UnownedFdioCaller partition_caller(vparts[0].fd.get());
  zx::unowned_channel partition_channel(partition_caller.borrow_channel());
  fuchsia_hardware_block_BlockInfo block_info;
  zx_status_t status;
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);

  size_t usable_slices_per_vpart = fvm::UsableSlicesCount(kDiskSize, kSliceSize) / kNumVParts;
  size_t i = 0;
  while (vparts[i].slices_used < usable_slices_per_vpart) {
    int vfd = vparts[i].fd.get();
    // This is the last 'accessible' block.
    size_t last_block = (vparts[i].slices_used * (kSliceSize / block_info.block_size)) - 1;
    fbl::RefPtr<VmoClient> vc;
    VmoClient::Create(vfd, &vc);
    std::unique_ptr<VmoBuf> vb;
    VmoBuf::Create(vc, block_info.block_size * 2, &vb);

    vc->CheckWrite(vb.get(), 0, block_info.block_size * last_block, block_info.block_size);
    vc->CheckRead(vb.get(), 0, block_info.block_size * last_block, block_info.block_size);

    // Try writing out of bounds -- check that we don't have access.
    CheckNoAccessBlock(vfd, last_block, 2);
    CheckNoAccessBlock(vfd, last_block + 1, 1);

    // Attempt to access the next contiguous slice
    fdio_cpp::UnownedFdioCaller partition_caller(vfd);
    zx::unowned_channel partition_channel(partition_caller.borrow_channel());
    uint64_t offset = vparts[i].slices_used;
    uint64_t length = 1;
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), offset, length,
                                                         &status),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);

    // Now we can access the next slice...
    vc->CheckWrite(vb.get(), block_info.block_size, block_info.block_size * (last_block + 1),
                   block_info.block_size);
    vc->CheckRead(vb.get(), block_info.block_size, block_info.block_size * (last_block + 1),
                  block_info.block_size);
    // ... We can still access the previous slice...
    vc->CheckRead(vb.get(), 0, block_info.block_size * last_block, block_info.block_size);
    // ... And we can cross slices
    vc->CheckRead(vb.get(), 0, block_info.block_size * last_block, block_info.block_size * 2);

    vparts[i].slices_used++;
    i = (i + 1) % kNumVParts;
  }

  for (size_t i = 0; i < kNumVParts; i++) {
    printf("Testing multi-slice operations on vslice %lu\n", i);

    // We need at least five slices, so we can access three "middle"
    // slices and jitter to test off-by-one errors.
    ASSERT_GE(vparts[i].slices_used, 5);

    {
      fbl::RefPtr<VmoClient> vc;
      VmoClient::Create(vparts[i].fd.get(), &vc);
      std::unique_ptr<VmoBuf> vb;
      VmoBuf::Create(vc, kSliceSize * 4, &vb);

      // Try accessing 3 noncontiguous slices at once, with the
      // addition of "off by one block".
      size_t dev_off_start = kSliceSize - block_info.block_size;
      size_t dev_off_end = kSliceSize + block_info.block_size;
      size_t len_start = kSliceSize * 3 - block_info.block_size;
      size_t len_end = kSliceSize * 3 + block_info.block_size;

      // Test a variety of:
      // Starting device offsets,
      size_t bsz = block_info.block_size;
      for (size_t dev_off = dev_off_start; dev_off <= dev_off_end; dev_off += bsz) {
        printf("  Testing non-contiguous write/read starting at offset: %zu\n", dev_off);
        // Operation lengths,
        for (size_t len = len_start; len <= len_end; len += bsz) {
          printf("    Testing operation of length: %zu\n", len);
          // and starting VMO offsets
          for (size_t vmo_off = 0; vmo_off < 3 * bsz; vmo_off += bsz) {
            // Try writing & reading the entire section (multiple
            // slices) at once.
            vc->CheckWrite(vb.get(), vmo_off, dev_off, len);
            vc->CheckRead(vb.get(), vmo_off, dev_off, len);

            // Try reading the section one slice at a time.
            // The results should be the same.
            size_t sub_off = 0;
            size_t sub_len = kSliceSize - (dev_off % kSliceSize);
            while (sub_off < len) {
              vc->CheckRead(vb.get(), vmo_off + sub_off, dev_off + sub_off, sub_len);
              sub_off += sub_len;
              sub_len = std::min(kSliceSize, len - sub_off);
            }
          }
        }
      }
    }
    ASSERT_EQ(close(vparts[i].fd.release()), 0);
  }

  ASSERT_EQ(close(fd.release()), 0);
  FVMCheckSliceSize(fvm_device(), kSliceSize);
  ValidateFVM(ramdisk_device());
}

// Test allocating and accessing slices which are
// allocated noncontiguously from the client's perspective.
TEST_F(FvmTest, TestSliceAccessNonContiguousVirtual) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 20;
  constexpr uint64_t kSliceSize = 64 * (1 << 20);
  constexpr uint64_t kDiskSize = kBlockSize * kBlockCount;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd = fvm_device();
  ASSERT_TRUE(fd);

  volume_info_t volume_info;
  ASSERT_EQ(fvm_query(fd.get(), &volume_info), ZX_OK);

  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  request.slice_count = 1;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);

  constexpr size_t kNumVParts = 3;
  typedef struct vdata {
    fbl::unique_fd fd;
    uint8_t guid[BLOCK_GUID_LEN];
    char name[32];
    size_t slices_used;
    size_t last_slice;
  } vdata_t;

  vdata_t vparts[kNumVParts] = {
      {fbl::unique_fd(), GUID_TEST_DATA_VALUE, "data", request.slice_count, request.slice_count},
      {fbl::unique_fd(), GUID_TEST_BLOB_VALUE, "blob", request.slice_count, request.slice_count},
      {fbl::unique_fd(), GUID_TEST_SYS_VALUE, "sys", request.slice_count, request.slice_count},
  };

  for (size_t i = 0; i < std::size(vparts); i++) {
    strcpy(request.name, vparts[i].name);
    memcpy(request.type, vparts[i].guid, BLOCK_GUID_LEN);
    vparts[i].fd.reset(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
    ASSERT_TRUE(vparts[i].fd);
  }

  fdio_cpp::UnownedFdioCaller partition_caller(vparts[0].fd.get());
  zx::unowned_channel partition_channel(partition_caller.borrow_channel());
  fuchsia_hardware_block_BlockInfo block_info;
  zx_status_t status;
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);

  size_t usable_slices_per_vpart = fvm::UsableSlicesCount(kDiskSize, kSliceSize) / kNumVParts;
  size_t i = 0;
  while (vparts[i].slices_used < usable_slices_per_vpart) {
    int vfd = vparts[i].fd.get();
    // This is the last 'accessible' block.
    size_t last_block = (vparts[i].last_slice * (kSliceSize / block_info.block_size)) - 1;
    CheckWriteReadBlock(vfd, last_block, 1);

    // Try writing out of bounds -- check that we don't have access.
    CheckNoAccessBlock(vfd, last_block, 2);
    CheckNoAccessBlock(vfd, last_block + 1, 1);

    // Attempt to access a non-contiguous slice
    fdio_cpp::UnownedFdioCaller partition_caller(vfd);
    zx::unowned_channel partition_channel(partition_caller.borrow_channel());
    uint64_t offset = vparts[i].last_slice + 2;
    uint64_t length = 1;
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), offset, length,
                                                         &status),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);

    // We still don't have access to the next slice...
    CheckNoAccessBlock(vfd, last_block, 2);
    CheckNoAccessBlock(vfd, last_block + 1, 1);

    // But we have access to the slice we asked for!
    size_t requested_block = (offset * kSliceSize) / block_info.block_size;
    CheckWriteReadBlock(vfd, requested_block, 1);

    vparts[i].slices_used++;
    vparts[i].last_slice = offset;
    i = (i + 1) % kNumVParts;
  }

  for (size_t i = 0; i < kNumVParts; i++) {
    ASSERT_EQ(close(vparts[i].fd.release()), 0);
  }

  ASSERT_EQ(close(fd.release()), 0);
  FVMCheckSliceSize(fvm_device(), kSliceSize);
  ValidateFVM(ramdisk_device());
}

// Test that the FVM driver actually persists updates.
TEST_F(FvmTest, TestPersistenceSimple) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 20;
  constexpr uint64_t kSliceSize = 64 * (1 << 20);
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd(fvm_device());
  ASSERT_TRUE(fd);

  constexpr uint64_t kDiskSize = kBlockSize * kBlockCount;
  size_t slices_left = fvm::UsableSlicesCount(kDiskSize, kSliceSize);
  const uint64_t kSliceCount = slices_left;

  volume_info_t volume_info;
  ASSERT_EQ(fvm_query(fd.get(), &volume_info), ZX_OK);

  // Allocate one VPart
  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  request.slice_count = 1;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  memcpy(request.type, kTestPartGUIDData, BLOCK_GUID_LEN);
  fbl::unique_fd vp_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(vp_fd);
  slices_left--;

  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd.get());
  zx::unowned_channel partition_channel(partition_caller.borrow_channel());

  // Check that the name matches what we provided
  char name[fvm::kMaxVPartitionNameLength + 1];
  zx_status_t status;
  size_t actual;
  ASSERT_EQ(fuchsia_hardware_block_partition_PartitionGetName(partition_channel->get(), &status,
                                                              name, sizeof(name), &actual),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  name[actual] = '\0';
  ASSERT_EQ(memcmp(name, kTestPartName1, strlen(kTestPartName1)), 0);
  fuchsia_hardware_block_BlockInfo block_info;
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  std::unique_ptr<uint8_t[]> buf(new uint8_t[block_info.block_size * 2]);

  // Check that we can read from / write to it
  CheckWrite(vp_fd.get(), 0, block_info.block_size, buf.get());
  CheckRead(vp_fd.get(), 0, block_info.block_size, buf.get());
  ASSERT_EQ(close(vp_fd.release()), 0);

  // Check that it still exists after rebinding the driver
  const partition_entry_t entries[] = {
      {kTestPartName1, 1},
  };
  ASSERT_EQ(close(fd.release()), 0);
  FVMRebind(entries, 1);
  fd = fvm_device();
  ASSERT_TRUE(fd, "Failed to rebind FVM driver");

  vp_fd.reset(open_partition_with_devfs(devfs_root().get(), kTestUniqueGUID, kTestPartGUIDData, 0,
                                        nullptr));
  ASSERT_TRUE(vp_fd, "Couldn't re-open Data VPart");
  CheckRead(vp_fd.get(), 0, block_info.block_size, buf.get());

  // Try extending the vpartition, and checking that the extension persists.
  // This is the last 'accessible' block.
  size_t last_block = (kSliceSize / block_info.block_size) - 1;
  CheckWrite(vp_fd.get(), block_info.block_size * last_block, block_info.block_size, &buf[0]);
  CheckRead(vp_fd.get(), block_info.block_size * last_block, block_info.block_size, &buf[0]);

  // Try writing out of bounds -- check that we don't have access.
  CheckNoAccessBlock(vp_fd.get(), (kSliceSize / block_info.block_size) - 1, 2);
  CheckNoAccessBlock(vp_fd.get(), kSliceSize / block_info.block_size, 1);

  partition_caller.reset(vp_fd.get());
  partition_channel = zx::unowned_channel(partition_caller.borrow_channel());
  uint64_t offset = 1;
  uint64_t length = 1;
  ASSERT_EQ(
      fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), offset, length, &status),
      ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  slices_left--;

  ASSERT_EQ(close(vp_fd.release()), 0);
  // FVMRebind will cause the rebind on ramdisk block device. The fvm device is child device
  // to ramdisk block device. Before issuing rebind make sure the fd is released.
  // Rebind the FVM driver, check the extension has succeeded.
  ASSERT_EQ(close(fd.release()), 0);
  FVMRebind(entries, 1);
  fd = fvm_device();

  vp_fd.reset(open_partition_with_devfs(devfs_root().get(), kTestUniqueGUID, kTestPartGUIDData, 0,
                                        nullptr));

  partition_caller.reset(vp_fd.get());
  partition_channel = zx::unowned_channel(partition_caller.borrow_channel());

  // Now we can access the next slice...
  CheckWrite(vp_fd.get(), block_info.block_size * (last_block + 1), block_info.block_size,
             &buf[block_info.block_size]);
  CheckRead(vp_fd.get(), block_info.block_size * (last_block + 1), block_info.block_size,
            &buf[block_info.block_size]);
  // ... We can still access the previous slice...
  CheckRead(vp_fd.get(), block_info.block_size * last_block, block_info.block_size, &buf[0]);
  // ... And we can cross slices
  CheckRead(vp_fd.get(), block_info.block_size * last_block, block_info.block_size * 2, &buf[0]);

  // Try allocating the rest of the slices, rebinding, and ensuring
  // that the size stays updated.
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(block_info.block_count * block_info.block_size, kSliceSize * 2);

  offset = 2;
  length = slices_left;
  ASSERT_EQ(
      fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), offset, length, &status),
      ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(block_info.block_count * block_info.block_size, kSliceSize * kSliceCount);

  ASSERT_EQ(close(vp_fd.release()), 0);
  ASSERT_EQ(close(fd.release()), 0);
  FVMRebind(entries, 1);
  fd = fvm_device();

  vp_fd.reset(open_partition_with_devfs(devfs_root().get(), kTestUniqueGUID, kTestPartGUIDData, 0,
                                        nullptr));
  ASSERT_TRUE(vp_fd, "Couldn't re-open Data VPart");
  partition_caller.reset(vp_fd.get());
  partition_channel = zx::unowned_channel(partition_caller.borrow_channel());

  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(block_info.block_count * block_info.block_size, kSliceSize * kSliceCount);

  ASSERT_EQ(close(vp_fd.release()), 0);
  ASSERT_EQ(close(fd.release()), 0);
  FVMCheckSliceSize(fvm_device(), 64lu * (1 << 20));
}

void CorruptMountHelper(const fbl::unique_fd& devfs_root, const char* partition_path,
                        const mount_options_t* mounting_options, disk_format_t disk_format,
                        const query_request_t& query_request) {
  // Format the VPart as |disk_format|.
  ASSERT_EQ(mkfs(partition_path, disk_format, launch_stdio_sync, &default_mkfs_options), ZX_OK);

  fbl::unique_fd vp_fd(
      open_partition_with_devfs(devfs_root.get(), kTestUniqueGUID, kTestPartGUIDData, 0, nullptr));
  ASSERT_TRUE(vp_fd);
  fuchsia_hardware_block_volume_VsliceRange
      ranges[fuchsia_hardware_block_volume_MAX_SLICE_REQUESTS];
  zx_status_t status;
  size_t actual_ranges_count;

  // Check initial slice allocation.
  //
  // Avoid keping the "FdioCaller" in-scope across mount, as the caller prevents
  // the file descriptor from being transferred.
  {
    fdio_cpp::UnownedFdioCaller partition_caller(vp_fd.get());
    zx::unowned_channel partition_channel(partition_caller.borrow_channel());
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeQuerySlices(
                  partition_channel->get(), query_request.vslice_start, query_request.count,
                  &status, ranges, &actual_ranges_count),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(query_request.count, actual_ranges_count);

    for (unsigned i = 0; i < actual_ranges_count; i++) {
      ASSERT_TRUE(ranges[i].allocated);
      ASSERT_EQ(ranges[i].count, 1);
    }

    // Manually shrink slices so FVM will differ from the partition.
    uint64_t offset = query_request.vslice_start[0];
    uint64_t length = 1;
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeShrink(partition_channel->get(), offset, length,
                                                         &status),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);

    // Check slice allocation after manual grow/shrink
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeQuerySlices(
                  partition_channel->get(), query_request.vslice_start, query_request.count,
                  &status, ranges, &actual_ranges_count),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(query_request.count, actual_ranges_count);
    ASSERT_FALSE(ranges[0].allocated);
    ASSERT_EQ(ranges[0].count, query_request.vslice_start[1] - query_request.vslice_start[0]);
  }

  // Try to mount the VPart. Since this mount call is supposed to fail, we wait for the spawned
  // fs process to finish and associated fidl channels to close before continuing to try and prevent
  // race conditions with the later mount call.
  ASSERT_NE(mount(vp_fd.release(), kMountPath, disk_format, mounting_options, launch_stdio_sync),
            ZX_OK);

  {
    vp_fd.reset(open_partition_with_devfs(devfs_root.get(), kTestUniqueGUID, kTestPartGUIDData, 0,
                                          nullptr));
    ASSERT_TRUE(vp_fd);

    fdio_cpp::UnownedFdioCaller partition_caller(vp_fd.get());
    zx::unowned_channel partition_channel(partition_caller.borrow_channel());

    // Grow back the slice we shrunk earlier.
    uint64_t offset = query_request.vslice_start[0];
    uint64_t length = 1;
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), offset, length,
                                                         &status),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);

    // Verify grow was successful.
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeQuerySlices(
                  partition_channel->get(), query_request.vslice_start, query_request.count,
                  &status, ranges, &actual_ranges_count),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(query_request.count, actual_ranges_count);
    ASSERT_TRUE(ranges[0].allocated);
    ASSERT_EQ(ranges[0].count, 1);

    // Now extend all extents by some number of additional slices.
    for (unsigned i = 0; i < query_request.count; i++) {
      uint64_t offset = query_request.vslice_start[i] + 1;
      uint64_t length = query_request.count - i;
      ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), offset, length,
                                                           &status),
                ZX_OK);
      ASSERT_EQ(status, ZX_OK);
    }

    // Verify that the extensions were successful.
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeQuerySlices(
                  partition_channel->get(), query_request.vslice_start, query_request.count,
                  &status, ranges, &actual_ranges_count),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(query_request.count, actual_ranges_count);
    for (unsigned i = 0; i < query_request.count; i++) {
      ASSERT_TRUE(ranges[i].allocated);
      ASSERT_EQ(ranges[i].count, 1 + query_request.count - i);
    }
  }

  // Try mount again.
  ASSERT_EQ(mount(vp_fd.release(), kMountPath, disk_format, mounting_options, launch_stdio_async),
            ZX_OK);
  ASSERT_EQ(umount(kMountPath), ZX_OK);

  vp_fd.reset(
      open_partition_with_devfs(devfs_root.get(), kTestUniqueGUID, kTestPartGUIDData, 0, nullptr));
  ASSERT_TRUE(vp_fd);
  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd.get());
  zx::unowned_channel partition_channel(partition_caller.borrow_channel());

  // Verify that slices were fixed on mount.
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeQuerySlices(
                partition_channel->get(), query_request.vslice_start, query_request.count, &status,
                ranges, &actual_ranges_count),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(query_request.count, actual_ranges_count);

  for (unsigned i = 0; i < query_request.count; i++) {
    ASSERT_TRUE(ranges[i].allocated);
    ASSERT_EQ(ranges[i].count, 1);
  }
}

TEST_F(FvmTest, TestCorruptMount) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd = fvm_device();
  ASSERT_TRUE(fd);

  volume_info_t volume_info;
  ASSERT_EQ(fvm_query(fd.get(), &volume_info), ZX_OK);
  ASSERT_EQ(kSliceSize, volume_info.slice_size);

  // Allocate one VPart
  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  request.slice_count = 1;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  memcpy(request.type, kTestPartGUIDData, BLOCK_GUID_LEN);
  fbl::unique_fd vp_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(vp_fd);
  ASSERT_EQ(close(vp_fd.release()), 0);

  char partition_path[PATH_MAX];
  snprintf(partition_path, sizeof(partition_path), "%s/%s-p-1/block", fvm_path(), kTestPartName1);

  size_t kMinfsBlocksPerSlice = kSliceSize / minfs::kMinfsBlockSize;
  query_request_t query_request;
  query_request.count = 4;
  query_request.vslice_start[0] = minfs::kFVMBlockInodeBmStart / kMinfsBlocksPerSlice;
  query_request.vslice_start[1] = minfs::kFVMBlockDataBmStart / kMinfsBlocksPerSlice;
  query_request.vslice_start[2] = minfs::kFVMBlockInodeStart / kMinfsBlocksPerSlice;
  query_request.vslice_start[3] = minfs::kFVMBlockDataStart / kMinfsBlocksPerSlice;

  // Run the test for Minfs.
  CorruptMountHelper(devfs_root(), partition_path, &mounting_options_, DISK_FORMAT_MINFS,
                     query_request);

  size_t kBlobfsBlocksPerSlice = kSliceSize / blobfs::kBlobfsBlockSize;
  query_request.count = 3;
  query_request.vslice_start[0] = blobfs::kFVMBlockMapStart / kBlobfsBlocksPerSlice;
  query_request.vslice_start[1] = blobfs::kFVMNodeMapStart / kBlobfsBlocksPerSlice;
  query_request.vslice_start[2] = blobfs::kFVMDataStart / kBlobfsBlocksPerSlice;

  // Run the test for Blobfs.
  CorruptMountHelper(devfs_root(), partition_path, &mounting_options_, DISK_FORMAT_BLOBFS,
                     query_request);

  // Clean up
  ASSERT_EQ(rmdir(kMountPath), 0);
  ASSERT_EQ(close(fd.release()), 0);
}

TEST_F(FvmTest, TestVPartitionUpgrade) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd = fvm_device();
  ASSERT_TRUE(fd);

  fdio_cpp::FdioCaller volume_manager(std::move(fd));

  // Short-hand for asking if we can open a partition.
  auto openable = [this](const uint8_t* instanceGUID, const uint8_t* typeGUID) {
    fbl::unique_fd fd(
        open_partition_with_devfs(devfs_root().get(), instanceGUID, typeGUID, 0, nullptr));
    return fd.is_valid();
  };

  // Allocate two VParts, one active, and one inactive.
  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  request.flags = fuchsia_hardware_block_volume_ALLOCATE_PARTITION_FLAG_INACTIVE;
  request.slice_count = 1;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  memcpy(request.type, kTestPartGUIDData, BLOCK_GUID_LEN);
  fbl::unique_fd vp_fd(
      fvm_allocate_partition_with_devfs(devfs_root().get(), volume_manager.fd().get(), &request));
  ASSERT_TRUE(vp_fd, "Couldn't open Volume");
  ASSERT_EQ(close(vp_fd.release()), 0);

  request.flags = 0;
  memcpy(request.guid, kTestUniqueGUID2, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName2);
  vp_fd.reset(
      fvm_allocate_partition_with_devfs(devfs_root().get(), volume_manager.fd().get(), &request));
  ASSERT_TRUE(vp_fd, "Couldn't open volume");
  ASSERT_EQ(close(vp_fd.release()), 0);

  const partition_entry_t entries[] = {
      {kTestPartName2, 2},
  };

  // Release FVM device that we opened earlier
  ASSERT_EQ(close(volume_manager.release().get()), 0);
  FVMRebind(entries, 1);
  volume_manager.reset(fvm_device());

  // We shouldn't be able to re-open the inactive partition...
  ASSERT_FALSE(openable(kTestUniqueGUID, kTestPartGUIDData));
  // ... but we SHOULD be able to re-open the active partition.
  ASSERT_TRUE(openable(kTestUniqueGUID2, kTestPartGUIDData));

  // Try to upgrade the partition (from GUID2 --> GUID)
  request.flags = fuchsia_hardware_block_volume_ALLOCATE_PARTITION_FLAG_INACTIVE;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  fbl::unique_fd new_fd(
      fvm_allocate_partition_with_devfs(devfs_root().get(), volume_manager.fd().get(), &request));
  ASSERT_TRUE(new_fd, "Couldn't open new volume");
  ASSERT_EQ(close(new_fd.release()), 0);

  Upgrade(volume_manager, kTestUniqueGUID2, kTestUniqueGUID, ZX_OK);

  // After upgrading, we should be able to open both partitions
  ASSERT_TRUE(openable(kTestUniqueGUID, kTestPartGUIDData));
  ASSERT_TRUE(openable(kTestUniqueGUID2, kTestPartGUIDData));

  // Rebind the FVM driver, check the upgrade has succeeded.
  // The original (GUID2) should be deleted, and the new partition (GUID)
  // should exist.
  const partition_entry_t upgraded_entries[] = {
      {kTestPartName1, 1},
  };
  // Release FVM device that we opened earlier
  ASSERT_EQ(close(volume_manager.release().get()), 0);
  FVMRebind(upgraded_entries, 1);
  volume_manager.reset(fvm_device());

  ASSERT_TRUE(openable(kTestUniqueGUID, kTestPartGUIDData));
  ASSERT_FALSE(openable(kTestUniqueGUID2, kTestPartGUIDData));

  // Try upgrading when the "new" version doesn't exist.
  // (It should return an error and have no noticable effect).
  Upgrade(volume_manager, kTestUniqueGUID, kTestUniqueGUID2, ZX_ERR_NOT_FOUND);

  // Release FVM device that we opened earlier
  ASSERT_EQ(close(volume_manager.release().get()), 0);
  FVMRebind(upgraded_entries, 1);
  volume_manager.reset(fvm_device());

  ASSERT_TRUE(openable(kTestUniqueGUID, kTestPartGUIDData));
  ASSERT_FALSE(openable(kTestUniqueGUID2, kTestPartGUIDData));

  // Try upgrading when the "old" version doesn't exist.
  request.flags = fuchsia_hardware_block_volume_ALLOCATE_PARTITION_FLAG_INACTIVE;
  memcpy(request.guid, kTestUniqueGUID2, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName2);
  new_fd.reset(
      fvm_allocate_partition_with_devfs(devfs_root().get(), volume_manager.fd().get(), &request));
  ASSERT_TRUE(new_fd, "Couldn't open volume");
  ASSERT_EQ(close(new_fd.release()), 0);

  uint8_t fake_guid[BLOCK_GUID_LEN];
  memset(fake_guid, 0, BLOCK_GUID_LEN);
  Upgrade(volume_manager, fake_guid, kTestUniqueGUID2, ZX_OK);

  const partition_entry_t upgraded_entries_both[] = {
      {kTestPartName1, 1},
      {kTestPartName2, 2},
  };

  // Release FVM device that we opened earlier
  ASSERT_EQ(close(volume_manager.release().get()), 0);
  FVMRebind(upgraded_entries_both, 2);
  volume_manager.reset(fvm_device());

  // We should be able to open both partitions again.
  ASSERT_TRUE(openable(kTestUniqueGUID, kTestPartGUIDData));
  ASSERT_TRUE(openable(kTestUniqueGUID2, kTestPartGUIDData));

  // Destroy and reallocate the first partition as inactive.
  vp_fd.reset(open_partition_with_devfs(devfs_root().get(), kTestUniqueGUID, kTestPartGUIDData, 0,
                                        nullptr));
  ASSERT_TRUE(vp_fd, "Couldn't open volume");
  fdio_cpp::FdioCaller partition_caller(std::move(vp_fd));
  zx_status_t status;
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeDestroy(partition_caller.borrow_channel(), &status),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  partition_caller.reset();
  request.flags = fuchsia_hardware_block_volume_ALLOCATE_PARTITION_FLAG_INACTIVE;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  new_fd.reset(
      fvm_allocate_partition_with_devfs(devfs_root().get(), volume_manager.fd().get(), &request));
  ASSERT_TRUE(new_fd, "Couldn't open volume");
  ASSERT_EQ(close(new_fd.release()), 0);

  // Upgrade the partition with old_guid == new_guid.
  // This should activate the partition.
  Upgrade(volume_manager, kTestUniqueGUID, kTestUniqueGUID, ZX_OK);

  // Release FVM device that we opened earlier
  ASSERT_EQ(close(volume_manager.release().get()), 0);
  FVMRebind(upgraded_entries_both, 2);
  volume_manager.reset(fvm_device());

  // We should be able to open both partitions again.
  ASSERT_TRUE(openable(kTestUniqueGUID, kTestPartGUIDData));
  ASSERT_TRUE(openable(kTestUniqueGUID2, kTestPartGUIDData));
}

// Test that the FVM driver can mount filesystems.
TEST_F(FvmTest, TestMounting) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd = fvm_device();
  ASSERT_TRUE(fd);

  volume_info_t volume_info;
  ASSERT_EQ(fvm_query(fd.get(), &volume_info), ZX_OK);

  // Allocate one VPart
  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  request.slice_count = 1;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  memcpy(request.type, kTestPartGUIDData, BLOCK_GUID_LEN);
  fbl::unique_fd vp_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(vp_fd);

  // Format the VPart as minfs
  char partition_path[PATH_MAX];
  snprintf(partition_path, sizeof(partition_path), "%s/%s-p-1/block", fvm_path(), kTestPartName1);
  ASSERT_EQ(mkfs(partition_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
            ZX_OK);

  // Mount the VPart
  ASSERT_EQ(
      mount(vp_fd.release(), kMountPath, DISK_FORMAT_MINFS, &mounting_options_, launch_stdio_async),
      ZX_OK);

  // Verify that the mount was successful.
  fbl::unique_fd rootfd(open(kMountPath, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(rootfd);
  zx_status_t status;
  filesystem_info_t filesystem_info;
  fdio_cpp::FdioCaller caller(std::move(rootfd));
  ASSERT_EQ(
      fuchsia_io_DirectoryAdminQueryFilesystem(caller.borrow_channel(), &status, &filesystem_info),
      ZX_OK);
  const char* kFsName = "minfs";
  const char* name = reinterpret_cast<const char*>(filesystem_info.name);
  ASSERT_EQ(strncmp(name, kFsName, strlen(kFsName)), 0, "Unexpected filesystem mounted");

  // Verify that MinFS does not try to use more of the VPartition than
  // was originally allocated.
  ASSERT_LE(filesystem_info.total_bytes, kSliceSize * request.slice_count);

  // Clean up.
  ASSERT_EQ(umount(kMountPath), ZX_OK);
  ASSERT_EQ(rmdir(kMountPath), 0);
  ASSERT_EQ(close(fd.release()), 0);
  FVMCheckSliceSize(fvm_device(), kSliceSize);
}

// Test that FVM-aware filesystem can be reformatted.
TEST_F(FvmTest, TestMkfs) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd = fvm_device();
  ASSERT_TRUE(fd);

  volume_info_t volume_info;
  ASSERT_EQ(fvm_query(fd.get(), &volume_info), ZX_OK);

  // Allocate one VPart.
  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  request.slice_count = 1;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  memcpy(request.type, kTestPartGUIDData, BLOCK_GUID_LEN);
  fbl::unique_fd vp_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(vp_fd);

  // Format the VPart as minfs.
  char partition_path[PATH_MAX];
  snprintf(partition_path, sizeof(partition_path), "%s/%s-p-1/block", fvm_path(), kTestPartName1);
  ASSERT_EQ(mkfs(partition_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
            ZX_OK);

  // Format it as MinFS again, even though it is already formatted.
  ASSERT_EQ(mkfs(partition_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
            ZX_OK);

  // Now try reformatting as blobfs.
  ASSERT_EQ(mkfs(partition_path, DISK_FORMAT_BLOBFS, launch_stdio_sync, &default_mkfs_options),
            ZX_OK);

  // Demonstrate that mounting as minfs will fail, but mounting as blobfs
  // is successful.
  ASSERT_NE(
      mount(vp_fd.release(), kMountPath, DISK_FORMAT_MINFS, &mounting_options_, launch_stdio_sync),
      ZX_OK);
  vp_fd.reset(open(partition_path, O_RDWR));
  ASSERT_TRUE(vp_fd);
  ASSERT_EQ(mount(vp_fd.release(), kMountPath, DISK_FORMAT_BLOBFS, &mounting_options_,
                  launch_stdio_async),
            ZX_OK);
  ASSERT_EQ(umount(kMountPath), ZX_OK);

  // ... and reformat back to MinFS again.
  ASSERT_EQ(mkfs(partition_path, DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options),
            ZX_OK);

  // Mount the VPart.
  vp_fd.reset(open(partition_path, O_RDWR));
  ASSERT_TRUE(vp_fd);
  ASSERT_EQ(
      mount(vp_fd.release(), kMountPath, DISK_FORMAT_MINFS, &mounting_options_, launch_stdio_async),
      ZX_OK);

  // Verify that the mount was successful.
  fbl::unique_fd rootfd(open(kMountPath, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(rootfd);
  zx_status_t status;
  filesystem_info_t filesystem_info;
  fdio_cpp::FdioCaller caller(std::move(rootfd));
  ASSERT_EQ(
      fuchsia_io_DirectoryAdminQueryFilesystem(caller.borrow_channel(), &status, &filesystem_info),
      ZX_OK);
  const char* kFsName = "minfs";
  const char* name = reinterpret_cast<const char*>(filesystem_info.name);
  ASSERT_EQ(strncmp(name, kFsName, strlen(kFsName)), 0, "Unexpected filesystem mounted");

  // Verify that MinFS does not try to use more of the VPartition than
  // was originally allocated.
  ASSERT_LE(filesystem_info.total_bytes, kSliceSize * request.slice_count);

  // Clean up.
  ASSERT_EQ(umount(kMountPath), ZX_OK);
  ASSERT_EQ(rmdir(kMountPath), 0);
  FVMCheckSliceSize(fvm_device(), kSliceSize);
}

// Test that the FVM can recover when one copy of
// metadata becomes corrupt.
TEST_F(FvmTest, TestCorruptionOk) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd = fvm_device();
  ASSERT_TRUE(fd);

  fbl::unique_fd ramdisk_fd = ramdisk_device();
  ASSERT_TRUE(ramdisk_fd);

  volume_info_t volume_info;
  ASSERT_EQ(fvm_query(fd.get(), &volume_info), ZX_OK);

  // Allocate one VPart (writes to backup)
  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  request.slice_count = 1;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  memcpy(request.type, kTestPartGUIDData, BLOCK_GUID_LEN);
  fbl::unique_fd vp_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(vp_fd);

  // Extend the vpart (writes to primary)
  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd.get());
  zx::unowned_channel partition_channel(partition_caller.borrow_channel());
  zx_status_t status;
  uint64_t offset = 1;
  uint64_t length = 1;
  ASSERT_EQ(
      fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), offset, length, &status),
      ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  fuchsia_hardware_block_BlockInfo block_info;
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(block_info.block_count * block_info.block_size, kSliceSize * 2);

  // Initial slice access
  CheckWriteReadBlock(vp_fd.get(), 0, 1);
  // Extended slice access
  CheckWriteReadBlock(vp_fd.get(), kSliceSize / block_info.block_size, 1);

  ASSERT_EQ(close(vp_fd.release()), 0);

  // Corrupt the (backup) metadata and rebind.
  // The 'primary' was the last one written, so it'll be used.
  constexpr uint64_t kDiskSize = kBlockSize * kBlockCount;
  off_t off = fvm::BackupStart(kDiskSize, kSliceSize);
  uint8_t buf[fvm::kBlockSize];
  ASSERT_EQ(lseek(ramdisk_fd.get(), off, SEEK_SET), off);
  ASSERT_EQ(read(ramdisk_fd.get(), buf, sizeof(buf)), sizeof(buf));
  // Modify an arbitrary byte (not the magic bits; we still want it to mount!)
  buf[128]++;
  ASSERT_EQ(lseek(ramdisk_fd.get(), off, SEEK_SET), off);
  ASSERT_EQ(write(ramdisk_fd.get(), buf, sizeof(buf)), sizeof(buf));

  const partition_entry_t entries[] = {
      {kTestPartName1, 1},
  };
  ASSERT_EQ(close(fd.release()), 0);
  FVMRebind(entries, 1);
  fd = fvm_device();

  vp_fd.reset(open_partition_with_devfs(devfs_root().get(), kTestUniqueGUID, kTestPartGUIDData, 0,
                                        nullptr));
  ASSERT_TRUE(vp_fd, "Couldn't re-open Data VPart");

  // The slice extension is still accessible.
  CheckWriteReadBlock(vp_fd.get(), 0, 1);
  CheckWriteReadBlock(vp_fd.get(), kSliceSize / block_info.block_size, 1);

  // Clean up
  ASSERT_EQ(close(vp_fd.release()), 0);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(close(ramdisk_fd.release()), 0);

  FVMCheckSliceSize(fvm_device(), kSliceSize);
}

TEST_F(FvmTest, TestCorruptionRegression) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd(fvm_device());
  ASSERT_TRUE(fd);

  fbl::unique_fd ramdisk_fd = ramdisk_device();
  ASSERT_TRUE(ramdisk_fd);

  volume_info_t volume_info;
  ASSERT_EQ(fvm_query(fd.get(), &volume_info), ZX_OK);
  size_t slice_size = volume_info.slice_size;

  // Allocate one VPart (writes to backup)
  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  request.slice_count = 1;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  memcpy(request.type, kTestPartGUIDData, BLOCK_GUID_LEN);
  fbl::unique_fd vp_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(vp_fd);

  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd.get());
  zx::unowned_channel partition_channel(partition_caller.borrow_channel());
  zx_status_t status;

  // Extend the vpart (writes to primary)
  uint64_t offset = 1;
  uint64_t length = 1;
  ASSERT_EQ(
      fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), offset, length, &status),
      ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  fuchsia_hardware_block_BlockInfo block_info;
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(block_info.block_count * block_info.block_size, slice_size * 2);

  // Initial slice access
  CheckWriteReadBlock(vp_fd.get(), 0, 1);
  // Extended slice access
  CheckWriteReadBlock(vp_fd.get(), slice_size / block_info.block_size, 1);

  ASSERT_EQ(close(vp_fd.release()), 0);

  // Corrupt the (primary) metadata and rebind.
  // The 'primary' was the last one written, so the backup will be used.
  off_t off = 0;
  uint8_t buf[fvm::kBlockSize];
  ASSERT_EQ(lseek(ramdisk_fd.get(), off, SEEK_SET), off);
  ASSERT_EQ(read(ramdisk_fd.get(), buf, sizeof(buf)), sizeof(buf));
  buf[128]++;
  ASSERT_EQ(lseek(ramdisk_fd.get(), off, SEEK_SET), off);
  ASSERT_EQ(write(ramdisk_fd.get(), buf, sizeof(buf)), sizeof(buf));

  const partition_entry_t entries[] = {
      {kTestPartName1, 1},
  };
  ASSERT_EQ(close(fd.release()), 0);
  FVMRebind(entries, 1);
  fd = fvm_device();

  vp_fd.reset(open_partition_with_devfs(devfs_root().get(), kTestUniqueGUID, kTestPartGUIDData, 0,
                                        nullptr));
  ASSERT_TRUE(vp_fd);

  // The slice extension is no longer accessible
  CheckWriteReadBlock(vp_fd.get(), 0, 1);
  CheckNoAccessBlock(vp_fd.get(), slice_size / block_info.block_size, 1);

  // Clean up
  ASSERT_EQ(close(vp_fd.release()), 0);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(close(ramdisk_fd.release()), 0);
  FVMCheckSliceSize(fvm_device(), kSliceSize);
}

TEST_F(FvmTest, TestCorruptionUnrecoverable) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd = fvm_device();
  ASSERT_TRUE(fd);

  // Allocate one VPart (writes to backup)
  alloc_req_t request;
  memset(&request, 0, sizeof(request));
  request.slice_count = 1;
  memcpy(request.guid, kTestUniqueGUID, BLOCK_GUID_LEN);
  strcpy(request.name, kTestPartName1);
  memcpy(request.type, kTestPartGUIDData, BLOCK_GUID_LEN);
  fbl::unique_fd vp_fd(fvm_allocate_partition_with_devfs(devfs_root().get(), fd.get(), &request));
  ASSERT_TRUE(vp_fd);

  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd.get());
  zx::unowned_channel partition_channel(partition_caller.borrow_channel());
  zx_status_t status;

  // Extend the vpart (writes to primary)
  uint64_t offset = 1;
  uint64_t length = 1;
  ASSERT_EQ(
      fuchsia_hardware_block_volume_VolumeExtend(partition_channel->get(), offset, length, &status),
      ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  fuchsia_hardware_block_BlockInfo block_info;
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(partition_channel->get(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(block_info.block_count * block_info.block_size, kSliceSize * 2);

  // Initial slice access
  CheckWriteReadBlock(vp_fd.get(), 0, 1);
  // Extended slice access
  CheckWriteReadBlock(vp_fd.get(), kSliceSize / block_info.block_size, 1);

  ASSERT_EQ(close(vp_fd.release()), 0);

  // Corrupt both copies of the metadata.
  // The 'primary' was the last one written, so the backup will be used.
  off_t off = 0;
  uint8_t buf[fvm::kBlockSize];
  fbl::unique_fd ramdisk_fd = ramdisk_device();
  ASSERT_TRUE(ramdisk_fd);
  ASSERT_EQ(lseek(ramdisk_fd.get(), off, SEEK_SET), off);
  ASSERT_EQ(read(ramdisk_fd.get(), buf, sizeof(buf)), sizeof(buf));
  buf[128]++;
  ASSERT_EQ(lseek(ramdisk_fd.get(), off, SEEK_SET), off);
  ASSERT_EQ(write(ramdisk_fd.get(), buf, sizeof(buf)), sizeof(buf));
  constexpr uint64_t kDiskSize = kBlockSize * kBlockCount;
  off = fvm::BackupStart(kDiskSize, kSliceSize);
  ASSERT_EQ(lseek(ramdisk_fd.get(), off, SEEK_SET), off);
  ASSERT_EQ(read(ramdisk_fd.get(), buf, sizeof(buf)), sizeof(buf));
  buf[128]++;
  ASSERT_EQ(lseek(ramdisk_fd.get(), off, SEEK_SET), off);
  ASSERT_EQ(write(ramdisk_fd.get(), buf, sizeof(buf)), sizeof(buf));

  ValidateFVM(ramdisk_device(), ValidationResult::Corrupted);

  // Clean up
  ASSERT_EQ(close(ramdisk_fd.release()), 0);
}

// Tests the FVM checker using invalid arguments.
TEST_F(FvmTest, TestCheckBadArguments) {
  fvm::Checker checker;
  ASSERT_FALSE(checker.Validate(), "Checker should be missing device, block size");

  checker.SetBlockSize(512);
  ASSERT_FALSE(checker.Validate(), "Checker should be missing device");

  checker.SetBlockSize(0);
  CreateFVM(512, 1 << 20, 64LU * (1 << 20));
  fbl::unique_fd fd = ramdisk_device();
  ASSERT_TRUE(fd);

  checker.SetDevice(std::move(fd));
  ASSERT_FALSE(checker.Validate(), "Checker should be missing block size");
}

// Tests the FVM checker against a just-initialized FVM.
TEST_F(FvmTest, TestCheckNewFVM) {
  CreateFVM(512, 1 << 20, 64LU * (1 << 20));
  fbl::unique_fd fd = ramdisk_device();
  ASSERT_TRUE(fd);

  fvm::Checker checker(std::move(fd), 512, true);
  ASSERT_TRUE(checker.Validate());
}

TEST_F(FvmTest, TestAbortDriverLoadSmallDevice) {
  constexpr uint64_t kMB = 1 << 20;
  constexpr uint64_t kGB = 1 << 30;
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 50 * kMB / kBlockSize;
  constexpr uint64_t kSliceSize = kMB;
  constexpr uint64_t kFvmPartitionSize = 4 * kGB;

  CreateRamdisk(kBlockSize, kBlockCount);
  fbl::unique_fd ramdisk_fd(ramdisk_device());

  // Init fvm with a partition bigger than the underlying disk.
  fvm_init_with_size(ramdisk_fd.get(), kFvmPartitionSize, kSliceSize);

  zx::channel fvm_channel;
  // Try to bind an fvm to the disk.
  ASSERT_OK(fdio_get_service_handle(ramdisk_fd.get(), fvm_channel.reset_and_get_address()));

  // Bind should return ZX_ERR_IO when the load of a driver fails.
  auto resp = ::llcpp::fuchsia::device::Controller::Call::Bind(
      zx::unowned_channel(fvm_channel.get()),
      ::fidl::StringView(FVM_DRIVER_LIB, STRLEN(FVM_DRIVER_LIB)));
  ASSERT_OK(resp.status());
  ASSERT_FALSE(resp->result.is_response());
  ASSERT_EQ(resp->result.err(), ZX_ERR_INTERNAL);

  // Grow the ramdisk to the appropiate size and bind should succeed.
  ASSERT_OK(ramdisk_grow(ramdisk(), kFvmPartitionSize));
  // Use Controller::Call::Rebind because the driver might still be
  // when init fails. Driver removes the device and will eventually be
  // unloaded but Controller::Bind above does not wait until
  // the device is removed. Controller::Rebind ensures nothing is
  // bound to the device, before it tries to bind the driver again.
  auto resp2 = ::llcpp::fuchsia::device::Controller::Call::Rebind(
      zx::unowned_channel(fvm_channel.get()),
      ::fidl::StringView(FVM_DRIVER_LIB, STRLEN(FVM_DRIVER_LIB)));
  ASSERT_OK(resp2.status());
  ASSERT_TRUE(resp2->result.is_response());
  char fvm_path[PATH_MAX];
  snprintf(fvm_path, sizeof(fvm_path), "%s/fvm", ramdisk_path());
  ASSERT_OK(wait_for_device(fvm_path, zx::duration::infinite().get()));
}

}  // namespace
