// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/paver/provider.h>

#include <optional>

#include <fcntl.h>

#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/hardware/nand/c/fidl.h>
#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <zircon/hw/gpt.h>
#include <zxtest/zxtest.h>

#include "device-partitioner.h"
#include "paver.h"
#include "test/test-utils.h"

namespace {

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
    PaverServiceTest()
        : loop_(&kAsyncLoopConfigAttachToThread) {
        zx::channel client, server;
        ASSERT_OK(zx::channel::create(0, &client, &server));

        client_.emplace(std::move(client));

        ASSERT_OK(paver_get_service_provider()->ops->init(&provider_ctx_));

        ASSERT_OK(paver_get_service_provider()->ops->connect(
            provider_ctx_, loop_.dispatcher(), ::llcpp::fuchsia::paver::Paver::Name_,
            server.release()));
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
        const uint8_t* start = static_cast<uint8_t*>(device_->mapper().start()) +
                               (block * kSkipBlockSize);
        for (size_t i = 0; i < kSkipBlockSize * num_blocks; i++) {
            ASSERT_EQ(start[i], 0x4a, "i = %zu", i);
        }
    }

    void ValidateUnwritten(uint32_t block, size_t num_blocks) {
        const uint8_t* start = static_cast<uint8_t*>(device_->mapper().start()) +
                               (block * kSkipBlockSize);
        for (size_t i = 0; i < kSkipBlockSize * num_blocks; i++) {
            ASSERT_EQ(start[i], 0xff, "i = %zu", i);
        }
    }

    void* provider_ctx_ = nullptr;
    fbl::unique_ptr<SkipBlockDevice> device_;
    std::optional<::llcpp::fuchsia::paver::Paver::SyncClient> client_;
    async::Loop loop_;
};


TEST_F(PaverServiceTest, QueryActiveConfiguration) {
    ::llcpp::fuchsia::paver::Paver_QueryActiveConfiguration_Result result;
    ASSERT_OK(client_->QueryActiveConfiguration_Deprecated(&result));
    ASSERT_TRUE(result.is_err());
    ASSERT_EQ(result.err(), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PaverServiceTest, SetActiveConfiguration) {
    zx_status_t status;
    auto configuration = ::llcpp::fuchsia::paver::Configuration::A;
    ASSERT_OK(client_->SetActiveConfiguration_Deprecated(configuration, &status));
    ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PaverServiceTest, MarkActiveConfigurationSuccessful) {
    zx_status_t status;
    ASSERT_OK(client_->MarkActiveConfigurationSuccessful_Deprecated(&status));
    ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PaverServiceTest, ForceRecoveryConfiguration) {
    zx_status_t status;
    ASSERT_OK(client_->ForceRecoveryConfiguration_Deprecated(&status));
    ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PaverServiceTest, WriteAssetKernelConfigA) {
    SpawnIsolatedDevmgr();
    ::llcpp::fuchsia::mem::Buffer payload;
    CreatePayload(2, &payload);
    zx_status_t status;
    ASSERT_OK(client_->WriteAsset_Deprecated(::llcpp::fuchsia::paver::Configuration::A,
                                  ::llcpp::fuchsia::paver::Asset::KERNEL, std::move(payload),
                                  &status));
    ASSERT_OK(status);
    ValidateWritten(8, 2);
    ValidateUnwritten(10, 4);
}

TEST_F(PaverServiceTest, WriteAssetKernelConfigB) {
    SpawnIsolatedDevmgr();
    ::llcpp::fuchsia::mem::Buffer payload;
    CreatePayload(2, &payload);
    zx_status_t status;
    ASSERT_OK(client_->WriteAsset_Deprecated(::llcpp::fuchsia::paver::Configuration::B,
                                  ::llcpp::fuchsia::paver::Asset::KERNEL, std::move(payload),
                                  &status));
    ASSERT_OK(status);
    ValidateUnwritten(8, 2);
    ValidateWritten(10, 2);
    ValidateUnwritten(12, 2);
}

TEST_F(PaverServiceTest, WriteAssetKernelConfigRecovery) {
    SpawnIsolatedDevmgr();
    ::llcpp::fuchsia::mem::Buffer payload;
    CreatePayload(2, &payload);
    zx_status_t status;
    ASSERT_OK(client_->WriteAsset_Deprecated(::llcpp::fuchsia::paver::Configuration::RECOVERY,
                                  ::llcpp::fuchsia::paver::Asset::KERNEL, std::move(payload),
                                  &status));
    ASSERT_OK(status);
    ValidateUnwritten(8, 4);
    ValidateWritten(12, 2);
}

TEST_F(PaverServiceTest, WriteAssetVbMetaConfigA) {
    SpawnIsolatedDevmgr();
    ::llcpp::fuchsia::mem::Buffer payload;
    CreatePayload(2, &payload);
    zx_status_t status;
    ASSERT_OK(client_->WriteAsset_Deprecated(::llcpp::fuchsia::paver::Configuration::A,
                                  ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA,
                                  std::move(payload), &status));
    ASSERT_OK(status);
    ValidateWritten(14, 2);
    ValidateUnwritten(16, 4);
}

TEST_F(PaverServiceTest, WriteAssetVbMetaConfigB) {
    SpawnIsolatedDevmgr();
    ::llcpp::fuchsia::mem::Buffer payload;
    CreatePayload(2, &payload);
    zx_status_t status;
    ASSERT_OK(client_->WriteAsset_Deprecated(::llcpp::fuchsia::paver::Configuration::B,
                                  ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA,
                                  std::move(payload), &status));
    ASSERT_OK(status);
    ValidateUnwritten(14, 2);
    ValidateWritten(16, 2);
    ValidateUnwritten(18, 2);
}

TEST_F(PaverServiceTest, WriteAssetVbMetaConfigRecovery) {
    SpawnIsolatedDevmgr();
    ::llcpp::fuchsia::mem::Buffer payload;
    CreatePayload(2, &payload);
    zx_status_t status;
    ASSERT_OK(client_->WriteAsset_Deprecated(::llcpp::fuchsia::paver::Configuration::RECOVERY,
                                  ::llcpp::fuchsia::paver::Asset::VERIFIED_BOOT_METADATA,
                                  std::move(payload), &status));
    ASSERT_OK(status);
    ValidateUnwritten(14, 4);
    ValidateWritten(18, 2);
}

TEST_F(PaverServiceTest, WriteAssetTwice) {
    SpawnIsolatedDevmgr();
    ::llcpp::fuchsia::mem::Buffer payload;
    CreatePayload(2, &payload);
    zx_status_t status;
    ASSERT_OK(client_->WriteAsset_Deprecated(::llcpp::fuchsia::paver::Configuration::A,
                                  ::llcpp::fuchsia::paver::Asset::KERNEL, std::move(payload),
                                  &status));
    ASSERT_OK(status);
    CreatePayload(2, &payload);
    ValidateWritten(8, 2);
    ValidateUnwritten(10, 4);
    ASSERT_OK(client_->WriteAsset_Deprecated(::llcpp::fuchsia::paver::Configuration::A,
                                  ::llcpp::fuchsia::paver::Asset::KERNEL, std::move(payload),
                                  &status));
    ASSERT_OK(status);
    ValidateWritten(8, 2);
    ValidateUnwritten(10, 4);
}

TEST_F(PaverServiceTest, WriteBootloader) {
    SpawnIsolatedDevmgr();
    ::llcpp::fuchsia::mem::Buffer payload;
    CreatePayload(4, &payload);
    zx_status_t status;
    ASSERT_OK(client_->WriteBootloader_Deprecated(std::move(payload), &status));
    ASSERT_OK(status);
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
    zx_status_t status;
    ASSERT_OK(client_->WipeVolumes_Deprecated(&status));
    ASSERT_OK(status);
}

} // namespace
