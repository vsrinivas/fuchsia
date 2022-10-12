// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_STORAGE_LIB_PAVER_TEST_TEST_UTILS_H_
#define SRC_STORAGE_LIB_PAVER_TEST_TEST_UTILS_H_
#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sys/component/cpp/service_client.h>

#include <memory>

#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>
#include <ramdevice-client-test/ramnandctl.h>
#include <ramdevice-client/ramdisk.h>
#include <ramdevice-client/ramnand.h>
#include <zxtest/zxtest.h>

#include "lib/fdio/directory.h"
#include "lib/fidl-async/cpp/bind.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/storage/lib/paver/device-partitioner.h"

constexpr uint64_t kBlockSize = 0x1000;
constexpr uint32_t kBlockCount = 0x100;
constexpr uint64_t kGptBlockCount = 2048;

constexpr uint32_t kOobSize = 8;
constexpr uint32_t kPageSize = 2048;
constexpr uint32_t kPagesPerBlock = 128;
constexpr uint32_t kSkipBlockSize = kPageSize * kPagesPerBlock;
constexpr uint32_t kNumBlocks = 400;

class BlockDevice {
 public:
  static void Create(const fbl::unique_fd& devfs_root, const uint8_t* guid,
                     std::unique_ptr<BlockDevice>* device);

  static void Create(const fbl::unique_fd& devfs_root, const uint8_t* guid, uint64_t block_count,
                     std::unique_ptr<BlockDevice>* device);

  static void Create(const fbl::unique_fd& devfs_root, const uint8_t* guid, uint64_t block_count,
                     uint32_t block_size, std::unique_ptr<BlockDevice>* device);

  ~BlockDevice() { ramdisk_destroy(client_); }

  // Does not transfer ownership of the file descriptor.
  int fd() const { return ramdisk_get_block_fd(client_); }

  // Block count and block size of this device.
  uint64_t block_count() const { return block_count_; }
  uint32_t block_size() const { return block_size_; }

 private:
  BlockDevice(ramdisk_client_t* client, uint64_t block_count, uint32_t block_size)
      : client_(client), block_count_(block_count), block_size_(block_size) {}

  ramdisk_client_t* client_;
  const uint64_t block_count_;
  const uint32_t block_size_;
};

class SkipBlockDevice {
 public:
  static void Create(const fuchsia_hardware_nand_RamNandInfo& nand_info,
                     std::unique_ptr<SkipBlockDevice>* device);

  fbl::unique_fd devfs_root() { return ctl_->devfs_root().duplicate(); }

  fzl::VmoMapper& mapper() { return mapper_; }

  ~SkipBlockDevice() = default;

 private:
  SkipBlockDevice(fbl::RefPtr<ramdevice_client_test::RamNandCtl> ctl,
                  ramdevice_client::RamNand ram_nand, fzl::VmoMapper mapper)
      : ctl_(std::move(ctl)), ram_nand_(std::move(ram_nand)), mapper_(std::move(mapper)) {}

  fbl::RefPtr<ramdevice_client_test::RamNandCtl> ctl_;
  ramdevice_client::RamNand ram_nand_;
  fzl::VmoMapper mapper_;
};

// Dummy DevicePartition implementation meant to be used for testing. All functions are no-ops, i.e.
// they silently pass without doing anything. Tests can inherit from this class and override
// functions that are relevant for their test cases; this class provides an easy way to inherit from
// DevicePartitioner which is an abstract class.
class FakeDevicePartitioner : public paver::DevicePartitioner {
 public:
  bool IsFvmWithinFtl() const override { return false; }

  bool SupportsPartition(const paver::PartitionSpec& spec) const override { return true; }

  zx::status<std::unique_ptr<paver::PartitionClient>> FindPartition(
      const paver::PartitionSpec& spec) const override {
    return zx::ok(nullptr);
  }

  zx::status<> FinalizePartition(const paver::PartitionSpec& spec) const override {
    return zx::ok();
  }

  zx::status<std::unique_ptr<paver::PartitionClient>> AddPartition(
      const paver::PartitionSpec& spec) const override {
    return zx::ok(nullptr);
  }

  zx::status<> WipeFvm() const override { return zx::ok(); }

  zx::status<> InitPartitionTables() const override { return zx::ok(); }

  zx::status<> WipePartitionTables() const override { return zx::ok(); }

  zx::status<> ValidatePayload(const paver::PartitionSpec& spec,
                               cpp20::span<const uint8_t> data) const override {
    return zx::ok();
  }
};

// Defines a PartitionClient that reads and writes to a partition backed by a VMO in memory.
// Used for testing.
class FakePartitionClient : public paver::BlockDevicePartitionClient {
 public:
  FakePartitionClient(size_t block_count, size_t block_size);
  explicit FakePartitionClient(size_t block_count);

  zx::status<size_t> GetBlockSize();
  zx::status<size_t> GetPartitionSize();
  zx::status<> Read(const zx::vmo& vmo, size_t size);
  zx::status<> Write(const zx::vmo& vmo, size_t vmo_size);
  zx::status<> Trim();
  zx::status<> Flush();
  fidl::ClientEnd<fuchsia_hardware_block::Block> GetChannel();
  fbl::unique_fd block_fd();

 protected:
  zx::vmo partition_;
  size_t block_size_;
  size_t partition_size_;
};

template <typename T>
class FakeSvc {
 public:
  explicit FakeSvc(async_dispatcher_t* dispatcher, T args)
      : dispatcher_(dispatcher), vfs_(dispatcher), fake_boot_args_(std::move(args)) {
    root_dir_ = fbl::MakeRefCounted<fs::PseudoDir>();
    root_dir_->AddEntry(
        fidl::DiscoverableProtocolName<fuchsia_boot::Arguments>,
        fbl::MakeRefCounted<fs::Service>([this](zx::channel request) {
          return fidl::BindSingleInFlightOnly<fidl::WireServer<fuchsia_boot::Arguments>>(
              dispatcher_, std::move(request), &fake_boot_args_);
        }));

    auto svc_remote = fidl::CreateEndpoints(&svc_local_);
    ASSERT_OK(svc_remote.status_value());

    vfs_.ServeDirectory(root_dir_, std::move(*svc_remote));
  }

  void ForwardServiceTo(const char* name, fidl::ClientEnd<fuchsia_io::Directory> svc) {
    root_dir_->AddEntry(
        name, fbl::MakeRefCounted<fs::Service>([name, svc = std::move(svc)](zx::channel request) {
          return fdio_service_connect_at(svc.channel().get(), fbl::StringPrintf("%s", name).data(),
                                         request.release());
        }));
  }

  T& fake_boot_args() { return fake_boot_args_; }
  fidl::ClientEnd<fuchsia_io::Directory>& svc_chan() { return svc_local_; }

 private:
  async_dispatcher_t* dispatcher_;
  fbl::RefPtr<fs::PseudoDir> root_dir_;
  fs::SynchronousVfs vfs_;
  T fake_boot_args_;
  fidl::ClientEnd<fuchsia_io::Directory> svc_local_;
};

#endif  // SRC_STORAGE_LIB_PAVER_TEST_TEST_UTILS_H_
