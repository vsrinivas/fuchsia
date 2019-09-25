// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>
#include <fcntl.h>
#include <fuchsia/boot/llcpp/fidl.h>
#include <fuchsia/hardware/nand/c/fidl.h>
#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/cksum.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fzl/fdio.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/paver/provider.h>
#include <lib/zx/vmo.h>
#include <zircon/hw/gpt.h>

#include <optional>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <zxtest/zxtest.h>

#include "device-partitioner.h"
#include "paver.h"
#include "test/test-utils.h"

namespace {

using devmgr_integration_test::IsolatedDevmgr;
using devmgr_integration_test::RecursiveWaitForFile;

constexpr fuchsia_hardware_nand_RamNandInfo
    kNandInfo =
        {
            .vmo = ZX_HANDLE_INVALID,
            .nand_info =
                {
                    .page_size = kPageSize,
                    .pages_per_block = kPagesPerBlock,
                    .num_blocks = kNumBlocks,
                    .ecc_bits = 8,
                    .oob_size = kOobSize,
                    .nand_class = fuchsia_hardware_nand_Class_PARTMAP,
                    .partition_guid = {},
                },
            .partition_map =
                {
                    .device_guid = {},
                    .partition_count = 6,
                    .partitions =
                        {
                            {
                                .type_guid = {},
                                .unique_guid = {},
                                .first_block = 0,
                                .last_block = 3,
                                .copy_count = 0,
                                .copy_byte_offset = 0,
                                .name = {},
                                .hidden = true,
                                .bbt = true,
                            },
                            {
                                .type_guid = GUID_BOOTLOADER_VALUE,
                                .unique_guid = {},
                                .first_block = 4,
                                .last_block = 7,
                                .copy_count = 0,
                                .copy_byte_offset = 0,
                                .name = {'b', 'o', 'o', 't', 'l', 'o', 'a', 'd', 'e', 'r'},
                                .hidden = false,
                                .bbt = false,
                            },
                            {
                                .type_guid = GUID_ZIRCON_A_VALUE,
                                .unique_guid = {},
                                .first_block = 8,
                                .last_block = 9,
                                .copy_count = 0,
                                .copy_byte_offset = 0,
                                .name = {'z', 'i', 'r', 'c', 'o', 'n', '-', 'a'},
                                .hidden = false,
                                .bbt = false,
                            },
                            {
                                .type_guid = GUID_ZIRCON_B_VALUE,
                                .unique_guid = {},
                                .first_block = 10,
                                .last_block = 11,
                                .copy_count = 0,
                                .copy_byte_offset = 0,
                                .name = {'z', 'i', 'r', 'c', 'o', 'n', '-', 'b'},
                                .hidden = false,
                                .bbt = false,
                            },
                            {
                                .type_guid = GUID_ZIRCON_R_VALUE,
                                .unique_guid = {},
                                .first_block = 12,
                                .last_block = 13,
                                .copy_count = 0,
                                .copy_byte_offset = 0,
                                .name = {'z', 'i', 'r', 'c', 'o', 'n', '-', 'r'},
                                .hidden = false,
                                .bbt = false,
                            },
                            {
                                .type_guid = GUID_SYS_CONFIG_VALUE,
                                .unique_guid = {},
                                .first_block = 14,
                                .last_block = 17,
                                .copy_count = 0,
                                .copy_byte_offset = 0,
                                .name = {'s', 'y', 's', 'c', 'o', 'n', 'f', 'i', 'g'},
                                .hidden = false,
                                .bbt = false,
                            },
                        },
                },
            .export_nand_config = true,
            .export_partition_map = true,
};

class FakeBootArgs : public ::llcpp::fuchsia::boot::Arguments::Interface {
 public:
  zx_status_t Connect(async_dispatcher_t* dispatcher, zx::channel request) {
    return fidl::Bind(dispatcher, std::move(request), this);
  }

  void Get(GetCompleter::Sync completer) {
    zx::vmo vmo;
    zx::vmo::create(fbl::round_up(sizeof(kArgs), ZX_PAGE_SIZE), 0, &vmo);
    vmo.write(kArgs, 0, sizeof(kArgs));
    completer.Reply(std::move(vmo), sizeof(kArgs));
  }

 private:
  static constexpr char kArgs[] = "zvb.current_slot=_a";
};

class FakeSvc {
 public:
  explicit FakeSvc(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher), vfs_(dispatcher) {
    auto root_dir = fbl::MakeRefCounted<fs::PseudoDir>();
    root_dir->AddEntry(::llcpp::fuchsia::boot::Arguments::Name,
                       fbl::MakeRefCounted<fs::Service>([this](zx::channel request) {
                         return fake_boot_args_.Connect(dispatcher_, std::move(request));
                       }));

    zx::channel svc_remote;
    ASSERT_OK(zx::channel::create(0, &svc_local_, &svc_remote));

    vfs_.ServeDirectory(root_dir, std::move(svc_remote));
  }

  FakeBootArgs& fake_boot_args() { return fake_boot_args_; }
  zx::channel& svc_chan() { return svc_local_; }

 private:
  async_dispatcher_t* dispatcher_;
  fs::SynchronousVfs vfs_;
  FakeBootArgs fake_boot_args_;
  zx::channel svc_local_;
};

class PaverServiceTest : public zxtest::Test {
 public:
  PaverServiceTest();

  ~PaverServiceTest();

 protected:
  void SpawnIsolatedDevmgr();

  // Spawn an isolated devmgr without a skip-block device.
  void SpawnIsolatedDevmgrBlock();

  void CreatePayload(size_t num_pages, ::llcpp::fuchsia::mem::Buffer* out);

  static constexpr size_t kKilobyte = 1 << 10;

  void SetAbr(const abr::Data& data) {
    auto* buf = reinterpret_cast<uint8_t*>(device_->mapper().start()) + (14 * kSkipBlockSize) +
                (60 * kKilobyte);
    *reinterpret_cast<abr::Data*>(buf) = data;
  }

  abr::Data GetAbr() {
    auto* buf = reinterpret_cast<uint8_t*>(device_->mapper().start()) + (14 * kSkipBlockSize) +
                (60 * kKilobyte);
    return *reinterpret_cast<abr::Data*>(buf);
  }

  void ValidateWritten(uint32_t block, size_t num_blocks) {
    const uint8_t* start =
        static_cast<uint8_t*>(device_->mapper().start()) + (block * kSkipBlockSize);
    for (size_t i = 0; i < kSkipBlockSize * num_blocks; i++) {
      ASSERT_EQ(start[i], 0x4a, "i = %zu", i);
    }
  }

  void ValidateUnwritten(uint32_t block, size_t num_blocks) {
    const uint8_t* start =
        static_cast<uint8_t*>(device_->mapper().start()) + (block * kSkipBlockSize);
    for (size_t i = 0; i < kSkipBlockSize * num_blocks; i++) {
      ASSERT_EQ(start[i], 0xff, "i = %zu", i);
    }
  }

  void ValidateWrittenPages(uint32_t page, size_t num_pages) {
    const uint8_t* start =
        static_cast<uint8_t*>(device_->mapper().start()) + (page * kPageSize);
    for (size_t i = 0; i < kPageSize * num_pages; i++) {
      ASSERT_EQ(start[i], 0x4a, "i = %zu", i);
    }
  }

  void ValidateUnwrittenPages(uint32_t page, size_t num_pages) {
    const uint8_t* start =
        static_cast<uint8_t*>(device_->mapper().start()) + (page * kPageSize);
    for (size_t i = 0; i < kPageSize * num_pages; i++) {
      ASSERT_EQ(start[i], 0xff, "i = %zu", i);
    }
  }

  void WriteData(uint32_t page, size_t num_pages, uint8_t data) {
    uint8_t* start =
        static_cast<uint8_t*>(device_->mapper().start()) + (page * kPageSize);
    memset(start, data, kPageSize * num_pages);
  }

  void* provider_ctx_ = nullptr;
  fbl::unique_ptr<SkipBlockDevice> device_;
  IsolatedDevmgr devmgr_;
  std::optional<::llcpp::fuchsia::paver::Paver::SyncClient> client_;
  async::Loop loop_;
  // The paver makes synchronous calls into /svc, so it must run in a seperate loop to not
  // deadlock.
  async::Loop loop2_;
  FakeSvc fake_svc_;
};

PaverServiceTest::PaverServiceTest()
    : loop_(&kAsyncLoopConfigAttachToCurrentThread),
      loop2_(&kAsyncLoopConfigNoAttachToCurrentThread),
      fake_svc_(loop2_.dispatcher()) {
  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));

  client_.emplace(std::move(client));

  ASSERT_OK(paver_get_service_provider()->ops->init(&provider_ctx_));

  ASSERT_OK(paver_get_service_provider()->ops->connect(
      provider_ctx_, loop_.dispatcher(), ::llcpp::fuchsia::paver::Paver::Name, server.release()));
  loop_.StartThread("paver-svc-test-loop");
  loop2_.StartThread("paver-svc-test-loop-2");
}

PaverServiceTest::~PaverServiceTest() {
  loop_.Shutdown();
  loop2_.Shutdown();
  paver_get_service_provider()->ops->release(provider_ctx_);
  provider_ctx_ = nullptr;
}

void PaverServiceTest::SpawnIsolatedDevmgr() {
  ASSERT_EQ(device_.get(), nullptr);
  SkipBlockDevice::Create(kNandInfo, &device_);
  static_cast<paver::Paver*>(provider_ctx_)->set_devfs_root(device_->devfs_root());
  static_cast<paver::Paver*>(provider_ctx_)->set_svc_root(std::move(fake_svc_.svc_chan()));
}

void PaverServiceTest::SpawnIsolatedDevmgrBlock() {
  devmgr_launcher::Args args;
  args.sys_device_driver = IsolatedDevmgr::kSysdevDriver;
  args.driver_search_paths.push_back("/boot/driver");
  args.disable_block_watcher = true;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr_));

  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "misc/ramctl", &fd));
  static_cast<paver::Paver*>(provider_ctx_)->set_devfs_root(devmgr_.devfs_root().duplicate());
  static_cast<paver::Paver*>(provider_ctx_)->set_svc_root(std::move(fake_svc_.svc_chan()));
}

void PaverServiceTest::CreatePayload(size_t num_pages, ::llcpp::fuchsia::mem::Buffer* out) {
  zx::vmo vmo;
  fzl::VmoMapper mapper;
  const size_t size = kPageSize * num_pages;
  ASSERT_OK(mapper.CreateAndMap(size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo));
  memset(mapper.start(), 0x4a, mapper.size());
  out->vmo = std::move(vmo);
  out->size = size;
}

constexpr abr::Data kAbrData = {
    .magic = {'\0', 'A', 'B', '0'},
    .version_major = abr::kMajorVersion,
    .version_minor = abr::kMinorVersion,
    .reserved1 = {},
    .slots =
        {
            {
                .priority = 0,
                .tries_remaining = 0,
                .successful_boot = 0,
                .reserved = {},
            },
            {
                .priority = 1,
                .tries_remaining = 0,
                .successful_boot = 1,
                .reserved = {},
            },
        },
    .oneshot_recovery_boot = 0,
    .reserved2 = {},
    .crc32 = {},
};

void ComputeCrc(abr::Data* data) {
  data->crc32 = htobe32(
      crc32(0, reinterpret_cast<const uint8_t*>(data), offsetof(abr::Data, crc32)));
}

TEST_F(PaverServiceTest, QueryActiveConfigurationSlotB) {
  SpawnIsolatedDevmgr();
  abr::Data abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  auto result = client_->QueryActiveConfiguration();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ASSERT_EQ(result->result.response().configuration, ::llcpp::fuchsia::paver::Configuration::B);
}

TEST_F(PaverServiceTest, QueryActiveConfigurationSlotA) {
  SpawnIsolatedDevmgr();
  abr::Data abr_data = kAbrData;
  abr_data.slots[0].priority = 2;
  abr_data.slots[0].successful_boot = 1;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  auto result = client_->QueryActiveConfiguration();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->result.is_response());
  ASSERT_EQ(result->result.response().configuration, ::llcpp::fuchsia::paver::Configuration::A);
}

TEST_F(PaverServiceTest, SetActiveConfiguration) {
  SpawnIsolatedDevmgr();
  abr::Data abr_data = kAbrData;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  abr_data.slots[0].priority = 2;
  abr_data.slots[0].tries_remaining = abr::kMaxTriesRemaining;
  abr_data.slots[0].successful_boot = 0;
  ComputeCrc(&abr_data);

  auto result = client_->SetActiveConfiguration(::llcpp::fuchsia::paver::Configuration::A);
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  auto actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));
}

TEST_F(PaverServiceTest, SetActiveConfigurationRollover) {
  SpawnIsolatedDevmgr();
  abr::Data abr_data = kAbrData;
  abr_data.slots[1].priority = abr::kMaxPriority;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  abr_data.slots[1].priority = 1;
  abr_data.slots[0].priority = 2;
  abr_data.slots[0].tries_remaining = abr::kMaxTriesRemaining;
  abr_data.slots[0].successful_boot = 0;
  ComputeCrc(&abr_data);

  auto result = client_->SetActiveConfiguration(::llcpp::fuchsia::paver::Configuration::A);
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  auto actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));
}

TEST_F(PaverServiceTest, MarkActiveConfigurationSuccessful) {
  SpawnIsolatedDevmgr();
  abr::Data abr_data = kAbrData;
  abr_data.slots[1].tries_remaining = 3;
  abr_data.slots[1].successful_boot = 0;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  abr_data.slots[1].tries_remaining = 0;
  abr_data.slots[1].successful_boot = 1;
  ComputeCrc(&abr_data);

  auto result = client_->MarkActiveConfigurationSuccessful();
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  auto actual = GetAbr();
  ASSERT_BYTES_EQ(&abr_data, &actual, sizeof(abr_data));
}

TEST_F(PaverServiceTest, MarkActiveConfigurationSuccessfulBothPriorityZero) {
  SpawnIsolatedDevmgr();
  abr::Data abr_data = kAbrData;
  abr_data.slots[1].tries_remaining = 3;
  abr_data.slots[1].successful_boot = 0;
  abr_data.slots[1].priority = 0;
  ComputeCrc(&abr_data);
  SetAbr(abr_data);

  auto result = client_->MarkActiveConfigurationSuccessful();
  ASSERT_OK(result.status());
  ASSERT_NE(result->status, ZX_OK);
}

TEST_F(PaverServiceTest, WriteAssetKernelConfigA) {
  SpawnIsolatedDevmgr();
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(2 * kPagesPerBlock, &payload);
  auto result = client_->WriteAsset(::llcpp::fuchsia::paver::Configuration::A,
                                    ::llcpp::fuchsia::paver::Asset::KERNEL, std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateWritten(8, 2);
  ValidateUnwritten(10, 4);
}

TEST_F(PaverServiceTest, WriteAssetKernelConfigB) {
  SpawnIsolatedDevmgr();
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(2 * kPagesPerBlock, &payload);
  auto result = client_->WriteAsset(::llcpp::fuchsia::paver::Configuration::B,
                                    ::llcpp::fuchsia::paver::Asset::KERNEL, std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateUnwritten(8, 2);
  ValidateWritten(10, 2);
  ValidateUnwritten(12, 2);
}

TEST_F(PaverServiceTest, WriteAssetKernelConfigRecovery) {
  SpawnIsolatedDevmgr();
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(2 * kPagesPerBlock, &payload);
  auto result = client_->WriteAsset(::llcpp::fuchsia::paver::Configuration::RECOVERY,
                                    ::llcpp::fuchsia::paver::Asset::KERNEL, std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateUnwritten(8, 4);
  ValidateWritten(12, 2);
}

TEST_F(PaverServiceTest, WriteAssetVbMetaConfigA) {
  SpawnIsolatedDevmgr();
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(32, &payload);
  auto result = client_->WriteAsset(::llcpp::fuchsia::paver::Configuration::A,
                                    ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA,
                                    std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateWrittenPages(14 * kPagesPerBlock + 32, 32);
}

TEST_F(PaverServiceTest, WriteAssetVbMetaConfigB) {
  SpawnIsolatedDevmgr();
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(32, &payload);
  auto result = client_->WriteAsset(::llcpp::fuchsia::paver::Configuration::B,
                                    ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA,
                                    std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateWrittenPages(14 * kPagesPerBlock + 64, 32);
}

TEST_F(PaverServiceTest, WriteAssetVbMetaConfigRecovery) {
  SpawnIsolatedDevmgr();
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(32, &payload);
  auto result = client_->WriteAsset(::llcpp::fuchsia::paver::Configuration::RECOVERY,
                                    ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA,
                                    std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateWrittenPages(14 * kPagesPerBlock + 96, 32);
}

TEST_F(PaverServiceTest, WriteAssetTwice) {
  SpawnIsolatedDevmgr();
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(2 * kPagesPerBlock, &payload);
  auto result = client_->WriteAsset(::llcpp::fuchsia::paver::Configuration::A,
                                    ::llcpp::fuchsia::paver::Asset::KERNEL, std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  CreatePayload(2 * kPagesPerBlock, &payload);
  ValidateWritten(8, 2);
  ValidateUnwritten(10, 4);
  result = client_->WriteAsset(::llcpp::fuchsia::paver::Configuration::A,
                               ::llcpp::fuchsia::paver::Asset::KERNEL, std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateWritten(8, 2);
  ValidateUnwritten(10, 4);
}

TEST_F(PaverServiceTest, WriteBootloader) {
  SpawnIsolatedDevmgr();
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(4 * kPagesPerBlock, &payload);
  auto result = client_->WriteBootloader(std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateWritten(4, 4);
}

// We prefill the bootloader partition with the expected data, leaving the last block as 0xFF.
// Normally the last page would be overwritten with 0s, but because the actual payload is identical,
// we don't actually pave the image, so the extra page stays as 0xFF.
TEST_F(PaverServiceTest, WriteBootloaderNotAligned) {
  SpawnIsolatedDevmgr();
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(4 * kPagesPerBlock, &payload);
  payload.size = 4 * kPagesPerBlock - 1;
  WriteData(4 * kPagesPerBlock, 4 * kPagesPerBlock - 1, 0x4a);
  WriteData(8 * kPagesPerBlock - 1 , 1, 0xff);
  auto result = client_->WriteBootloader(std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateWrittenPages(4 * kPagesPerBlock, 4 * kPagesPerBlock - 1);
  ValidateUnwrittenPages(8 * kPagesPerBlock - 1, 1);
}

TEST_F(PaverServiceTest, WriteDataFile) {
  // TODO(ZX-4007): Figure out a way to test this.
}

TEST_F(PaverServiceTest, WriteVolumes) {
  // TODO(ZX-4007): Figure out a way to test this.
}

TEST_F(PaverServiceTest, WipeVolumes) {
  // TODO(ZX-4007): Figure out a way to test this.
}

// TODO(34771): Re-enable once bug in GPT is fixed.
#if 0
#if defined(__x86_64__)
constexpr uint8_t kEmptyType[GPT_GUID_LEN] = GUID_EMPTY_VALUE;

TEST_F(PaverServiceTest, InitializePartitionTables) {
  ASSERT_NO_FATAL_FAILURES(SpawnIsolatedDevmgrBlock());
  fbl::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t block_count = (1LU << 34) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, block_count,
                                               &gpt_dev));

  zx::channel gpt_chan;
  ASSERT_OK(fdio_fd_clone(gpt_dev->fd(), gpt_chan.reset_and_get_address()));

  auto result = client_->InitializePartitionTables(std::move(gpt_chan));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
}

TEST_F(PaverServiceTest, InitializePartitionTablesMultipleDevices) {
  ASSERT_NO_FATAL_FAILURES(SpawnIsolatedDevmgrBlock());
  fbl::unique_ptr<BlockDevice> gpt_dev1, gpt_dev2;
  constexpr uint64_t block_count = (1LU << 34) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, block_count,
                                               &gpt_dev1));
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, block_count,
                                               &gpt_dev2));

  zx::channel gpt_chan;
  ASSERT_OK(fdio_fd_clone(gpt_dev1->fd(), gpt_chan.reset_and_get_address()));

  auto result = client_->InitializePartitionTables(std::move(gpt_chan));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
}

TEST_F(PaverServiceTest, WipePartitionTables) {
  ASSERT_NO_FATAL_FAILURES(SpawnIsolatedDevmgrBlock());
  fbl::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t block_count = (1LU << 34) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, block_count,
                                               &gpt_dev));

  zx::channel gpt_chan;
  ASSERT_OK(fdio_fd_clone(gpt_dev->fd(), gpt_chan.reset_and_get_address()));

  auto result = client_->InitializePartitionTables(std::move(gpt_chan));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);

  ASSERT_OK(fdio_fd_clone(gpt_dev->fd(), gpt_chan.reset_and_get_address()));

  auto wipe_result = client_->WipePartitionTables(std::move(gpt_chan));
  ASSERT_OK(wipe_result.status());
  ASSERT_OK(wipe_result.value().status);
}
#endif
#endif

}  // namespace
