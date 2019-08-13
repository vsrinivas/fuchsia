// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/nand/c/fidl.h>
#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fzl/fdio.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/paver/provider.h>
#include <lib/zx/vmo.h>
#include <lib/fdio/directory.h>
#include <zircon/hw/gpt.h>

#include <optional>

#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
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
                    .partition_count = 8,
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
                                .type_guid = GUID_VBMETA_A_VALUE,
                                .unique_guid = {},
                                .first_block = 14,
                                .last_block = 15,
                                .copy_count = 0,
                                .copy_byte_offset = 0,
                                .name = {'v', 'b', 'm', 'e', 't', 'a', '-', 'a'},
                                .hidden = false,
                                .bbt = false,
                            },
                            {
                                .type_guid = GUID_VBMETA_B_VALUE,
                                .unique_guid = {},
                                .first_block = 16,
                                .last_block = 17,
                                .copy_count = 0,
                                .copy_byte_offset = 0,
                                .name = {'v', 'b', 'm', 'e', 't', 'a', '-', 'b'},
                                .hidden = false,
                                .bbt = false,
                            },
                            {
                                .type_guid = GUID_VBMETA_R_VALUE,
                                .unique_guid = {},
                                .first_block = 18,
                                .last_block = 19,
                                .copy_count = 0,
                                .copy_byte_offset = 0,
                                .name = {'v', 'b', 'm', 'e', 't', 'a', '-', 'r'},
                                .hidden = false,
                                .bbt = false,
                            },
                        },
                },
            .export_nand_config = true,
            .export_partition_map = true,
};

class PaverServiceTest : public zxtest::Test {
 public:
  PaverServiceTest() : loop_(&kAsyncLoopConfigAttachToThread) {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));

    client_.emplace(std::move(client));

    ASSERT_OK(paver_get_service_provider()->ops->init(&provider_ctx_));

    ASSERT_OK(paver_get_service_provider()->ops->connect(
        provider_ctx_, loop_.dispatcher(), ::llcpp::fuchsia::paver::Paver::Name, server.release()));
    loop_.StartThread("paver-svc-test-loop");
  }

  ~PaverServiceTest() {
    paver_get_service_provider()->ops->release(provider_ctx_);
    provider_ctx_ = nullptr;
  }

 protected:
  void SpawnIsolatedDevmgr() {
    ASSERT_EQ(device_.get(), nullptr);
    SkipBlockDevice::Create(kNandInfo, &device_);
    static_cast<paver::Paver*>(provider_ctx_)->set_devfs_root(device_->devfs_root());
  }

  // Spawn an isolated devmgr without a skip-block device.
  void SpawnIsolatedDevmgrBlock() {
    devmgr_launcher::Args args;
    args.sys_device_driver = IsolatedDevmgr::kSysdevDriver;
    args.driver_search_paths.push_back("/boot/driver");
    args.use_system_svchost = true;
    args.disable_block_watcher = true;
    ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr_));

    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "misc/ramctl", &fd));
    static_cast<paver::Paver*>(provider_ctx_)->set_devfs_root(devmgr_.devfs_root().duplicate());
  }

  void CreatePayload(size_t num_blocks, ::llcpp::fuchsia::mem::Buffer* out) {
    zx::vmo vmo;
    fzl::VmoMapper mapper;
    const size_t size = kPageSize * kPagesPerBlock * num_blocks;
    ASSERT_OK(mapper.CreateAndMap(size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo));
    memset(mapper.start(), 0x4a, mapper.size());
    out->vmo = std::move(vmo);
    out->size = size;
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

  void* provider_ctx_ = nullptr;
  fbl::unique_ptr<SkipBlockDevice> device_;
  IsolatedDevmgr devmgr_;
  std::optional<::llcpp::fuchsia::paver::Paver::SyncClient> client_;
  async::Loop loop_;
};

TEST_F(PaverServiceTest, QueryActiveConfiguration) {
  auto result = client_->QueryActiveConfiguration();
  ASSERT_OK(result.status());
  const auto& response = result.value();
  ASSERT_TRUE(response.result.is_err());
  ASSERT_EQ(response.result.err(), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PaverServiceTest, SetActiveConfiguration) {
  auto configuration = ::llcpp::fuchsia::paver::Configuration::A;
  auto result = client_->SetActiveConfiguration(configuration);
  ASSERT_OK(result.status());
  ASSERT_STATUS(result.value().status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PaverServiceTest, MarkActiveConfigurationSuccessful) {
  auto result = client_->MarkActiveConfigurationSuccessful();
  ASSERT_OK(result.status());
  ASSERT_STATUS(result.value().status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PaverServiceTest, ForceRecoveryConfiguration) {
  auto result = client_->ForceRecoveryConfiguration();
  ASSERT_OK(result.status());
  ASSERT_STATUS(result.value().status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PaverServiceTest, WriteAssetKernelConfigA) {
  SpawnIsolatedDevmgr();
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(2, &payload);
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
  CreatePayload(2, &payload);
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
  CreatePayload(2, &payload);
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
  CreatePayload(2, &payload);
  auto result = client_->WriteAsset(::llcpp::fuchsia::paver::Configuration::A,
                                    ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA,
                                    std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateWritten(14, 2);
  ValidateUnwritten(16, 4);
}

TEST_F(PaverServiceTest, WriteAssetVbMetaConfigB) {
  SpawnIsolatedDevmgr();
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(2, &payload);
  auto result = client_->WriteAsset(::llcpp::fuchsia::paver::Configuration::B,
                                    ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA,
                                    std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateUnwritten(14, 2);
  ValidateWritten(16, 2);
  ValidateUnwritten(18, 2);
}

TEST_F(PaverServiceTest, WriteAssetVbMetaConfigRecovery) {
  SpawnIsolatedDevmgr();
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(2, &payload);
  auto result = client_->WriteAsset(::llcpp::fuchsia::paver::Configuration::RECOVERY,
                                    ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA,
                                    std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateUnwritten(14, 4);
  ValidateWritten(18, 2);
}

TEST_F(PaverServiceTest, WriteAssetTwice) {
  SpawnIsolatedDevmgr();
  ::llcpp::fuchsia::mem::Buffer payload;
  CreatePayload(2, &payload);
  auto result = client_->WriteAsset(::llcpp::fuchsia::paver::Configuration::A,
                                    ::llcpp::fuchsia::paver::Asset::KERNEL, std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  CreatePayload(2, &payload);
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
  CreatePayload(4, &payload);
  auto result = client_->WriteBootloader(std::move(payload));
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
  ValidateWritten(4, 4);
}

TEST_F(PaverServiceTest, WriteDataFile) {
  // TODO(ZX-4007): Figure out a way to test this.
}

TEST_F(PaverServiceTest, WriteVolumes) {
  // TODO(ZX-4007): Figure out a way to test this.
}

TEST_F(PaverServiceTest, WipeVolumes) {
  SpawnIsolatedDevmgr();
  auto result = client_->WipeVolumes(zx::channel());
  ASSERT_OK(result.status());
  ASSERT_OK(result.value().status);
}

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
#endif

}  // namespace
