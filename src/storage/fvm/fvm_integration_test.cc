// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.partition/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/unsafe.h>
#include <lib/fit/function.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/fifo.h>
#include <lib/zx/vmo.h>
#include <poll.h>
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
#include <zircon/syscalls.h>

#include <algorithm>
#include <climits>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

#include "src/lib/storage/block_client/cpp/client.h"
#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/lib/storage/fs_management/cpp/admin.h"
#include "src/lib/storage/fs_management/cpp/fvm.h"
#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/fvm/format.h"
#include "src/storage/fvm/fvm_check.h"
#include "src/storage/minfs/format.h"
#include "zircon/errors.h"

#define FVM_DRIVER_LIB "fvm.so"
#define STRLEN(s) (sizeof(s) / sizeof((s)[0]))

namespace {

using VolumeManagerInfo = fuchsia_hardware_block_volume::wire::VolumeManagerInfo;
using BlockGuid = std::array<uint8_t, BLOCK_GUID_LEN>;
using BlockName = std::array<char, BLOCK_NAME_LEN>;

constexpr char kMountPath[] = "/test/minfs_test_mountpath";
constexpr char kTestDevPath[] = "/fake/dev";
constexpr char kTestBlobfsChildName[] = "test-blobfs";
constexpr char kTestCollectionName[] = "fs-collection";

// Returns the number of usable slices for a standard layout on a given-sized device.
size_t UsableSlicesCount(size_t disk_size, size_t slice_size) {
  return fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions, disk_size, slice_size)
      .GetAllocationTableUsedEntryCount();
}

using driver_integration_test::IsolatedDevmgr;

class FvmTest : public zxtest::Test {
 public:
  void SetUp() override {
    IsolatedDevmgr::Args args;
    args.disable_block_watcher = true;

    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));
    ASSERT_OK(wait_for_device_at(devfs_root().get(), "sys/platform/00:00:2d/ramctl",
                                 zx::duration::infinite().get()));

    ASSERT_OK(loop_.StartThread());

    fdio_ns_t* name_space;
    ASSERT_OK(fdio_ns_get_installed(&name_space));

    ASSERT_OK(fdio_ns_bind_fd(name_space, kTestDevPath, devmgr_.devfs_root().get()));
  }

  const fbl::unique_fd& devfs_root() const { return devmgr_.devfs_root(); }

  void TearDown() override {
    fdio_ns_t* name_space;
    ASSERT_OK(fdio_ns_get_installed(&name_space));
    ASSERT_OK(fdio_ns_unbind(name_space, kTestDevPath));
    ASSERT_OK(ramdisk_destroy(ramdisk_));
  }

  fbl::unique_fd fvm_device() const { return fbl::unique_fd(open(fvm_driver_path_, O_RDWR)); }

  const char* fvm_path() const { return fvm_driver_path_; }

  fbl::unique_fd ramdisk_device() const { return fbl::unique_fd(open(ramdisk_path_, O_RDWR)); }

  fidl::UnownedClientEnd<fuchsia_device::Controller> ramdisk_controller_interface() const {
    return fidl::UnownedClientEnd<fuchsia_device::Controller>(
        ramdisk_get_block_interface(ramdisk_));
  }

  fidl::UnownedClientEnd<fuchsia_hardware_block::Block> ramdisk_block_interface() const {
    return fidl::UnownedClientEnd<fuchsia_hardware_block::Block>(
        ramdisk_get_block_interface(ramdisk_));
  }

  const ramdisk_client* ramdisk() const { return ramdisk_; }

  const char* ramdisk_path() const { return ramdisk_path_; }

  void FVMRebind();

  void CreateFVM(uint64_t block_size, uint64_t block_count, uint64_t slice_size);

  void CreateRamdisk(uint64_t block_size, uint64_t block_count);

  zx::result<fbl::unique_fd> OpenPartition(const fs_management::PartitionMatcher& matcher) const {
    return WaitForPartition(matcher, zx::duration(0));
  }

  zx::result<fbl::unique_fd> WaitForPartition(
      const fs_management::PartitionMatcher& matcher,
      zx::duration timeout = zx::duration::infinite()) const {
    return fs_management::OpenPartitionWithDevfs(devfs_root().get(), matcher, timeout.get(),
                                                 nullptr);
  }

  struct AllocatePartitionRequest {
    size_t slice_count = 1;
    const BlockGuid& type;
    const BlockGuid& guid;
    const BlockName& name;
    uint32_t flags = 0;
  };

  zx::result<fbl::unique_fd> AllocatePartition(AllocatePartitionRequest request) const {
    alloc_req_t req;
    req.slice_count = request.slice_count;
    req.flags = request.flags;
    static_assert(sizeof(req.type) == std::tuple_size<BlockGuid>::value);
    static_assert(sizeof(req.guid) == std::tuple_size<BlockGuid>::value);
    static_assert(sizeof(req.name) == std::tuple_size<BlockName>::value);
    memcpy(req.type, request.type.data(), sizeof(req.type));
    memcpy(req.guid, request.guid.data(), sizeof(req.guid));
    memcpy(req.name, request.name.data(), sizeof(req.name));

    return fs_management::FvmAllocatePartitionWithDevfs(devfs_root().get(), fvm_device().get(),
                                                        &req);
  }

 protected:
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  IsolatedDevmgr devmgr_;
  ramdisk_client_t* ramdisk_ = nullptr;
  fs_management::MountOptions mounting_options_;
  char ramdisk_path_[PATH_MAX] = {};
  char fvm_driver_path_[PATH_MAX] = {};
};

void FvmTest::CreateRamdisk(uint64_t block_size, uint64_t block_count) {
  ASSERT_OK(ramdisk_create_at(devfs_root().get(), block_size, block_count, &ramdisk_));
  snprintf(ramdisk_path_, PATH_MAX, "%s/%s", kTestDevPath, ramdisk_get_path(ramdisk_));
}

void FvmTest::CreateFVM(uint64_t block_size, uint64_t block_count, uint64_t slice_size) {
  CreateRamdisk(block_size, block_count);

  ASSERT_OK(fs_management::FvmInitPreallocated(ramdisk_block_interface(), block_count * block_size,
                                               block_count * block_size, slice_size));

  auto resp =
      fidl::WireCall(ramdisk_controller_interface())->Bind(fidl::StringView(FVM_DRIVER_LIB));
  ASSERT_OK(resp.status());
  ASSERT_TRUE(resp->is_ok());

  snprintf(fvm_driver_path_, PATH_MAX, "%s/fvm", ramdisk_path_);
  ASSERT_OK(wait_for_device(fvm_driver_path_, zx::duration::infinite().get()));
}

void FvmTest::FVMRebind() {
  auto resp =
      fidl::WireCall(ramdisk_controller_interface())->Rebind(fidl::StringView(FVM_DRIVER_LIB));
  ASSERT_OK(resp.status());
  ASSERT_TRUE(resp->is_ok());

  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/fvm", ramdisk_path_);
  ASSERT_OK(wait_for_device(path, zx::duration::infinite().get()));
}

void FVMCheckSliceSize(const fbl::unique_fd& fd, size_t expected_slice_size) {
  ASSERT_TRUE(fd);
  auto volume_info_or = fs_management::FvmQuery(fd.get());
  ASSERT_EQ(volume_info_or.status_value(), ZX_OK, "Failed to query fvm\n");
  ASSERT_EQ(expected_slice_size, volume_info_or->slice_size, "Unexpected slice size\n");
}

void FVMCheckAllocatedCount(const fbl::unique_fd& fd, size_t expected_allocated,
                            size_t expected_total) {
  auto volume_info_or = fs_management::FvmQuery(fd.get());
  ASSERT_EQ(volume_info_or.status_value(), ZX_OK);
  ASSERT_EQ(volume_info_or->slice_count, expected_total);
  ASSERT_EQ(volume_info_or->assigned_slice_count, expected_allocated);
}

enum class ValidationResult {
  Valid,
  Corrupted,
};

void ValidateFVM(fbl::unique_fd fd, ValidationResult result = ValidationResult::Valid) {
  ASSERT_TRUE(fd);
  fdio_cpp::UnownedFdioCaller disk_caller(fd.get());
  const fidl::WireResult wire_result =
      fidl::WireCall(disk_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
  ASSERT_OK(wire_result.status());
  const fidl::WireResponse wire_response = wire_result.value();
  ASSERT_OK(wire_response.status);
  const fuchsia_hardware_block::wire::BlockInfo& block_info = *wire_response.info;
  fvm::Checker checker(std::move(fd), block_info.block_size, true);
  switch (result) {
    case ValidationResult::Valid:
      ASSERT_TRUE(checker.Validate());
      break;
    default:
      ASSERT_FALSE(checker.Validate());
  }
}

zx::result<std::string> GetPartitionPath(int fd) {
  fdio_cpp::UnownedFdioCaller caller(fd);
  auto controller = caller.borrow_as<fuchsia_device::Controller>();
  auto path = fidl::WireCall(controller)->GetTopologicalPath();
  if (!path.ok()) {
    return zx::error(path.status());
  }
  if (path->is_error()) {
    return zx::error(path->error_value());
  }
  // The partition doesn't know that the devmgr it's in is bound at "/fake".
  std::string topological_path = std::string("/fake") + path->value()->path.data();
  return zx::ok(std::move(topological_path));
}

/////////////////////// Helper functions, definitions

constexpr BlockGuid kTestUniqueGuid1 = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
constexpr BlockGuid kTestUniqueGuid2 = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

// Intentionally avoid aligning these GUIDs with
// the actual system GUIDs; otherwise, limited versions
// of Fuchsia may attempt to actually mount these
// partitions automatically.

constexpr BlockName kTestPartDataName = {'d', 'a', 't', 'a'};
constexpr BlockGuid kTestPartDataGuid = {
    0xAA, 0xFF, 0xBB, 0x00, 0x33, 0x44, 0x88, 0x99, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
};

constexpr BlockName kTestPartBlobName = {'b', 'l', 'o', 'b'};
constexpr BlockGuid kTestPartBlobGuid = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0xAA, 0xFF, 0xBB, 0x00, 0x33, 0x44, 0x88, 0x99,
};

constexpr BlockName kTestPartSystemName = {'s', 'y', 's', 't', 'e', 'm'};
constexpr BlockGuid kTestPartSystemGuid = {
    0xEE, 0xFF, 0xBB, 0x00, 0x33, 0x44, 0x88, 0x99, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
};

constexpr fs_management::PartitionMatcher kPartition1Matcher = {
    .type_guid = kTestPartDataGuid.data(),
    .instance_guid = kTestUniqueGuid1.data(),
};
constexpr fs_management::PartitionMatcher kPartition2Matcher = {
    .type_guid = kTestPartDataGuid.data(),
    .instance_guid = kTestUniqueGuid2.data(),
};

class VmoBuf;

class VmoClient : public fbl::RefCounted<VmoClient> {
 public:
  explicit VmoClient(int fd);
  ~VmoClient();

  void CheckWrite(VmoBuf& vbuf, size_t buf_off, size_t dev_off, size_t len);
  void CheckRead(VmoBuf& vbuf, size_t buf_off, size_t dev_off, size_t len);
  void Transaction(block_fifo_request_t* requests, size_t count) {
    ASSERT_OK(client_->Transaction(requests, count));
  }

  int fd() const { return fd_; }
  groupid_t group() { return 0; }

 private:
  int fd_;
  uint32_t block_size_;
  std::unique_ptr<block_client::Client> client_;
};

class VmoBuf {
 public:
  VmoBuf(fbl::RefPtr<VmoClient> client, size_t size) : client_(std::move(client)) {
    buf_ = std::make_unique<uint8_t[]>(size);

    ASSERT_EQ(zx::vmo::create(size, 0, &vmo_), ZX_OK);
    zx::vmo xfer_vmo;
    ASSERT_EQ(vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &xfer_vmo), ZX_OK);

    fdio_cpp::UnownedFdioCaller disk_connection(client_->fd());
    auto res = fidl::WireCall(disk_connection.borrow_as<fuchsia_hardware_block::Block>())
                   ->AttachVmo(std::move(xfer_vmo));
    ASSERT_EQ(res.status(), ZX_OK);
    ASSERT_EQ(res.value().status, ZX_OK);
    vmoid_ = res.value().vmoid->id;
  }

  ~VmoBuf() {
    if (vmo_.is_valid()) {
      block_fifo_request_t request;
      request.group = client_->group();
      request.vmoid = vmoid_;
      request.opcode = BLOCKIO_CLOSE_VMO;
      client_->Transaction(&request, 1);
    }
  }

 private:
  friend VmoClient;

  fbl::RefPtr<VmoClient> client_;
  zx::vmo vmo_;
  std::unique_ptr<uint8_t[]> buf_;
  vmoid_t vmoid_;
};

VmoClient::VmoClient(int fd) : fd_(fd) {
  fdio_cpp::UnownedFdioCaller disk_connection(fd);

  auto fifo_or =
      fidl::WireCall(disk_connection.borrow_as<fuchsia_hardware_block::Block>())->GetFifo();
  ASSERT_EQ(fifo_or.status(), ZX_OK);
  ASSERT_EQ(fifo_or.value().status, ZX_OK);

  auto info_res =
      fidl::WireCall(disk_connection.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
  ASSERT_EQ(info_res.status(), ZX_OK);
  ASSERT_EQ(info_res.value().status, ZX_OK);
  block_size_ = info_res.value().info->block_size;

  client_ = std::make_unique<block_client::Client>(std::move(fifo_or.value().fifo));
}

VmoClient::~VmoClient() {
  fdio_cpp::UnownedFdioCaller disk_connection(fd());
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)fidl::WireCall(disk_connection.borrow_as<fuchsia_hardware_block::Block>())->CloseFifo();
}

void VmoClient::CheckWrite(VmoBuf& vbuf, size_t buf_off, size_t dev_off, size_t len) {
  // Write to the client-side buffer
  for (size_t i = 0; i < len; i++)
    vbuf.buf_[i + buf_off] = static_cast<uint8_t>(rand());

  // Write to the registered VMO
  ASSERT_EQ(vbuf.vmo_.write(&vbuf.buf_[buf_off], buf_off, len), ZX_OK);

  // Write to the block device
  block_fifo_request_t request;
  request.group = group();
  request.vmoid = vbuf.vmoid_;
  request.opcode = BLOCKIO_WRITE;
  ASSERT_EQ(len % block_size_, 0);
  ASSERT_EQ(buf_off % block_size_, 0);
  ASSERT_EQ(dev_off % block_size_, 0);
  request.length = static_cast<uint32_t>(len / block_size_);
  request.vmo_offset = buf_off / block_size_;
  request.dev_offset = dev_off / block_size_;
  Transaction(&request, 1);
}

void VmoClient::CheckRead(VmoBuf& vbuf, size_t buf_off, size_t dev_off, size_t len) {
  // Create a comparison buffer
  fbl::AllocChecker ac;
  std::unique_ptr<uint8_t[]> out(new (&ac) uint8_t[len]);
  ASSERT_TRUE(ac.check());
  memset(out.get(), 0, len);

  // Read from the block device
  block_fifo_request_t request;
  request.group = group();
  request.vmoid = vbuf.vmoid_;
  request.opcode = BLOCKIO_READ;
  ASSERT_EQ(len % block_size_, 0);
  ASSERT_EQ(buf_off % block_size_, 0);
  ASSERT_EQ(dev_off % block_size_, 0);
  request.length = static_cast<uint32_t>(len / block_size_);
  request.vmo_offset = buf_off / block_size_;
  request.dev_offset = dev_off / block_size_;
  Transaction(&request, 1);

  // Read from the registered VMO
  ASSERT_EQ(vbuf.vmo_.read(out.get(), buf_off, len), ZX_OK);

  ASSERT_EQ(memcmp(&vbuf.buf_[buf_off], out.get(), len), 0);
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
  const fidl::WireResult result =
      fidl::WireCall(disk_connection.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);
  const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;
  size_t len = block_info.block_size * count;
  size_t off = block_info.block_size * block;
  std::unique_ptr<uint8_t[]> in(new uint8_t[len]);
  CheckWrite(fd, off, len, in.get());
  CheckRead(fd, off, len, in.get());
}

void CheckWriteReadBytesFifo(fidl::UnownedClientEnd<fuchsia_hardware_block::Block> device,
                             size_t off, size_t len) {
  std::unique_ptr<uint8_t[]> write_buf(new uint8_t[len]);
  memset(write_buf.get(), 0xa3, len);

  ASSERT_EQ(block_client::SingleWriteBytes(device, write_buf.get(), len, off), ZX_OK);
  std::unique_ptr<uint8_t[]> read_buf(new uint8_t[len]);
  memset(read_buf.get(), 0, len);
  ASSERT_EQ(block_client::SingleReadBytes(device, read_buf.get(), len, off), ZX_OK);
  EXPECT_EQ(memcmp(write_buf.get(), read_buf.get(), len), 0);
}

void CheckNoAccessBlock(int fd, size_t block, size_t count) {
  fdio_cpp::UnownedFdioCaller disk_connection(fd);
  const fidl::WireResult result =
      fidl::WireCall(disk_connection.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);
  const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;
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

void Upgrade(const fdio_cpp::FdioCaller& caller, const BlockGuid& old_guid,
             const BlockGuid& new_guid, zx_status_t result) {
  fuchsia_hardware_block_partition::wire::Guid old_guid_fidl;
  memcpy(&old_guid_fidl.value, old_guid.data(), old_guid.size());
  fuchsia_hardware_block_partition::wire::Guid new_guid_fidl;
  memcpy(&new_guid_fidl.value, new_guid.data(), new_guid.size());

  auto response = fidl::WireCall(caller.borrow_as<fuchsia_hardware_block_volume::VolumeManager>())
                      ->Activate(old_guid_fidl, new_guid_fidl);
  ASSERT_EQ(ZX_OK, response.status());
  ASSERT_EQ(result, response.value().status);
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
  ASSERT_EQ(fs_management::FvmInit(ramdisk_block_interface(), slice_size), ZX_ERR_NO_SPACE);
  ValidateFVM(ramdisk_device(), ValidationResult::Corrupted);
}

// Test initializing the FVM on a large partition, with metadata size > the max transfer size
TEST_F(FvmTest, TestLarge) {
  char fvm_path[PATH_MAX];
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount{UINT64_C(8) * (1 << 20)};
  CreateRamdisk(kBlockSize, kBlockCount);
  fbl::unique_fd fd(ramdisk_device());
  ASSERT_TRUE(fd);

  constexpr size_t kSliceSize{static_cast<size_t>(16) * (1 << 10)};
  fvm::Header fvm_header =
      fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions, kBlockSize * kBlockCount, kSliceSize);

  fdio_cpp::UnownedFdioCaller disk_connection(fd.get());
  const fidl::WireResult result =
      fidl::WireCall(disk_connection.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);
  const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;
  ASSERT_LT(block_info.max_transfer_size, fvm_header.GetMetadataAllocatedBytes());

  ASSERT_EQ(fs_management::FvmInit(ramdisk_block_interface(), kSliceSize), ZX_OK);

  auto resp = fidl::WireCall(disk_connection.borrow_as<fuchsia_device::Controller>())
                  ->Bind(fidl::StringView(FVM_DRIVER_LIB));
  ASSERT_OK(resp.status());
  ASSERT_TRUE(resp->is_ok());

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

  // Allocate one VPart
  auto vp_fd_or = AllocatePartition({
      .type = kTestPartDataGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartDataName,
  });
  ASSERT_EQ(vp_fd_or.status_value(), ZX_OK);
  fbl::unique_fd vp_fd = *std::move(vp_fd_or);

  // Check that the name matches what we provided
  fdio_cpp::UnownedFdioCaller partition_connection(vp_fd);

  const fidl::WireResult result =
      fidl::WireCall(partition_connection.borrow_as<fuchsia_hardware_block_partition::Partition>())
          ->GetName();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);
  ASSERT_STREQ(response.name.get(), kTestPartDataName.data());

  // Check that we can read from / write to it.
  CheckWriteReadBlock(vp_fd.get(), 0, 1);

  // Try accessing the block again after closing / re-opening it.
  ASSERT_EQ(close(vp_fd.release()), 0);
  vp_fd_or = WaitForPartition(kPartition1Matcher);
  ASSERT_EQ(vp_fd_or.status_value(), ZX_OK, "Couldn't re-open Data VPart");
  vp_fd = *std::move(vp_fd_or);
  CheckWriteReadBlock(vp_fd.get(), 0, 1);

  ASSERT_EQ(close(vp_fd.release()), 0);
  FVMCheckSliceSize(fvm_device(), kSliceSize);
  ValidateFVM(ramdisk_device());
}

// Test Reading and writing with RemoteBlockDevice helpers
TEST_F(FvmTest, TestReadWriteSingle) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);

  // Allocate one VPart
  zx::result vp_fd = AllocatePartition({
      .type = kTestPartDataGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartDataName,
  });
  ASSERT_OK(vp_fd);
  fdio_cpp::FdioCaller caller(std::move(vp_fd.value()));

  // Check that we can read from / write to it.
  CheckWriteReadBytesFifo(caller.borrow_as<fuchsia_hardware_block::Block>(), 0, kBlockSize);
  // Check with an offset
  CheckWriteReadBytesFifo(caller.borrow_as<fuchsia_hardware_block::Block>(), kBlockSize * 7,
                          kBlockSize * 4);

  ASSERT_EQ(close(caller.release().release()), 0);
  FVMCheckSliceSize(fvm_device(), kSliceSize);
  ValidateFVM(ramdisk_device());
}

// Test allocating a collection of partitions
TEST_F(FvmTest, TestAllocateMany) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);

  // Test allocation of multiple VPartitions
  auto data_fd_or = AllocatePartition({
      .type = kTestPartDataGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartDataName,
  });
  ASSERT_EQ(data_fd_or.status_value(), ZX_OK);
  fbl::unique_fd data_fd = *std::move(data_fd_or);

  auto blob_fd_or = AllocatePartition({
      .type = kTestPartBlobGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartBlobName,
  });
  ASSERT_EQ(blob_fd_or.status_value(), ZX_OK);
  fbl::unique_fd blob_fd = *std::move(blob_fd_or);

  auto sys_fd_or = AllocatePartition({
      .type = kTestPartSystemGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartSystemName,
  });
  ASSERT_EQ(sys_fd_or.status_value(), ZX_OK);
  fbl::unique_fd sys_fd = *std::move(sys_fd_or);

  CheckWriteReadBlock(data_fd.get(), 0, 1);
  CheckWriteReadBlock(blob_fd.get(), 0, 1);
  CheckWriteReadBlock(sys_fd.get(), 0, 1);

  ASSERT_EQ(close(data_fd.release()), 0);
  ASSERT_EQ(close(blob_fd.release()), 0);
  ASSERT_EQ(close(sys_fd.release()), 0);
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

  auto volume_info_or = fs_management::FvmQuery(fd.get());
  ASSERT_EQ(volume_info_or.status_value(), ZX_OK);
  size_t slice_size = volume_info_or->slice_size;
  constexpr uint64_t kDiskSize = kBlockSize * kBlockCount;
  size_t slices_total = UsableSlicesCount(kDiskSize, slice_size);
  size_t slices_left = slices_total;

  FVMCheckAllocatedCount(fd, slices_total - slices_left, slices_total);

  // Allocate one VPart
  size_t slice_count = 1;
  auto vp_fd_or = AllocatePartition({
      .slice_count = slice_count,
      .type = kTestPartDataGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartDataName,
  });
  ASSERT_EQ(vp_fd_or.status_value(), ZX_OK, "Couldn't open Volume");
  fbl::unique_fd vp_fd = *std::move(vp_fd_or);
  slices_left--;
  FVMCheckAllocatedCount(fd, slices_total - slices_left, slices_total);

  // Confirm that the disk reports the correct number of slices
  fdio_cpp::FdioCaller partition_caller(std::move(vp_fd));

  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;
    ASSERT_EQ(block_info.block_count * block_info.block_size, slice_size * slice_count);
  }

  // Try re-allocating an already allocated vslice
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(0, 1);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_STATUS(response.status, ZX_ERR_OUT_OF_RANGE);
  }

  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;
    ASSERT_EQ(block_info.block_count * block_info.block_size, slice_size * slice_count);
  }

  // Try again with a portion of the request which is unallocated
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(0, 2);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_STATUS(response.status, ZX_ERR_OUT_OF_RANGE);
  }

  // Allocate OBSCENELY too many slices
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(slice_count, std::numeric_limits<uint64_t>::max());
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_STATUS(response.status, ZX_ERR_OUT_OF_RANGE);
  }

  // Allocate slices at a too-large offset
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(std::numeric_limits<uint64_t>::max(), 1);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_STATUS(response.status, ZX_ERR_OUT_OF_RANGE);
  }

  // Attempt to allocate slightly too many slices
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(slice_count, slices_left + 1);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_STATUS(response.status, ZX_ERR_NO_SPACE);
  }

  // The number of free slices should be unchanged.
  FVMCheckAllocatedCount(fd, slices_total - slices_left, slices_total);

  // Allocate exactly the remaining number of slices
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(slice_count, slices_left);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }

  slice_count += slices_left;
  slices_left = 0;
  FVMCheckAllocatedCount(fd, slices_total - slices_left, slices_total);

  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;
    ASSERT_EQ(block_info.block_count * block_info.block_size, slice_size * slice_count);
  }

  // We can't allocate any more to this VPartition
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(slice_count, 1);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_STATUS(response.status, ZX_ERR_NO_SPACE);
  }

  // We can't allocate a new VPartition
  auto vp2_fd_or = AllocatePartition({
      .type = kTestPartBlobGuid,
      .guid = kTestUniqueGuid2,
      .name = kTestPartBlobName,
  });
  ASSERT_NE(vp2_fd_or.status_value(), ZX_OK, "Expected VPart allocation failure");

  FVMCheckSliceSize(fvm_device(), kSliceSize);
  ValidateFVM(ramdisk_device());
}

// Test allocating very sparse VPartition
TEST_F(FvmTest, TestVPartitionExtendSparse) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);

  auto vp_fd_or = AllocatePartition({
      .type = kTestPartDataGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartDataName,
  });
  ASSERT_EQ(vp_fd_or.status_value(), ZX_OK);
  fbl::unique_fd vp_fd = *std::move(vp_fd_or);
  CheckWriteReadBlock(vp_fd.get(), 0, 1);

  // Double check that we can access a block at this vslice address
  // (this isn't always possible; for certain slice sizes, blocks may be
  // allocatable / freeable, but not addressable).
  size_t bno = (fvm::kMaxVSlices - 1) * (kSliceSize / kBlockSize);
  ASSERT_EQ(bno / (kSliceSize / kBlockSize), (fvm::kMaxVSlices - 1), "bno overflowed");
  ASSERT_EQ((bno * kBlockSize) / kBlockSize, bno, "block access will overflow");

  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd);

  // Try allocating at a location that's slightly too large
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(fvm::kMaxVSlices, 1);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_STATUS(response.status, ZX_ERR_OUT_OF_RANGE);
  }

  // Try allocating at the largest offset
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(fvm::kMaxVSlices - 1, 1);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }

  CheckWriteReadBlock(vp_fd.get(), bno, 1);

  // Try freeing beyond largest offset
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Shrink(fvm::kMaxVSlices, 1);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_STATUS(response.status, ZX_ERR_OUT_OF_RANGE);
  }

  CheckWriteReadBlock(vp_fd.get(), bno, 1);

  // Try freeing at the largest offset
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Shrink(fvm::kMaxVSlices - 1, 1);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }

  CheckNoAccessBlock(vp_fd.get(), bno, 1);

  ASSERT_EQ(close(vp_fd.release()), 0);
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

  auto volume_info_or = fs_management::FvmQuery(fd.get());
  ASSERT_EQ(volume_info_or.status_value(), ZX_OK);
  size_t slice_size = volume_info_or->slice_size;
  const size_t kDiskSize = kBlockSize * kBlockCount;
  size_t slices_total = UsableSlicesCount(kDiskSize, slice_size);
  size_t slices_left = slices_total;

  FVMCheckAllocatedCount(fd, slices_total - slices_left, slices_total);

  // Allocate one VPart
  size_t slice_count = 1;
  auto vp_fd_or = AllocatePartition({
      .slice_count = slice_count,
      .type = kTestPartDataGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartDataName,
  });
  ASSERT_EQ(vp_fd_or.status_value(), ZX_OK, "Couldn't open Volume");
  fbl::unique_fd vp_fd = *std::move(vp_fd_or);
  slices_left--;

  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd);

  // Confirm that the disk reports the correct number of slices
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;
    ASSERT_EQ(block_info.block_count * block_info.block_size, slice_size * slice_count);
    CheckWriteReadBlock(vp_fd.get(), (slice_size / block_info.block_size) - 1, 1);
    CheckNoAccessBlock(vp_fd.get(), (slice_size / block_info.block_size) - 1, 2);
    FVMCheckAllocatedCount(fd, slices_total - slices_left, slices_total);
  }

  // Try shrinking the 0th vslice
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Shrink(0, 1);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_STATUS(response.status, ZX_ERR_OUT_OF_RANGE);
  }

  // Try no-op requests (length = 0).
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(1, 0);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Shrink(1, 0);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }

  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;
    ASSERT_EQ(block_info.block_count * block_info.block_size, slice_size * slice_count);
  }

  // Try again with a portion of the request which is unallocated
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Shrink(1, 2);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_STATUS(response.status, ZX_ERR_INVALID_ARGS);
  }
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;
    ASSERT_EQ(block_info.block_count * block_info.block_size, slice_size * slice_count);
    FVMCheckAllocatedCount(fd, slices_total - slices_left, slices_total);
  }

  // Allocate exactly the remaining number of slices
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(slice_count, slices_left);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }
  slice_count += slices_left;
  slices_left = 0;

  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;
    ASSERT_EQ(block_info.block_count * block_info.block_size, slice_size * slice_count);
    CheckWriteReadBlock(vp_fd.get(), (slice_size / block_info.block_size) - 1, 1);
    CheckWriteReadBlock(vp_fd.get(), (slice_size / block_info.block_size) - 1, 2);
  }
  FVMCheckAllocatedCount(fd, slices_total - slices_left, slices_total);

  // We can't allocate any more to this VPartition
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(slice_count, 1);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_STATUS(response.status, ZX_ERR_NO_SPACE);
  }

  // Try to shrink off the end (okay, since SOME of the slices are allocated)
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Shrink(1, slice_count + 3);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }
  FVMCheckAllocatedCount(fd, 1, slices_total);

  // The same request to shrink should now fail (NONE of the slices are
  // allocated)
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Shrink(1, slice_count - 1);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_STATUS(response.status, ZX_ERR_INVALID_ARGS);
  }
  FVMCheckAllocatedCount(fd, 1, slices_total);

  // ... unless we re-allocate and try again.
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(1, slice_count - 1);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Shrink(1, slice_count - 1);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }

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

  auto volume_info_or = fs_management::FvmQuery(fd.get());
  ASSERT_EQ(volume_info_or.status_value(), ZX_OK);
  size_t slice_size = volume_info_or->slice_size;

  // Allocate one VPart
  size_t slice_count = 5;
  auto vp_fd_or = AllocatePartition({
      .slice_count = slice_count,
      .type = kTestPartDataGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartDataName,
  });
  ASSERT_EQ(vp_fd_or.status_value(), ZX_OK);
  fbl::unique_fd vp_fd = *std::move(vp_fd_or);

  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd);

  // Confirm that the disk reports the correct number of slices
  const fidl::WireResult result =
      fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);
  const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;
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

  auto doExtend = [&partition_caller](extend_request_t request) {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(request.offset, request.length);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  };

  auto doShrink = [&partition_caller](extend_request_t request) {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Shrink(request.offset, request.length);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  };

  // We should be able to split the extent.
  verifyExtents(true, true, true);
  doShrink(mid_erequest);
  verifyExtents(true, false, true);
  doShrink(start_erequest);
  verifyExtents(false, false, true);
  doShrink(end_erequest);
  verifyExtents(false, false, false);

  doExtend(reset_erequest);

  doShrink(start_erequest);
  verifyExtents(false, true, true);
  doShrink(mid_erequest);
  verifyExtents(false, false, true);
  doShrink(end_erequest);
  verifyExtents(false, false, false);

  doExtend(reset_erequest);

  doShrink(end_erequest);
  verifyExtents(true, true, false);
  doShrink(mid_erequest);
  verifyExtents(true, false, false);
  doShrink(start_erequest);
  verifyExtents(false, false, false);

  doExtend(reset_erequest);

  doShrink(end_erequest);
  verifyExtents(true, true, false);
  doShrink(start_erequest);
  verifyExtents(false, true, false);
  doShrink(mid_erequest);
  verifyExtents(false, false, false);

  // We should also be able to combine extents
  doExtend(mid_erequest);
  verifyExtents(false, true, false);
  doExtend(start_erequest);
  verifyExtents(true, true, false);
  doExtend(end_erequest);
  verifyExtents(true, true, true);

  doShrink(reset_erequest);

  doExtend(end_erequest);
  verifyExtents(false, false, true);
  doExtend(mid_erequest);
  verifyExtents(false, true, true);
  doExtend(start_erequest);
  verifyExtents(true, true, true);

  doShrink(reset_erequest);

  doExtend(end_erequest);
  verifyExtents(false, false, true);
  doExtend(start_erequest);
  verifyExtents(true, false, true);
  doExtend(mid_erequest);
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
  auto data_fd_or = AllocatePartition({
      .type = kTestPartDataGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartDataName,
  });
  ASSERT_EQ(data_fd_or.status_value(), ZX_OK);
  fbl::unique_fd data_fd = *std::move(data_fd_or);
  fdio_cpp::UnownedFdioCaller data_caller(data_fd);

  auto blob_fd_or = AllocatePartition({
      .type = kTestPartBlobGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartBlobName,
  });
  ASSERT_EQ(blob_fd_or.status_value(), ZX_OK);
  fbl::unique_fd blob_fd = *std::move(blob_fd_or);
  fdio_cpp::UnownedFdioCaller blob_caller(blob_fd);

  auto sys_fd_or = AllocatePartition({
      .type = kTestPartSystemGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartSystemName,
  });
  ASSERT_EQ(sys_fd_or.status_value(), ZX_OK);
  fbl::unique_fd sys_fd = *std::move(sys_fd_or);
  fdio_cpp::UnownedFdioCaller sys_caller(sys_fd.get());

  // We can access all three...
  CheckWriteReadBlock(data_fd.get(), 0, 1);
  CheckWriteReadBlock(blob_fd.get(), 0, 1);
  CheckWriteReadBlock(sys_fd.get(), 0, 1);

  // But not after we destroy the blob partition.
  {
    const fidl::WireResult result =
        fidl::WireCall(blob_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())->Destroy();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }
  CheckWriteReadBlock(data_fd.get(), 0, 1);
  CheckWriteReadBlock(sys_fd.get(), 0, 1);
  CheckDeadConnection(blob_fd.get());

  // Destroy the other two VPartitions.
  {
    const fidl::WireResult result =
        fidl::WireCall(data_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())->Destroy();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }
  CheckWriteReadBlock(sys_fd.get(), 0, 1);
  CheckDeadConnection(data_fd.get());

  {
    const fidl::WireResult result =
        fidl::WireCall(sys_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())->Destroy();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }
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
  auto part_fd_or = AllocatePartition({
      .slice_count = 10,
      .type = kTestPartDataGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartDataName,
  });
  ASSERT_EQ(part_fd_or.status_value(), ZX_OK);
  fbl::unique_fd part_fd = *std::move(part_fd_or);
  fdio_cpp::FdioCaller partition_caller(std::move(part_fd));

  // Create non-contiguous extent.
  uint64_t offset = 20;
  uint64_t length = 10;
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(offset, length);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }

  auto volume_info_or = fs_management::FvmQuery(fd.get());
  ASSERT_EQ(volume_info_or.status_value(), ZX_OK);

  // Query various vslice ranges
  uint64_t start_slices[6];
  start_slices[0] = 0;
  start_slices[1] = 10;
  start_slices[2] = 20;
  start_slices[3] = 50;
  start_slices[4] = 25;
  start_slices[5] = 15;

  // Check response from partition query
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->QuerySlices(fidl::VectorView<uint64_t>::FromExternal(start_slices));
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    fidl::Array ranges = response.response;

    ASSERT_EQ(response.response_count, std::size(start_slices));
    ASSERT_TRUE(ranges[0].allocated);
    ASSERT_EQ(ranges[0].count, 10);
    ASSERT_FALSE(ranges[1].allocated);
    ASSERT_EQ(ranges[1].count, 10);
    ASSERT_TRUE(ranges[2].allocated);
    ASSERT_EQ(ranges[2].count, 10);
    ASSERT_FALSE(ranges[3].allocated);
    ASSERT_EQ(ranges[3].count, volume_info_or->max_virtual_slice - 50);
    ASSERT_TRUE(ranges[4].allocated);
    ASSERT_EQ(ranges[4].count, 5);
    ASSERT_FALSE(ranges[5].allocated);
    ASSERT_EQ(ranges[5].count, 5);
  }

  // Merge the extents!
  offset = 10;
  length = 10;
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(offset, length);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }

  // Check partition query response again after extend
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->QuerySlices(fidl::VectorView<uint64_t>::FromExternal(start_slices));
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    fidl::Array ranges = response.response;

    ASSERT_EQ(response.response_count, std::size(start_slices));
    ASSERT_TRUE(ranges[0].allocated);
    ASSERT_EQ(ranges[0].count, 30);
    ASSERT_TRUE(ranges[1].allocated);
    ASSERT_EQ(ranges[1].count, 20);
    ASSERT_TRUE(ranges[2].allocated);
    ASSERT_EQ(ranges[2].count, 10);
    ASSERT_FALSE(ranges[3].allocated);
    ASSERT_EQ(ranges[3].count, volume_info_or->max_virtual_slice - 50);
    ASSERT_TRUE(ranges[4].allocated);
    ASSERT_EQ(ranges[4].count, 5);
    ASSERT_TRUE(ranges[5].allocated);
    ASSERT_EQ(ranges[5].count, 15);
  }

  start_slices[0] = volume_info_or->max_virtual_slice + 1;
  const fidl::WireResult result =
      fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
          ->QuerySlices(fidl::VectorView<uint64_t>::FromExternal(start_slices));
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_STATUS(response.status, ZX_ERR_OUT_OF_RANGE);

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

  auto volume_info_or = fs_management::FvmQuery(fd.get());
  ASSERT_EQ(volume_info_or.status_value(), ZX_OK);
  size_t slice_size = volume_info_or->slice_size;

  // Allocate one VPart
  auto vp_fd_or = AllocatePartition({
      .type = kTestPartDataGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartDataName,
  });
  ASSERT_EQ(vp_fd_or.status_value(), ZX_OK);
  fbl::unique_fd vp_fd = *std::move(vp_fd_or);

  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd);

  const fidl::WireResult result =
      fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);
  const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;

  // This is the last 'accessible' block.
  size_t last_block = (slice_size / block_info.block_size) - 1;

  {
    auto vc = fbl::MakeRefCounted<VmoClient>(vp_fd.get());
    VmoBuf vb(vc, block_info.block_size * static_cast<size_t>(2));
    vc->CheckWrite(vb, 0, block_info.block_size * last_block, block_info.block_size);
    vc->CheckRead(vb, 0, block_info.block_size * last_block, block_info.block_size);

    // Try writing out of bounds -- check that we don't have access.
    CheckNoAccessBlock(vp_fd.get(), (slice_size / block_info.block_size) - 1, 2);
    CheckNoAccessBlock(vp_fd.get(), slice_size / block_info.block_size, 1);

    // Attempt to access the next contiguous slice
    {
      const fidl::WireResult result =
          fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
              ->Extend(1, 1);
      ASSERT_OK(result.status());
      const fidl::WireResponse response = result.value();
      ASSERT_OK(response.status);
    }

    // Now we can access the next slice...
    vc->CheckWrite(vb, block_info.block_size, block_info.block_size * (last_block + 1),
                   block_info.block_size);
    vc->CheckRead(vb, block_info.block_size, block_info.block_size * (last_block + 1),
                  block_info.block_size);
    // ... We can still access the previous slice...
    vc->CheckRead(vb, 0, block_info.block_size * last_block, block_info.block_size);
    // ... And we can cross slices
    vc->CheckRead(vb, 0, block_info.block_size * last_block,
                  block_info.block_size * static_cast<size_t>(2));
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

  auto volume_info_or = fs_management::FvmQuery(fd.get());
  ASSERT_EQ(volume_info_or.status_value(), ZX_OK);
  ASSERT_EQ(volume_info_or->slice_size, kSliceSize);

  // Allocate one VPart
  auto vp_fd_or = AllocatePartition({
      .type = kTestPartDataGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartDataName,
  });
  ASSERT_EQ(vp_fd_or.status_value(), ZX_OK);
  fbl::unique_fd vp_fd = *std::move(vp_fd_or);

  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd);

  const fidl::WireResult result =
      fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);
  const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;
  ASSERT_EQ(block_info.block_size, kBlockSize);

  {
    auto vc = fbl::MakeRefCounted<VmoClient>(vp_fd.get());
    VmoBuf vb(vc, kSliceSize * 3);

    // Access the first slice
    vc->CheckWrite(vb, 0, 0, kSliceSize);
    vc->CheckRead(vb, 0, 0, kSliceSize);

    // Try writing out of bounds -- check that we don't have access.
    CheckNoAccessBlock(vp_fd.get(), kBlocksPerSlice - 1, 2);
    CheckNoAccessBlock(vp_fd.get(), kBlocksPerSlice, 1);

    // Attempt to access the next contiguous slices
    uint64_t offset = 1;
    uint64_t length = 2;
    {
      const fidl::WireResult result =
          fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
              ->Extend(offset, length);
      ASSERT_OK(result.status());
      const fidl::WireResponse response = result.value();
      ASSERT_OK(response.status);
    }

    // Now we can access the next slices...
    vc->CheckWrite(vb, kSliceSize, kSliceSize, 2 * kSliceSize);
    vc->CheckRead(vb, kSliceSize, kSliceSize, 2 * kSliceSize);
    // ... We can still access the previous slice...
    vc->CheckRead(vb, 0, 0, kSliceSize);
    // ... And we can cross slices for reading.
    vc->CheckRead(vb, 0, 0, 3 * kSliceSize);

    // Also, we can cross slices for writing.
    vc->CheckWrite(vb, 0, 0, 3 * kSliceSize);
    vc->CheckRead(vb, 0, 0, 3 * kSliceSize);

    // Additionally, we can access "parts" of slices in a multi-slice
    // operation. Here, read one block into the first slice, and read
    // up to the last block in the final slice.
    vc->CheckWrite(vb, 0, kBlockSize, 3 * kSliceSize - 2 * kBlockSize);
    vc->CheckRead(vb, 0, kBlockSize, 3 * kSliceSize - 2 * kBlockSize);
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
  constexpr uint64_t kBlockSize{512};
  constexpr uint64_t kBlockCount{1 << 16};
  constexpr uint64_t kSliceSize{kBlockSize * 64};
  constexpr uint64_t kDiskSize{kBlockSize * kBlockCount};
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd(fvm_device());
  ASSERT_TRUE(fd);

  ASSERT_EQ(fs_management::FvmQuery(fd.get()).status_value(), ZX_OK);

  constexpr size_t kNumVParts = 3;
  constexpr size_t kSliceCount = 1;
  typedef struct vdata {
    fbl::unique_fd fd;
    const BlockGuid& guid;
    const BlockName& name;
    size_t slices_used;
  } vdata_t;

  vdata_t vparts[kNumVParts] = {
      {fbl::unique_fd(), kTestPartDataGuid, kTestPartDataName, kSliceCount},
      {fbl::unique_fd(), kTestPartBlobGuid, kTestPartBlobName, kSliceCount},
      {fbl::unique_fd(), kTestPartSystemGuid, kTestPartSystemName, kSliceCount},
  };

  for (auto& vpart : vparts) {
    auto fd_or = AllocatePartition({
        .slice_count = kSliceCount,
        .type = vpart.guid,
        .guid = kTestUniqueGuid1,
        .name = vpart.name,
    });
    ASSERT_EQ(fd_or.status_value(), ZX_OK);
    vpart.fd = *std::move(fd_or);
  }

  fdio_cpp::UnownedFdioCaller partition_caller(vparts[0].fd);
  const fidl::WireResult result =
      fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);
  const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;

  size_t usable_slices_per_vpart = UsableSlicesCount(kDiskSize, kSliceSize) / kNumVParts;
  size_t i = 0;
  while (vparts[i].slices_used < usable_slices_per_vpart) {
    int vfd = vparts[i].fd.get();
    // This is the last 'accessible' block.
    size_t last_block = (vparts[i].slices_used * (kSliceSize / block_info.block_size)) - 1;

    auto vc = fbl::MakeRefCounted<VmoClient>(vfd);
    VmoBuf vb(vc, block_info.block_size * static_cast<size_t>(2));

    vc->CheckWrite(vb, 0, block_info.block_size * last_block, block_info.block_size);
    vc->CheckRead(vb, 0, block_info.block_size * last_block, block_info.block_size);

    // Try writing out of bounds -- check that we don't have access.
    CheckNoAccessBlock(vfd, last_block, 2);
    CheckNoAccessBlock(vfd, last_block + 1, 1);

    // Attempt to access the next contiguous slice
    fdio_cpp::UnownedFdioCaller partition_caller(vfd);
    zx::unowned_channel partition_channel(partition_caller.borrow_channel());
    uint64_t offset = vparts[i].slices_used;
    uint64_t length = 1;
    {
      const fidl::WireResult result =
          fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
              ->Extend(offset, length);
      ASSERT_OK(result.status());
      const fidl::WireResponse response = result.value();
      ASSERT_OK(response.status);
    }

    // Now we can access the next slice...
    vc->CheckWrite(vb, block_info.block_size, block_info.block_size * (last_block + 1),
                   block_info.block_size);
    vc->CheckRead(vb, block_info.block_size, block_info.block_size * (last_block + 1),
                  block_info.block_size);
    // ... We can still access the previous slice...
    vc->CheckRead(vb, 0, block_info.block_size * last_block, block_info.block_size);
    // ... And we can cross slices
    vc->CheckRead(vb, 0, block_info.block_size * last_block,
                  block_info.block_size * static_cast<size_t>(2));

    vparts[i].slices_used++;
    i = (i + 1) % kNumVParts;
  }

  for (size_t i = 0; i < kNumVParts; i++) {
    printf("Testing multi-slice operations on vslice %lu\n", i);

    // We need at least five slices, so we can access three "middle"
    // slices and jitter to test off-by-one errors.
    ASSERT_GE(vparts[i].slices_used, 5);

    {
      auto vc = fbl::MakeRefCounted<VmoClient>(vparts[i].fd.get());
      VmoBuf vb(vc, kSliceSize * 4);

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
            vc->CheckWrite(vb, vmo_off, dev_off, len);
            vc->CheckRead(vb, vmo_off, dev_off, len);

            // Try reading the section one slice at a time.
            // The results should be the same.
            size_t sub_off = 0;
            size_t sub_len = kSliceSize - (dev_off % kSliceSize);
            while (sub_off < len) {
              vc->CheckRead(vb, vmo_off + sub_off, dev_off + sub_off, sub_len);
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
  constexpr uint64_t kBlockSize{512};
  constexpr uint64_t kBlockCount{1 << 20};
  constexpr uint64_t kSliceSize{UINT64_C(64) * (1 << 20)};
  constexpr uint64_t kDiskSize{kBlockSize * kBlockCount};
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd = fvm_device();
  ASSERT_TRUE(fd);

  ASSERT_EQ(fs_management::FvmQuery(fd.get()).status_value(), ZX_OK);

  constexpr size_t kNumVParts = 3;
  constexpr size_t kSliceCount = 1;
  typedef struct vdata {
    fbl::unique_fd fd;
    const BlockGuid& guid;
    const BlockName& name;
    size_t slices_used;
    size_t last_slice;
  } vdata_t;

  vdata_t vparts[kNumVParts] = {
      {fbl::unique_fd(), kTestPartDataGuid, kTestPartDataName, kSliceCount, kSliceCount},
      {fbl::unique_fd(), kTestPartBlobGuid, kTestPartBlobName, kSliceCount, kSliceCount},
      {fbl::unique_fd(), kTestPartSystemGuid, kTestPartSystemName, kSliceCount, kSliceCount},
  };

  for (auto& vpart : vparts) {
    auto fd_or = AllocatePartition({
        .slice_count = kSliceCount,
        .type = vpart.guid,
        .guid = kTestUniqueGuid1,
        .name = vpart.name,
    });
    ASSERT_EQ(fd_or.status_value(), ZX_OK);
    vpart.fd = *std::move(fd_or);
  }

  fdio_cpp::UnownedFdioCaller partition_caller(vparts[0].fd);
  const fidl::WireResult result =
      fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);
  const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;

  size_t usable_slices_per_vpart = UsableSlicesCount(kDiskSize, kSliceSize) / kNumVParts;
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
    {
      const fidl::WireResult result =
          fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
              ->Extend(offset, length);
      ASSERT_OK(result.status());
      const fidl::WireResponse response = result.value();
      ASSERT_OK(response.status);
    }

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

  for (vdata_t& vpart : vparts) {
    ASSERT_EQ(close(vpart.fd.release()), 0);
  }

  ASSERT_EQ(close(fd.release()), 0);
  FVMCheckSliceSize(fvm_device(), kSliceSize);
  ValidateFVM(ramdisk_device());
}

// Test that the FVM driver actually persists updates.
TEST_F(FvmTest, TestPersistenceSimple) {
  constexpr uint64_t kBlockSize{512};
  constexpr uint64_t kBlockCount{1 << 20};
  constexpr uint64_t kSliceSize{UINT64_C(64) * (1 << 20)};
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd(fvm_device());
  ASSERT_TRUE(fd);

  constexpr uint64_t kDiskSize = kBlockSize * kBlockCount;
  size_t slices_left = UsableSlicesCount(kDiskSize, kSliceSize);
  const uint64_t kSliceCount = slices_left;

  ASSERT_EQ(fs_management::FvmQuery(fd.get()).status_value(), ZX_OK);

  // Allocate one VPart
  auto vp_fd_or = AllocatePartition({
      .type = kTestPartDataGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartDataName,
  });
  ASSERT_EQ(vp_fd_or.status_value(), ZX_OK);
  fbl::unique_fd vp_fd = *std::move(vp_fd_or);
  slices_left--;

  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd);

  // Check that the name matches what we provided
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_partition::Partition>())
            ->GetName();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    ASSERT_STREQ(response.name.get(), kTestPartDataName.data());
  }

  fuchsia_hardware_block::wire::BlockInfo block_info;
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    block_info = *response.info;
  }
  std::unique_ptr<uint8_t[]> buf(new uint8_t[block_info.block_size * static_cast<size_t>(2)]);

  // Check that we can read from / write to it
  CheckWrite(vp_fd.get(), 0, block_info.block_size, buf.get());
  CheckRead(vp_fd.get(), 0, block_info.block_size, buf.get());
  ASSERT_EQ(close(vp_fd.release()), 0);

  // Check that it still exists after rebinding the driver
  ASSERT_EQ(close(fd.release()), 0);
  FVMRebind();
  fd = fvm_device();
  ASSERT_TRUE(fd, "Failed to rebind FVM driver");

  vp_fd_or = WaitForPartition(kPartition1Matcher);
  ASSERT_OK(vp_fd_or.status_value());
  vp_fd = *std::move(vp_fd_or);
  CheckRead(vp_fd.get(), 0, block_info.block_size, buf.get());

  // Try extending the vpartition, and checking that the extension persists.
  // This is the last 'accessible' block.
  size_t last_block = (kSliceSize / block_info.block_size) - 1;
  CheckWrite(vp_fd.get(), block_info.block_size * last_block, block_info.block_size, buf.get());
  CheckRead(vp_fd.get(), block_info.block_size * last_block, block_info.block_size, buf.get());

  // Try writing out of bounds -- check that we don't have access.
  CheckNoAccessBlock(vp_fd.get(), (kSliceSize / block_info.block_size) - 1, 2);
  CheckNoAccessBlock(vp_fd.get(), kSliceSize / block_info.block_size, 1);

  partition_caller.reset(vp_fd);
  uint64_t offset = 1;
  uint64_t length = 1;
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(offset, length);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }
  slices_left--;

  ASSERT_EQ(close(vp_fd.release()), 0);
  // FVMRebind will cause the rebind on ramdisk block device. The fvm device is child device
  // to ramdisk block device. Before issuing rebind make sure the fd is released.
  // Rebind the FVM driver, check the extension has succeeded.
  ASSERT_EQ(close(fd.release()), 0);
  FVMRebind();
  fd = fvm_device();
  ASSERT_TRUE(fd, "Failed to rebind FVM driver");

  vp_fd_or = WaitForPartition(kPartition1Matcher);
  ASSERT_OK(vp_fd_or.status_value());
  vp_fd = *std::move(vp_fd_or);

  partition_caller.reset(vp_fd);

  // Now we can access the next slice...
  CheckWrite(vp_fd.get(), block_info.block_size * (last_block + 1), block_info.block_size,
             &buf[block_info.block_size]);
  CheckRead(vp_fd.get(), block_info.block_size * (last_block + 1), block_info.block_size,
            &buf[block_info.block_size]);
  // ... We can still access the previous slice...
  CheckRead(vp_fd.get(), block_info.block_size * last_block, block_info.block_size, buf.get());
  // ... And we can cross slices
  CheckRead(vp_fd.get(), block_info.block_size * last_block,
            block_info.block_size * static_cast<size_t>(2), buf.get());

  // Try allocating the rest of the slices, rebinding, and ensuring
  // that the size stays updated.
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    block_info = *response.info;
  }
  ASSERT_EQ(block_info.block_count * block_info.block_size, kSliceSize * 2);

  offset = 2;
  length = slices_left;
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(offset, length);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    block_info = *response.info;
  }
  ASSERT_EQ(block_info.block_count * block_info.block_size, kSliceSize * kSliceCount);

  ASSERT_EQ(close(vp_fd.release()), 0);
  ASSERT_EQ(close(fd.release()), 0);
  FVMRebind();
  fd = fvm_device();
  ASSERT_TRUE(fd, "Failed to rebind FVM driver");

  vp_fd_or = WaitForPartition(kPartition1Matcher);
  ASSERT_OK(vp_fd_or.status_value());
  vp_fd = *std::move(vp_fd_or);
  partition_caller.reset(vp_fd);

  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    block_info = *response.info;
  }
  ASSERT_EQ(block_info.block_count * block_info.block_size, kSliceSize * kSliceCount);

  ASSERT_EQ(close(vp_fd.release()), 0);
  ASSERT_EQ(close(fd.release()), 0);
  FVMCheckSliceSize(fvm_device(), 64lu * (1 << 20));
}

void CorruptMountHelper(const fbl::unique_fd& devfs_root, const char* partition_path,
                        const fs_management::MountOptions& mounting_options,
                        fs_management::DiskFormat disk_format,
                        const query_request_t& query_request) {
  // Format the VPart as |disk_format|.
  fs_management::MkfsOptions mkfs_options{
      .component_child_name = mounting_options.component_child_name,
      .component_collection_name = mounting_options.component_collection_name,
  };
  ASSERT_EQ(fs_management::Mkfs(partition_path, disk_format, fs_management::LaunchStdioSync,
                                mkfs_options),
            ZX_OK);

  auto vp_fd_or =
      fs_management::OpenPartitionWithDevfs(devfs_root.get(), kPartition1Matcher, 0, nullptr);
  ASSERT_EQ(vp_fd_or.status_value(), ZX_OK);
  fbl::unique_fd vp_fd(*std::move(vp_fd_or));

  fuchsia_hardware_block_volume::wire::VsliceRange
      initial_ranges[fuchsia_hardware_block_volume::wire::kMaxSliceRequests];

  // Check initial slice allocation.
  //
  // Avoid keeping the "FdioCaller" in-scope across mount, as the caller prevents
  // the file descriptor from being transferred.
  {
    fdio_cpp::UnownedFdioCaller partition_caller(vp_fd);
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->QuerySlices(fidl::VectorView<uint64_t>::FromExternal(
                const_cast<size_t*>(query_request.vslice_start), query_request.count));
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    ASSERT_EQ(query_request.count, response.response_count);

    for (unsigned i = 0; i < response.response_count; i++) {
      ASSERT_TRUE(response.response[i].allocated);
      ASSERT_GT(response.response[i].count, 0);
      initial_ranges[i] = response.response[i];
    }

    // Manually shrink slices so FVM will differ from the partition.
    uint64_t offset = query_request.vslice_start[0] + response.response[0].count - 1;
    uint64_t length = 1;
    {
      const fidl::WireResult result =
          fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
              ->Shrink(offset, length);
      ASSERT_OK(result.status());
      const fidl::WireResponse response = result.value();
      ASSERT_OK(response.status);
    }

    // Check slice allocation after manual grow/shrink
    {
      const fidl::WireResult result =
          fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
              ->QuerySlices(fidl::VectorView<uint64_t>::FromExternal(
                  const_cast<size_t*>(query_request.vslice_start), query_request.count));
      ASSERT_OK(result.status());
      const fidl::WireResponse response = result.value();
      ASSERT_OK(response.status);
      ASSERT_EQ(query_request.count, response.response_count);
      ASSERT_FALSE(response.response[0].allocated);
      ASSERT_EQ(response.response[0].count,
                query_request.vslice_start[1] - query_request.vslice_start[0]);
    }
  }

  // Try to mount the VPart. Since this mount call is supposed to fail, we wait for the spawned
  // fs process to finish and associated fidl channels to close before continuing to try and prevent
  // race conditions with the later mount call.
  ASSERT_NE(fs_management::Mount(std::move(vp_fd), disk_format, mounting_options,
                                 fs_management::LaunchStdioSync)
                .status_value(),
            ZX_OK);

  {
    vp_fd_or =
        fs_management::OpenPartitionWithDevfs(devfs_root.get(), kPartition1Matcher, 0, nullptr);
    ASSERT_EQ(vp_fd_or.status_value(), ZX_OK);
    vp_fd = *std::move(vp_fd_or);

    fdio_cpp::UnownedFdioCaller partition_caller(vp_fd);

    // Grow back the slice we shrunk earlier.
    uint64_t offset = query_request.vslice_start[0];
    uint64_t length = 1;
    {
      const fidl::WireResult result =
          fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
              ->Extend(offset, length);
      ASSERT_OK(result.status());
      const fidl::WireResponse response = result.value();
      ASSERT_OK(response.status);
    }

    // Verify grow was successful.
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->QuerySlices(fidl::VectorView<uint64_t>::FromExternal(
                const_cast<size_t*>(query_request.vslice_start), query_request.count));
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    ASSERT_EQ(query_request.count, response.response_count);
    ASSERT_TRUE(response.response[0].allocated);
    ASSERT_EQ(response.response[0].count, 1);

    // Now extend all extents by some number of additional slices.
    fuchsia_hardware_block_volume::wire::VsliceRange
        ranges_before_extend[fuchsia_hardware_block_volume::wire::kMaxSliceRequests];
    for (unsigned i = 0; i < query_request.count; i++) {
      ranges_before_extend[i] = response.response[i];
      uint64_t offset = query_request.vslice_start[i] + response.response[i].count;
      uint64_t length = query_request.count - i;
      {
        const fidl::WireResult result =
            fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
                ->Extend(offset, length);
        ASSERT_OK(result.status());
        const fidl::WireResponse response = result.value();
        ASSERT_OK(response.status);
      }
    }

    // Verify that the extensions were successful.
    {
      const fidl::WireResult result =
          fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
              ->QuerySlices(fidl::VectorView<uint64_t>::FromExternal(
                  const_cast<size_t*>(query_request.vslice_start), query_request.count));
      ASSERT_OK(result.status());
      const fidl::WireResponse response = result.value();
      ASSERT_OK(response.status);
      ASSERT_EQ(query_request.count, response.response_count);
      for (unsigned i = 0; i < query_request.count; i++) {
        ASSERT_TRUE(response.response[i].allocated);
        ASSERT_EQ(response.response[i].count,
                  ranges_before_extend[i].count + query_request.count - i);
      }
    }
  }

  // Try mount again.
  ASSERT_EQ(fs_management::Mount(std::move(vp_fd), disk_format, mounting_options,
                                 fs_management::LaunchStdioAsync)
                .status_value(),
            ZX_OK);

  vp_fd_or =
      fs_management::OpenPartitionWithDevfs(devfs_root.get(), kPartition1Matcher, 0, nullptr);
  ASSERT_EQ(vp_fd_or.status_value(), ZX_OK);
  vp_fd = *std::move(vp_fd_or);
  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd);
  zx::unowned_channel partition_channel(partition_caller.borrow_channel());

  // Verify that slices were fixed on mount.
  const fidl::WireResult result =
      fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
          ->QuerySlices(fidl::VectorView<uint64_t>::FromExternal(
              const_cast<size_t*>(query_request.vslice_start), query_request.count));
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);
  ASSERT_EQ(query_request.count, response.response_count);

  for (unsigned i = 0; i < query_request.count; i++) {
    ASSERT_TRUE(response.response[i].allocated);
    ASSERT_EQ(response.response[i].count, initial_ranges[i].count);
  }
}

TEST_F(FvmTest, TestCorruptMount) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd = fvm_device();
  ASSERT_TRUE(fd);

  auto volume_info_or = fs_management::FvmQuery(fd.get());
  ASSERT_EQ(volume_info_or.status_value(), ZX_OK);
  ASSERT_EQ(kSliceSize, volume_info_or->slice_size);

  // Allocate one VPart
  auto vp_fd_or = AllocatePartition({
      .type = kTestPartDataGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartDataName,
  });
  ASSERT_OK(vp_fd_or.status_value());

  auto partition_path = GetPartitionPath(vp_fd_or->get());
  ASSERT_OK(partition_path.status_value());

  size_t kMinfsBlocksPerSlice = kSliceSize / minfs::kMinfsBlockSize;
  query_request_t query_request;
  query_request.count = 4;
  query_request.vslice_start[0] = minfs::kFVMBlockInodeBmStart / kMinfsBlocksPerSlice;
  query_request.vslice_start[1] = minfs::kFVMBlockDataBmStart / kMinfsBlocksPerSlice;
  query_request.vslice_start[2] = minfs::kFVMBlockInodeStart / kMinfsBlocksPerSlice;
  query_request.vslice_start[3] = minfs::kFVMBlockDataStart / kMinfsBlocksPerSlice;

  // Run the test for Minfs.
  CorruptMountHelper(devfs_root(), partition_path->c_str(), mounting_options_,
                     fs_management::kDiskFormatMinfs, query_request);

  size_t kBlobfsBlocksPerSlice = kSliceSize / blobfs::kBlobfsBlockSize;
  query_request.count = 3;
  query_request.vslice_start[0] = blobfs::kFVMBlockMapStart / kBlobfsBlocksPerSlice;
  query_request.vslice_start[1] = blobfs::kFVMNodeMapStart / kBlobfsBlocksPerSlice;
  query_request.vslice_start[2] = blobfs::kFVMDataStart / kBlobfsBlocksPerSlice;

  // Run the test for Blobfs.
  fs_management::MountOptions options = mounting_options_;
  options.component_child_name = kTestBlobfsChildName;
  options.component_collection_name = kTestCollectionName;
  CorruptMountHelper(devfs_root(), partition_path->c_str(), options,
                     fs_management::kDiskFormatBlobfs, query_request);

  // Clean up
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

  // Allocate two VParts, one active, and one inactive.
  {
    auto vp_fd_or = AllocatePartition({
        .type = kTestPartDataGuid,
        .guid = kTestUniqueGuid1,
        .name = kTestPartDataName,
        .flags = fuchsia_hardware_block_volume::wire::kAllocatePartitionFlagInactive,
    });
    ASSERT_EQ(vp_fd_or.status_value(), ZX_OK, "Couldn't open Volume");
  }

  {
    auto vp_fd_or = AllocatePartition({
        .type = kTestPartDataGuid,
        .guid = kTestUniqueGuid2,
        .name = kTestPartBlobName,
    });
    ASSERT_OK(vp_fd_or.status_value(), "Couldn't open volume");
  }

  // Release FVM device that we opened earlier
  ASSERT_EQ(close(volume_manager.release().get()), 0);
  FVMRebind();
  volume_manager.reset(fvm_device());

  // The active partition should still exist.
  ASSERT_OK(WaitForPartition(kPartition2Matcher).status_value());
  // The inactive partition should be gone.
  ASSERT_NE(OpenPartition(kPartition1Matcher).status_value(), ZX_OK);

  // Reallocate GUID1 as inactive.

  {
    auto vp_fd_or = AllocatePartition({
        .type = kTestPartDataGuid,
        .guid = kTestUniqueGuid1,
        .name = kTestPartDataName,
        .flags = fuchsia_hardware_block_volume::wire::kAllocatePartitionFlagInactive,
    });
    ASSERT_OK(vp_fd_or.status_value(), "Couldn't open new volume");
  }

  // Atomically set GUID1 as active and GUID2 as inactive.
  Upgrade(volume_manager, kTestUniqueGuid2, kTestUniqueGuid1, ZX_OK);

  // After upgrading, we should be able to open both partitions
  ASSERT_OK(WaitForPartition(kPartition1Matcher).status_value());
  ASSERT_OK(WaitForPartition(kPartition2Matcher).status_value());

  // Rebind the FVM driver, check that the upgrade has succeeded.
  // The original (GUID2) should be deleted, and the new partition (GUID)
  // should exist.
  // Release FVM device that we opened earlier
  ASSERT_EQ(close(volume_manager.release().get()), 0);
  FVMRebind();
  volume_manager.reset(fvm_device());

  ASSERT_OK(WaitForPartition(kPartition1Matcher).status_value());
  ASSERT_NE(OpenPartition(kPartition2Matcher).status_value(), ZX_OK);

  // Try upgrading when the "new" version doesn't exist.
  // (It should return an error and have no noticeable effect).
  Upgrade(volume_manager, kTestUniqueGuid1, kTestUniqueGuid2, ZX_ERR_NOT_FOUND);

  // Release FVM device that we opened earlier
  ASSERT_EQ(close(volume_manager.release().get()), 0);
  FVMRebind();
  volume_manager.reset(fvm_device());

  ASSERT_OK(WaitForPartition(kPartition1Matcher).status_value());
  ASSERT_NE(OpenPartition(kPartition2Matcher).status_value(), ZX_OK);

  // Try upgrading when the "old" version doesn't exist.
  {
    auto vp_fd_or = AllocatePartition({
        .type = kTestPartDataGuid,
        .guid = kTestUniqueGuid2,
        .name = kTestPartBlobName,
        .flags = fuchsia_hardware_block_volume::wire::kAllocatePartitionFlagInactive,
    });
    ASSERT_EQ(vp_fd_or.status_value(), ZX_OK, "Couldn't open volume");
  }

  BlockGuid fake_guid = {};
  Upgrade(volume_manager, fake_guid, kTestUniqueGuid2, ZX_OK);

  // Release FVM device that we opened earlier
  ASSERT_EQ(close(volume_manager.release().get()), 0);
  FVMRebind();
  volume_manager.reset(fvm_device());

  // We should be able to open both partitions again.
  auto vp_fd_or = WaitForPartition(kPartition1Matcher);
  ASSERT_OK(vp_fd_or.status_value());
  ASSERT_OK(WaitForPartition(kPartition2Matcher).status_value());

  // Destroy and reallocate the first partition as inactive.
  {
    fdio_cpp::FdioCaller partition_caller(*std::move(vp_fd_or));
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Destroy();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }
  {
    auto vp_fd_or = AllocatePartition({
        .type = kTestPartDataGuid,
        .guid = kTestUniqueGuid1,
        .name = kTestPartDataName,
        .flags = fuchsia_hardware_block_volume::wire::kAllocatePartitionFlagInactive,
    });
    ASSERT_EQ(vp_fd_or.status_value(), ZX_OK, "Couldn't open volume");
  }

  // Upgrade the partition with old_guid == new_guid.
  // This should activate the partition.
  Upgrade(volume_manager, kTestUniqueGuid1, kTestUniqueGuid1, ZX_OK);

  // Release FVM device that we opened earlier
  ASSERT_EQ(close(volume_manager.release().get()), 0);
  FVMRebind();
  volume_manager.reset(fvm_device());

  // We should be able to open both partitions again.
  ASSERT_OK(WaitForPartition(kPartition1Matcher).status_value());
  ASSERT_OK(WaitForPartition(kPartition2Matcher).status_value());
}

// Test that the FVM driver can mount filesystems.
TEST_F(FvmTest, TestMounting) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);
  fbl::unique_fd fd = fvm_device();
  ASSERT_TRUE(fd);

  auto volume_info_or = fs_management::FvmQuery(fd.get());
  ASSERT_EQ(volume_info_or.status_value(), ZX_OK);

  // Allocate one VPart
  size_t slice_count = 5;
  auto vp_fd_or = AllocatePartition({
      .slice_count = slice_count,
      .type = kTestPartDataGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartDataName,
  });
  ASSERT_EQ(vp_fd_or.status_value(), ZX_OK);
  fbl::unique_fd vp_fd(*std::move(vp_fd_or));

  // Format the VPart as minfs
  auto partition_path = GetPartitionPath(vp_fd.get());
  ASSERT_OK(partition_path.status_value());
  ASSERT_EQ(fs_management::Mkfs(partition_path->c_str(), fs_management::kDiskFormatMinfs,
                                fs_management::LaunchStdioSync, fs_management::MkfsOptions()),
            ZX_OK);

  // Mount the VPart
  auto mounted_filesystem =
      fs_management::Mount(std::move(vp_fd), fs_management::kDiskFormatMinfs, mounting_options_,
                           fs_management::LaunchStdioAsync);
  ASSERT_EQ(mounted_filesystem.status_value(), ZX_OK);
  auto data = mounted_filesystem->DataRoot();
  ASSERT_EQ(data.status_value(), ZX_OK);
  auto binding = fs_management::NamespaceBinding::Create(kMountPath, std::move(*data));
  ASSERT_EQ(binding.status_value(), ZX_OK);

  // Verify that the mount was successful.
  fbl::unique_fd rootfd(open(kMountPath, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(rootfd);
  fdio_cpp::FdioCaller caller(std::move(rootfd));
  auto result = fidl::WireCall(caller.directory())->QueryFilesystem();
  ASSERT_TRUE(result.ok());
  const char* kFsName = "minfs";
  const char* name = reinterpret_cast<const char*>(result.value().info->name.data());
  ASSERT_EQ(strncmp(name, kFsName, strlen(kFsName)), 0, "Unexpected filesystem mounted");

  // Verify that MinFS does not try to use more of the VPartition than
  // was originally allocated.
  ASSERT_LE(result.value().info->total_bytes, kSliceSize * slice_count);

  // Clean up.
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

  auto volume_info_or = fs_management::FvmQuery(fd.get());
  ASSERT_EQ(volume_info_or.status_value(), ZX_OK);

  // Allocate one VPart.
  size_t slice_count = 5;
  auto vp_fd_or = AllocatePartition({
      .slice_count = slice_count,
      .type = kTestPartDataGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartDataName,
  });
  ASSERT_EQ(vp_fd_or.status_value(), ZX_OK);
  fbl::unique_fd vp_fd(*std::move(vp_fd_or));

  // Format the VPart as minfs.
  auto partition_path = GetPartitionPath(vp_fd.get());
  ASSERT_OK(partition_path.status_value());
  ASSERT_EQ(fs_management::Mkfs(partition_path->c_str(), fs_management::kDiskFormatMinfs,
                                fs_management::LaunchStdioSync, fs_management::MkfsOptions()),
            ZX_OK);

  // Format it as MinFS again, even though it is already formatted.
  ASSERT_EQ(fs_management::Mkfs(partition_path->c_str(), fs_management::kDiskFormatMinfs,
                                fs_management::LaunchStdioSync, fs_management::MkfsOptions()),
            ZX_OK);

  // Now try reformatting as blobfs.
  fs_management::MkfsOptions mkfs_options = fs_management::MkfsOptions{
      .component_child_name = kTestBlobfsChildName,
      .component_collection_name = kTestCollectionName,
  };
  ASSERT_EQ(fs_management::Mkfs(partition_path->c_str(), fs_management::kDiskFormatBlobfs,
                                fs_management::LaunchStdioSync, mkfs_options),
            ZX_OK);

  // Demonstrate that mounting as minfs will fail, but mounting as blobfs
  // is successful.
  ASSERT_NE(fs_management::Mount(std::move(vp_fd), fs_management::kDiskFormatMinfs,
                                 mounting_options_, fs_management::LaunchStdioSync)
                .status_value(),
            ZX_OK);
  vp_fd.reset(open(partition_path->c_str(), O_RDWR));
  ASSERT_TRUE(vp_fd);

  fs_management::MountOptions mounting_options = mounting_options_;
  mounting_options.component_child_name = kTestBlobfsChildName;
  mounting_options.component_collection_name = kTestCollectionName;
  ASSERT_EQ(fs_management::Mount(std::move(vp_fd), fs_management::kDiskFormatBlobfs,
                                 mounting_options, fs_management::LaunchStdioAsync)
                .status_value(),
            ZX_OK);

  // ... and reformat back to MinFS again.
  ASSERT_EQ(fs_management::Mkfs(partition_path->c_str(), fs_management::kDiskFormatMinfs,
                                fs_management::LaunchStdioSync, fs_management::MkfsOptions()),
            ZX_OK);

  // Mount the VPart.
  vp_fd.reset(open(partition_path->c_str(), O_RDWR));
  ASSERT_TRUE(vp_fd);
  auto mounted_filesystem =
      fs_management::Mount(std::move(vp_fd), fs_management::kDiskFormatMinfs, mounting_options_,
                           fs_management::LaunchStdioAsync);
  ASSERT_EQ(mounted_filesystem.status_value(), ZX_OK);
  auto data = mounted_filesystem->DataRoot();
  ASSERT_EQ(data.status_value(), ZX_OK);
  auto binding = fs_management::NamespaceBinding::Create(kMountPath, std::move(*data));
  ASSERT_EQ(binding.status_value(), ZX_OK);

  // Verify that the mount was successful.
  fbl::unique_fd rootfd(open(kMountPath, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(rootfd);
  fdio_cpp::FdioCaller caller(std::move(rootfd));
  auto result = fidl::WireCall(caller.directory())->QueryFilesystem();
  ASSERT_TRUE(result.ok());
  const char* kFsName = "minfs";
  const char* name = reinterpret_cast<const char*>(result.value().info->name.data());
  ASSERT_EQ(strncmp(name, kFsName, strlen(kFsName)), 0, "Unexpected filesystem mounted");

  // Verify that MinFS does not try to use more of the VPartition than
  // was originally allocated.
  ASSERT_LE(result.value().info->total_bytes, kSliceSize * slice_count);

  // Clean up.
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

  auto volume_info_or = fs_management::FvmQuery(fd.get());
  ASSERT_EQ(volume_info_or.status_value(), ZX_OK);

  // Allocate one VPart (writes to backup)
  auto vp_fd_or = AllocatePartition({
      .type = kTestPartDataGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartDataName,
  });
  ASSERT_EQ(vp_fd_or.status_value(), ZX_OK);
  fbl::unique_fd vp_fd(*std::move(vp_fd_or));

  // Extend the vpart (writes to primary)
  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd);
  uint64_t offset = 1;
  uint64_t length = 1;
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(offset, length);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }
  const fidl::WireResult result =
      fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);
  const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;
  ASSERT_EQ(block_info.block_count * block_info.block_size, kSliceSize * 2);

  // Initial slice access
  CheckWriteReadBlock(vp_fd.get(), 0, 1);
  // Extended slice access
  CheckWriteReadBlock(vp_fd.get(), kSliceSize / block_info.block_size, 1);

  ASSERT_EQ(close(vp_fd.release()), 0);

  // Corrupt the (backup) metadata and rebind.
  // The 'primary' was the last one written, so it'll be used.
  fvm::Header header =
      fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions, kBlockSize * kBlockCount, kSliceSize);
  auto off = static_cast<off_t>(header.GetSuperblockOffset(fvm::SuperblockType::kSecondary));
  uint8_t buf[fvm::kBlockSize];
  ASSERT_EQ(lseek(ramdisk_fd.get(), off, SEEK_SET), off);
  ASSERT_EQ(read(ramdisk_fd.get(), buf, sizeof(buf)), sizeof(buf));
  // Modify an arbitrary byte (not the magic bits; we still want it to mount!)
  buf[128]++;
  ASSERT_EQ(lseek(ramdisk_fd.get(), off, SEEK_SET), off);
  ASSERT_EQ(write(ramdisk_fd.get(), buf, sizeof(buf)), sizeof(buf));

  ASSERT_EQ(close(fd.release()), 0);
  FVMRebind();

  vp_fd_or = WaitForPartition(kPartition1Matcher);
  ASSERT_EQ(vp_fd_or.status_value(), ZX_OK, "Couldn't re-open Data VPart");
  vp_fd = *std::move(vp_fd_or);

  // The slice extension is still accessible.
  CheckWriteReadBlock(vp_fd.get(), 0, 1);
  CheckWriteReadBlock(vp_fd.get(), kSliceSize / block_info.block_size, 1);

  // Clean up
  ASSERT_EQ(close(vp_fd.release()), 0);
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

  auto volume_info_or = fs_management::FvmQuery(fd.get());
  ASSERT_EQ(volume_info_or.status_value(), ZX_OK);
  size_t slice_size = volume_info_or->slice_size;

  // Allocate one VPart (writes to backup)
  auto vp_fd_or = AllocatePartition({
      .type = kTestPartDataGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartDataName,
  });
  ASSERT_EQ(vp_fd_or.status_value(), ZX_OK);
  fbl::unique_fd vp_fd(*std::move(vp_fd_or));

  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd);
  zx::unowned_channel partition_channel(partition_caller.borrow_channel());

  // Extend the vpart (writes to primary)
  uint64_t offset = 1;
  uint64_t length = 1;
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(offset, length);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }
  const fidl::WireResult result =
      fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);
  const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;
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

  ASSERT_EQ(close(fd.release()), 0);
  FVMRebind();

  vp_fd_or = WaitForPartition(kPartition1Matcher);
  ASSERT_EQ(vp_fd_or.status_value(), ZX_OK);
  vp_fd = *std::move(vp_fd_or);

  // The slice extension is no longer accessible
  CheckWriteReadBlock(vp_fd.get(), 0, 1);
  CheckNoAccessBlock(vp_fd.get(), slice_size / block_info.block_size, 1);

  // Clean up
  ASSERT_EQ(close(vp_fd.release()), 0);
  ASSERT_EQ(close(ramdisk_fd.release()), 0);
  FVMCheckSliceSize(fvm_device(), kSliceSize);
}

TEST_F(FvmTest, TestCorruptionUnrecoverable) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);

  // Allocate one VPart (writes to backup)
  auto vp_fd_or = AllocatePartition({
      .type = kTestPartDataGuid,
      .guid = kTestUniqueGuid1,
      .name = kTestPartDataName,
  });
  ASSERT_EQ(vp_fd_or.status_value(), ZX_OK);
  fbl::unique_fd vp_fd(*std::move(vp_fd_or));

  fdio_cpp::UnownedFdioCaller partition_caller(vp_fd.get());
  zx::unowned_channel partition_channel(partition_caller.borrow_channel());

  // Extend the vpart (writes to primary)
  uint64_t offset = 1;
  uint64_t length = 1;
  {
    const fidl::WireResult result =
        fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block_volume::Volume>())
            ->Extend(offset, length);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }
  const fidl::WireResult result =
      fidl::WireCall(partition_caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);
  const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;
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

  fvm::Header header =
      fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions, kBlockSize * kBlockCount, kSliceSize);
  off = static_cast<off_t>(header.GetSuperblockOffset(fvm::SuperblockType::kSecondary));
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

  // Init fvm with a partition bigger than the underlying disk.
  fs_management::FvmInitWithSize(ramdisk_block_interface(), kFvmPartitionSize, kSliceSize);

  // Try to bind an fvm to the disk.
  //
  // Bind should return ZX_ERR_IO when the load of a driver fails.
  auto resp =
      fidl::WireCall(ramdisk_controller_interface())->Bind(fidl::StringView(FVM_DRIVER_LIB));
  ASSERT_OK(resp.status());
  ASSERT_FALSE(resp->is_ok());
  ASSERT_EQ(resp->error_value(), ZX_ERR_INTERNAL);

  // Grow the ramdisk to the appropiate size and bind should succeed.
  ASSERT_OK(ramdisk_grow(ramdisk(), kFvmPartitionSize));
  // Use Controller::Call::Rebind because the driver might still be
  // when init fails. Driver removes the device and will eventually be
  // unloaded but Controller::Bind above does not wait until
  // the device is removed. Controller::Rebind ensures nothing is
  // bound to the device, before it tries to bind the driver again.
  auto resp2 =
      fidl::WireCall(ramdisk_controller_interface())->Rebind(fidl::StringView(FVM_DRIVER_LIB));
  ASSERT_OK(resp2.status());
  ASSERT_TRUE(resp2->is_ok());
  char fvm_path[PATH_MAX];
  snprintf(fvm_path, sizeof(fvm_path), "%s/fvm", ramdisk_path());
  ASSERT_OK(wait_for_device(fvm_path, zx::duration::infinite().get()));
}

TEST_F(FvmTest, TestPreventDuplicateDeviceNames) {
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 1 << 16;
  constexpr uint64_t kSliceSize = 64 * kBlockSize;
  CreateFVM(kBlockSize, kBlockCount, kSliceSize);

  // When a partition is destroyed, the slot in FVM is synchronously freed but the device is
  // asynchronously removed. DFv2 prevents multiple child devices with the same name from being
  // bound. This test rapidly allocates and destroys the same partition to try and get a race
  // between the new device being bound and the old device being removed to try and get FVM to bind
  // multiple devices with the same name.
  for (int i = 0; i < 10; ++i) {
    auto vp_fd_or = AllocatePartition({
        .type = kTestPartDataGuid,
        .guid = kTestUniqueGuid1,
        .name = kTestPartDataName,
    });
    ASSERT_OK(vp_fd_or.status_value());
    fdio_cpp::UnownedFdioCaller caller(std::move(vp_fd_or).value());
    auto volume = caller.borrow_as<fuchsia_hardware_block_volume::Volume>();
    auto result = fidl::WireCall(volume)->Destroy();
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }
}

}  // namespace
