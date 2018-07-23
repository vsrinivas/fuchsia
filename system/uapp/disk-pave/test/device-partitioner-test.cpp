// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device-partitioner.h"

#include <dirent.h>
#include <fcntl.h>

#include <fbl/auto_call.h>
#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/string_piece.h>
#include <fbl/unique_ptr.h>
#include <fs-management/ram-nand.h>
#include <fs-management/ramdisk.h>
#include <lib/fzl/mapped-vmo.h>
#include <unittest/unittest.h>
#include <zircon/boot/image.h>
#include <zircon/device/device.h>
#include <zircon/hw/gpt.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

namespace {

constexpr uint8_t kZirconAType[GPT_GUID_LEN] = GUID_ZIRCON_A_VALUE;
constexpr uint8_t kZirconBType[GPT_GUID_LEN] = GUID_ZIRCON_B_VALUE;
constexpr uint8_t kZirconRType[GPT_GUID_LEN] = GUID_ZIRCON_R_VALUE;
constexpr uint8_t kFvmType[GPT_GUID_LEN] = GUID_FVM_VALUE;

constexpr uint64_t kBlockSize = 0x1000;
constexpr uint64_t kBlockCount = 0x10;

constexpr uint32_t kOobSize = 8;
constexpr uint32_t kPageSize = 1024;
constexpr uint32_t kPagesPerBlock = 16;
constexpr uint32_t kNumBlocks = 16;
constexpr ram_nand_info_t kNandInfo = {
    .vmo = ZX_HANDLE_INVALID,
    .nand_info = {
        .page_size = kPageSize,
        .pages_per_block = kPagesPerBlock,
        .num_blocks = kNumBlocks,
        .ecc_bits = 8,
        .oob_size = kOobSize,
        .nand_class = NAND_CLASS_PARTMAP,
        .partition_guid = {},
    },
    .export_nand_config = true,
    .export_partition_map = true,
    .bad_block_config = {
        .table_start_block = 0,
        .table_end_block = 3,
    },
    .extra_partition_config_count = 0,
    .extra_partition_config = {},
    .partition_map = {
        .block_count = kNumBlocks,
        .block_size = kPageSize * kPagesPerBlock,
        .partition_count = 4,
        .reserved = 0,
        .guid = {},
        .partitions = {
            {
                .type_guid = GUID_BOOTLOADER_VALUE,
                .uniq_guid = {},
                .first_block = 4,
                .last_block = 7,
                .flags = 0,
                .name = {'b', 'o', 'o', 't', 'l', 'o', 'a', 'd', 'e', 'r'},
            },
            {
                .type_guid = GUID_ZIRCON_A_VALUE,
                .uniq_guid = {},
                .first_block = 8,
                .last_block = 9,
                .flags = 0,
                .name = {'z', 'i', 'r', 'c', 'o', 'n', '-', 'a'},
            },
            {
                .type_guid = GUID_ZIRCON_B_VALUE,
                .uniq_guid = {},
                .first_block = 10,
                .last_block = 11,
                .flags = 0,
                .name = {'z', 'i', 'r', 'c', 'o', 'n', '-', 'b'},
            },
            {
                .type_guid = GUID_ZIRCON_R_VALUE,
                .uniq_guid = {},
                .first_block = 12,
                .last_block = 13,
                .flags = 0,
                .name = {'z', 'i', 'r', 'c', 'o', 'n', '-', 'r'},
            },
        },
    },
};


static fbl::Vector<fbl::String> test_block_devices;
static fbl::Vector<fbl::String> test_skip_block_devices;

bool FilterRealBlockDevices(const fbl::unique_fd& fd) {
    char topo_path[PATH_MAX] = { '\0' };
    if (ioctl_device_get_topo_path(fd.get(), topo_path, PATH_MAX) < 0) {
        return false;
    }
    for (const auto& device : test_block_devices) {
        if (strstr(topo_path, device.data()) == topo_path) {
            return false;
        }
    }
    return true;
}

bool FilterRealSkipBlockDevices(const fbl::unique_fd& fd) {
    char topo_path[PATH_MAX] = { '\0' };
    if (ioctl_device_get_topo_path(fd.get(), topo_path, PATH_MAX) < 0) {
        return false;
    }
    for (const auto& device : test_skip_block_devices) {
        if (strstr(topo_path, device.data()) == topo_path) {
            return false;
        }
    }
    return true;
}

bool Initialize() {
    test_block_devices.reset();
    test_skip_block_devices.reset();
    paver::TestBlockFilter = FilterRealBlockDevices;
    paver::TestSkipBlockFilter = FilterRealSkipBlockDevices;
    return true;
}

bool InsertTestDevices(fbl::StringPiece path, bool skip) {
    BEGIN_HELPER;
    fbl::unique_fd fd(open(path.data(), O_RDWR));
    ASSERT_TRUE(fd.is_valid());
    fbl::String topo_path(PATH_MAX, '\0');
    ASSERT_GE(ioctl_device_get_topo_path(fd.get(), const_cast<char*>(topo_path.data()), PATH_MAX),
              0);
    if (skip) {
        test_skip_block_devices.push_back(fbl::move(topo_path));
    } else {
        test_block_devices.push_back(fbl::move(topo_path));
    }
    END_HELPER;
}

class BlockDevice {
public:
    static bool Create(const uint8_t* guid, fbl::unique_ptr<BlockDevice>* device) {
        BEGIN_HELPER;
        fbl::String path(PATH_MAX, '\0');
        ASSERT_EQ(create_ramdisk_with_guid(kBlockSize, kBlockCount, guid, ZBI_PARTITION_GUID_LEN,
                                           const_cast<char*>(path.data())),
                  0);
        ASSERT_TRUE(InsertTestDevices(path.ToStringPiece(), false));
        device->reset(new BlockDevice(fbl::move(path)));
        END_HELPER;
    }

    ~BlockDevice() {
        destroy_ramdisk(path_.data());
    }

    fbl::StringPiece GetPath() {
        return path_.ToStringPiece();
    }

private:
    BlockDevice(fbl::String path)
        : path_(fbl::move(path)) {}

    fbl::String path_;
};

void CreateBadBlockMap(void* buffer) {
    // Set all entries in first BBT to be good blocks.
    constexpr uint8_t kBlockGood = 0;
    memset(buffer, kBlockGood, kPageSize);

    struct OobMetadata {
        uint32_t magic;
        int16_t program_erase_cycles;
        uint16_t generation;
    };

    const size_t oob_offset = kPageSize * kPagesPerBlock * kNumBlocks;
    auto* oob = reinterpret_cast<OobMetadata*>(reinterpret_cast<uintptr_t>(buffer) + oob_offset);
    oob->magic = 0x7462626E; // "nbbt"
    oob->program_erase_cycles = 0;
    oob->generation = 1;
}

class SkipBlockDevice {
public:
    static bool Create(fbl::unique_ptr<SkipBlockDevice>* device) {
        BEGIN_HELPER;
        fbl::unique_ptr<fzl::MappedVmo> mapped_vmo;
        ASSERT_EQ(fzl::MappedVmo::Create((kPageSize + kOobSize) * kPagesPerBlock * kNumBlocks,
                                         "Fake NAND Device", &mapped_vmo),
                  ZX_OK);
        memset(mapped_vmo->GetData(), 0xff, mapped_vmo->GetSize());
        CreateBadBlockMap(mapped_vmo->GetData());
        zx_vmo_op_range(mapped_vmo->GetVmo(), ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, 0,
                        mapped_vmo->GetSize(), nullptr, 0);
        zx_handle_t dup;
        ASSERT_EQ(zx_handle_duplicate(mapped_vmo->GetVmo(), ZX_RIGHT_SAME_RIGHTS, &dup), ZX_OK);

        fbl::String path(PATH_MAX, '\0');
        ram_nand_info_t info = kNandInfo;
        info.vmo = dup;
        ASSERT_EQ(create_ram_nand(&info, const_cast<char*>(path.data())), 0);
        ASSERT_TRUE(InsertTestDevices(path.ToStringPiece(), true));
        device->reset(new SkipBlockDevice(fbl::move(path), fbl::move(mapped_vmo)));
        END_HELPER;
    }

    ~SkipBlockDevice() {
        destroy_ram_nand(path_.data());
    }

    fbl::StringPiece GetPath() {
        return path_.ToStringPiece();
    }

private:
    SkipBlockDevice(fbl::String path, fbl::unique_ptr<fzl::MappedVmo> mapped_vmo)
        : path_(fbl::move(path)), mapped_vmo_(fbl::move(mapped_vmo)) {}

    fbl::String path_;
    fbl::unique_ptr<fzl::MappedVmo> mapped_vmo_;
};

} // namespace

namespace efi {
namespace {
bool UseBlockInterfaceTest() {
    BEGIN_TEST;
    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<BlockDevice> device;
    ASSERT_TRUE(BlockDevice::Create(kZirconAType, &device));

    END_TEST;
}

} // namespace
} // namespace efi

BEGIN_TEST_CASE(EfiDevicePartitionerTests)
RUN_TEST(efi::UseBlockInterfaceTest)
END_TEST_CASE(EfiDevicePartitionerTests);

namespace cros {
namespace {

bool UseBlockInterfaceTest() {
    BEGIN_TEST;

    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<BlockDevice> device;
    ASSERT_TRUE(BlockDevice::Create(kZirconAType, &device));

    END_TEST;
}

} // namespace
} // namespace cros

BEGIN_TEST_CASE(CrosDevicePartitionerTests)
RUN_TEST(cros::UseBlockInterfaceTest)
END_TEST_CASE(CrosDevicePartitionerTests);

namespace fixed {
namespace {

bool IsCrosTest() {
    BEGIN_TEST;

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::FixedDevicePartitioner::Initialize(&partitioner), ZX_OK);
    ASSERT_FALSE(partitioner->IsCros());

    END_TEST;
}

bool UseBlockInterfaceTest() {
    BEGIN_TEST;

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::FixedDevicePartitioner::Initialize(&partitioner), ZX_OK);
    ASSERT_FALSE(partitioner->UseSkipBlockInterface());

    END_TEST;
}

bool AddPartitionTest() {
    BEGIN_TEST;

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::FixedDevicePartitioner::Initialize(&partitioner), ZX_OK);
    ASSERT_EQ(partitioner->AddPartition(paver::Partition::kZirconB, nullptr), ZX_ERR_NOT_SUPPORTED);

    END_TEST;
}

bool WipePartitionsTest() {
    BEGIN_TEST;

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::FixedDevicePartitioner::Initialize(&partitioner), ZX_OK);
    ASSERT_EQ(partitioner->WipePartitions(fbl::Vector<paver::Partition>()), ZX_ERR_NOT_SUPPORTED);

    END_TEST;
}

bool FinalizePartitionTest() {
    BEGIN_TEST;

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::FixedDevicePartitioner::Initialize(&partitioner), ZX_OK);

    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kZirconA), ZX_OK);
    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kZirconB), ZX_OK);
    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kZirconR), ZX_OK);
    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kFuchsiaVolumeManager), ZX_OK);

    END_TEST;
}

bool FindPartitionTest() {
    BEGIN_TEST;

    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<BlockDevice> fvm, zircon_a, zircon_b, zircon_r;
    ASSERT_TRUE(BlockDevice::Create(kZirconAType, &zircon_a));
    ASSERT_TRUE(BlockDevice::Create(kZirconBType, &zircon_b));
    ASSERT_TRUE(BlockDevice::Create(kZirconRType, &zircon_r));
    ASSERT_TRUE(BlockDevice::Create(kFvmType, &fvm));

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::FixedDevicePartitioner::Initialize(&partitioner), ZX_OK);

    fbl::unique_fd fd;
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kZirconA, &fd), ZX_OK);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kZirconB, &fd), ZX_OK);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kZirconR, &fd), ZX_OK);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kFuchsiaVolumeManager, &fd), ZX_OK);

    END_TEST;
}

bool GetBlockSizeTest() {
    BEGIN_TEST;

    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<BlockDevice> fvm, zircon_a, zircon_b, zircon_r;
    ASSERT_TRUE(BlockDevice::Create(kZirconAType, &zircon_a));
    ASSERT_TRUE(BlockDevice::Create(kZirconBType, &zircon_b));
    ASSERT_TRUE(BlockDevice::Create(kZirconRType, &zircon_r));
    ASSERT_TRUE(BlockDevice::Create(kFvmType, &fvm));

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::FixedDevicePartitioner::Initialize(&partitioner), ZX_OK);

    fbl::unique_fd fd;
    uint32_t block_size;
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kZirconA, &fd), ZX_OK);
    ASSERT_EQ(partitioner->GetBlockSize(fd, &block_size), ZX_OK);
    ASSERT_EQ(block_size, kBlockSize);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kZirconB, &fd), ZX_OK);
    ASSERT_EQ(partitioner->GetBlockSize(fd, &block_size), ZX_OK);
    ASSERT_EQ(block_size, kBlockSize);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kZirconR, &fd), ZX_OK);
    ASSERT_EQ(partitioner->GetBlockSize(fd, &block_size), ZX_OK);
    ASSERT_EQ(block_size, kBlockSize);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kFuchsiaVolumeManager, &fd), ZX_OK);
    ASSERT_EQ(partitioner->GetBlockSize(fd, &block_size), ZX_OK);
    ASSERT_EQ(block_size, kBlockSize);

    END_TEST;
}

} // namespace
} // namespace fixed

BEGIN_TEST_CASE(FixedDevicePartitionerTests)
RUN_TEST(fixed::IsCrosTest)
RUN_TEST(fixed::UseBlockInterfaceTest)
RUN_TEST(fixed::AddPartitionTest)
RUN_TEST(fixed::WipePartitionsTest)
RUN_TEST(fixed::FinalizePartitionTest)
RUN_TEST(fixed::FindPartitionTest)
RUN_TEST(fixed::GetBlockSizeTest)
END_TEST_CASE(FixedDevicePartitionerTests);

namespace skipblock {
namespace {

bool IsCrosTest() {
    BEGIN_TEST;

    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<SkipBlockDevice> device;
    ASSERT_TRUE(SkipBlockDevice::Create(&device));

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::SkipBlockDevicePartitioner::Initialize(&partitioner), ZX_OK);
    ASSERT_FALSE(partitioner->IsCros());

    END_TEST;
}

bool UseSkipBlockInterfaceTest() {
    BEGIN_TEST;

    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<SkipBlockDevice> device;
    ASSERT_TRUE(SkipBlockDevice::Create(&device));

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::SkipBlockDevicePartitioner::Initialize(&partitioner), ZX_OK);
    ASSERT_TRUE(partitioner->UseSkipBlockInterface());

    END_TEST;
}

bool AddPartitionTest() {
    BEGIN_TEST;

    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<SkipBlockDevice> device;
    ASSERT_TRUE(SkipBlockDevice::Create(&device));

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::SkipBlockDevicePartitioner::Initialize(&partitioner), ZX_OK);
    ASSERT_EQ(partitioner->AddPartition(paver::Partition::kZirconB, nullptr), ZX_ERR_NOT_SUPPORTED);

    END_TEST;
}

bool WipePartitionsTest() {
    BEGIN_TEST;

    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<SkipBlockDevice> device;
    ASSERT_TRUE(SkipBlockDevice::Create(&device));

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::SkipBlockDevicePartitioner::Initialize(&partitioner), ZX_OK);
    ASSERT_EQ(partitioner->WipePartitions(fbl::Vector<paver::Partition>()), ZX_ERR_NOT_SUPPORTED);

    END_TEST;
}

bool FinalizePartitionTest() {
    BEGIN_TEST;

    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<SkipBlockDevice> device;
    ASSERT_TRUE(SkipBlockDevice::Create(&device));

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::SkipBlockDevicePartitioner::Initialize(&partitioner), ZX_OK);

    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kBootloader), ZX_OK);
    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kZirconA), ZX_OK);
    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kZirconB), ZX_OK);
    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kZirconR), ZX_OK);

    END_TEST;
}

bool FindPartitionTest() {
    BEGIN_TEST;

    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<SkipBlockDevice> device;
    ASSERT_TRUE(SkipBlockDevice::Create(&device));
    fbl::unique_ptr<BlockDevice> fvm;
    ASSERT_TRUE(BlockDevice::Create(kFvmType, &fvm));

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::SkipBlockDevicePartitioner::Initialize(&partitioner), ZX_OK);

    fbl::unique_fd fd;
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kBootloader, &fd), ZX_OK);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kZirconA, &fd), ZX_OK);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kZirconB, &fd), ZX_OK);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kZirconR, &fd), ZX_OK);

    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kFuchsiaVolumeManager, &fd), ZX_OK);

    END_TEST;
}

bool GetBlockSizeTest() {
    BEGIN_TEST;

    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<SkipBlockDevice> device;
    ASSERT_TRUE(SkipBlockDevice::Create(&device));
    fbl::unique_ptr<BlockDevice> fvm;
    ASSERT_TRUE(BlockDevice::Create(kFvmType, &fvm));

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::SkipBlockDevicePartitioner::Initialize(&partitioner), ZX_OK);

    fbl::unique_fd fd;
    uint32_t block_size;
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kBootloader, &fd), ZX_OK);
    ASSERT_EQ(partitioner->GetBlockSize(fd, &block_size), ZX_OK);
    ASSERT_EQ(block_size, kPageSize * kPagesPerBlock);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kZirconA, &fd), ZX_OK);
    ASSERT_EQ(partitioner->GetBlockSize(fd, &block_size), ZX_OK);
    ASSERT_EQ(block_size, kPageSize * kPagesPerBlock);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kZirconB, &fd), ZX_OK);
    ASSERT_EQ(partitioner->GetBlockSize(fd, &block_size), ZX_OK);
    ASSERT_EQ(block_size, kPageSize * kPagesPerBlock);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kZirconR, &fd), ZX_OK);
    ASSERT_EQ(partitioner->GetBlockSize(fd, &block_size), ZX_OK);
    ASSERT_EQ(block_size, kPageSize * kPagesPerBlock);

    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kFuchsiaVolumeManager, &fd), ZX_OK);
    ASSERT_EQ(partitioner->GetBlockSize(fd, &block_size), ZX_OK);
    ASSERT_EQ(block_size, kBlockSize);

    END_TEST;
}

} // namespace
} // namespace skipblock

BEGIN_TEST_CASE(SkipBlockDevicePartitionerTests)
RUN_TEST(skipblock::IsCrosTest)
RUN_TEST(skipblock::UseSkipBlockInterfaceTest)
RUN_TEST(skipblock::AddPartitionTest)
RUN_TEST(skipblock::WipePartitionsTest)
RUN_TEST(skipblock::FinalizePartitionTest)
RUN_TEST(skipblock::FindPartitionTest)
RUN_TEST(skipblock::GetBlockSizeTest)
END_TEST_CASE(SkipBlockDevicePartitionerTests);
