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
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/hardware/nand/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/unsafe.h>
#include <lib/fzl/vmo-mapper.h>
#include <ramdevice-client/ramdisk.h>
#include <ramdevice-client/ramnand.h>
#include <zircon/boot/image.h>
#include <zircon/hw/gpt.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <zxtest/zxtest.h>

#include <utility>

namespace {

constexpr uint8_t kZirconAType[GPT_GUID_LEN] = GUID_ZIRCON_A_VALUE;
constexpr uint8_t kZirconBType[GPT_GUID_LEN] = GUID_ZIRCON_B_VALUE;
constexpr uint8_t kZirconRType[GPT_GUID_LEN] = GUID_ZIRCON_R_VALUE;
constexpr uint8_t kVbMetaAType[GPT_GUID_LEN] = GUID_VBMETA_A_VALUE;
constexpr uint8_t kVbMetaBType[GPT_GUID_LEN] = GUID_VBMETA_B_VALUE;
constexpr uint8_t kFvmType[GPT_GUID_LEN] = GUID_FVM_VALUE;

constexpr uint64_t kBlockSize = 0x1000;
constexpr uint64_t kBlockCount = 0x10;

constexpr uint32_t kOobSize = 8;
constexpr uint32_t kPageSize = 1024;
constexpr uint32_t kPagesPerBlock = 16;
constexpr uint32_t kNumBlocks = 18;
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
                    .partition_count = 7,
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
                        },
                },
            .export_nand_config = true,
            .export_partition_map = true,
};

static fbl::Vector<fbl::String> test_block_devices;
static fbl::Vector<fbl::String> test_skip_block_devices;

bool FilterRealBlockDevices(const fbl::unique_fd& fd) {
    char topo_path[PATH_MAX] = {'\0'};

    fdio_t* io = fdio_unsafe_fd_to_io(fd.get());
    if (io == nullptr) {
        return false;
    }
    zx_status_t call_status;
    size_t path_len;
    zx_status_t status = fuchsia_device_ControllerGetTopologicalPath(
        fdio_unsafe_borrow_channel(io), &call_status, topo_path, sizeof(topo_path) - 1,
        &path_len);
    fdio_unsafe_release(io);
    if (status != ZX_OK || call_status != ZX_OK) {
        return false;
    }
    topo_path[path_len] = 0;

    for (const auto& device : test_block_devices) {
        if (strstr(topo_path, device.data()) == topo_path) {
            return false;
        }
    }
    return true;
}

bool Initialize() {
    test_block_devices.reset();
    paver::TestBlockFilter = FilterRealBlockDevices;
    return true;
}

void InsertTestDevices(fbl::StringPiece path) {
    zx::channel device, device_remote;
    ASSERT_EQ(zx::channel::create(0, &device, &device_remote), ZX_OK);
    ASSERT_EQ(fdio_service_connect(path.data(), device_remote.release()), ZX_OK);

    char topo_path[PATH_MAX] = {};
    zx_status_t call_status;
    size_t path_len;
    ASSERT_EQ(fuchsia_device_ControllerGetTopologicalPath(device.get(), &call_status, topo_path,
                                                          sizeof(topo_path) - 1, &path_len),
              ZX_OK);
    ASSERT_EQ(call_status, ZX_OK);
    topo_path[path_len] = 0;

    fbl::String topo_path_str(topo_path);
    test_block_devices.push_back(std::move(topo_path_str));
}

class BlockDevice {
public:
    static void Create(const uint8_t* guid, fbl::unique_ptr<BlockDevice>* device) {
        ramdisk_client_t* client;
        ASSERT_EQ(ramdisk_create_with_guid(kBlockSize, kBlockCount, guid, ZBI_PARTITION_GUID_LEN,
                                           &client),
                  ZX_OK);
        InsertTestDevices(ramdisk_get_path(client));
        device->reset(new BlockDevice(client));
    }

    ~BlockDevice() {
        ramdisk_destroy(client_);
    }

private:
    BlockDevice(ramdisk_client_t* client)
        : client_(client) {}

    ramdisk_client_t* client_;
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
    static void Create(fbl::unique_ptr<SkipBlockDevice>* device) {
        fzl::VmoMapper mapper;
        zx::vmo vmo;
        ASSERT_EQ(ZX_OK, mapper.CreateAndMap((kPageSize + kOobSize) * kPagesPerBlock * kNumBlocks,
                                             ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo));
        memset(mapper.start(), 0xff, mapper.size());
        CreateBadBlockMap(mapper.start());
        vmo.op_range(ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, 0, mapper.size(), nullptr, 0);
        zx::vmo dup;
        ASSERT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup), ZX_OK);

        fuchsia_hardware_nand_RamNandInfo info = kNandInfo;
        info.vmo = dup.release();
        fbl::RefPtr<ramdevice_client::RamNandCtl> ctl;
        ASSERT_EQ(ramdevice_client::RamNandCtl::Create(&ctl), ZX_OK);
        std::optional<ramdevice_client::RamNand> ram_nand;
        ASSERT_EQ(ramdevice_client::RamNand::Create(ctl, &info, &ram_nand), ZX_OK);
        device->reset(new SkipBlockDevice(std::move(ctl), *std::move(ram_nand),
                                          std::move(mapper)));
    }

    fbl::unique_fd devfs_root() { return fbl::unique_fd(dup(ctl_->devfs_root().get())); }

    ~SkipBlockDevice() = default;

private:
    SkipBlockDevice(fbl::RefPtr<ramdevice_client::RamNandCtl> ctl, ramdevice_client::RamNand ram_nand,
                    fzl::VmoMapper mapper)
        : ctl_(std::move(ctl)), ram_nand_(std::move(ram_nand)), mapper_(std::move(mapper)) {}

    fbl::RefPtr<ramdevice_client::RamNandCtl> ctl_;
    ramdevice_client::RamNand ram_nand_;
    fzl::VmoMapper mapper_;
};

} // namespace

TEST(EfiDevicePartitionerTests, UseBlockInterfaceTest) {
    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<BlockDevice> device;
    BlockDevice::Create(kZirconAType, &device);
}

TEST(CrosDevicePartitionerTests, UseBlockInterfaceTest) {
    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<BlockDevice> device;
    BlockDevice::Create(kZirconAType, &device);
}

TEST(FixedDevicePartitionerTests, UseBlockInterfaceTest) {
    fbl::unique_fd devfs(open("/dev", O_RDWR));
    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::FixedDevicePartitioner::Initialize(std::move(devfs), &partitioner), ZX_OK);
    ASSERT_FALSE(partitioner->UseSkipBlockInterface());
}

TEST(FixedDevicePartitionerTests, AddPartitionTest) {
    fbl::unique_fd devfs(open("/dev", O_RDWR));
    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::FixedDevicePartitioner::Initialize(std::move(devfs), &partitioner), ZX_OK);
    ASSERT_EQ(partitioner->AddPartition(paver::Partition::kZirconB, nullptr), ZX_ERR_NOT_SUPPORTED);
}

TEST(FixedDevicePartitionerTests, WipeFvmTest) {
    fbl::unique_fd devfs(open("/dev", O_RDWR));
    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::FixedDevicePartitioner::Initialize(std::move(devfs), &partitioner), ZX_OK);
    ASSERT_EQ(partitioner->WipeFvm(), ZX_OK);
}

TEST(FixedDevicePartitionerTests, FinalizePartitionTest) {
    fbl::unique_fd devfs(open("/dev", O_RDWR));
    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::FixedDevicePartitioner::Initialize(std::move(devfs), &partitioner), ZX_OK);

    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kZirconA), ZX_OK);
    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kZirconB), ZX_OK);
    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kZirconR), ZX_OK);
    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kVbMetaA), ZX_OK);
    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kVbMetaB), ZX_OK);
    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kFuchsiaVolumeManager), ZX_OK);
}

TEST(FixedDevicePartitionerTests, FindPartitionTest) {
    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<BlockDevice> fvm, zircon_a, zircon_b, zircon_r, vbmeta_a, vbmeta_b;
    BlockDevice::Create(kZirconAType, &zircon_a);
    BlockDevice::Create(kZirconBType, &zircon_b);
    BlockDevice::Create(kZirconRType, &zircon_r);
    BlockDevice::Create(kVbMetaAType, &vbmeta_a);
    BlockDevice::Create(kVbMetaBType, &vbmeta_b);
    BlockDevice::Create(kFvmType, &fvm);

    fbl::unique_fd devfs(open("/dev", O_RDWR));
    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::FixedDevicePartitioner::Initialize(std::move(devfs), &partitioner), ZX_OK);

    fbl::unique_fd fd;
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kZirconA, &fd), ZX_OK);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kZirconB, &fd), ZX_OK);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kZirconR, &fd), ZX_OK);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kVbMetaA, &fd), ZX_OK);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kVbMetaB, &fd), ZX_OK);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kFuchsiaVolumeManager, &fd), ZX_OK);
}

TEST(FixedDevicePartitionerTests, GetBlockSizeTest) {
    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<BlockDevice> fvm, zircon_a, zircon_b, zircon_r, vbmeta_a, vbmeta_b;
    BlockDevice::Create(kZirconAType, &zircon_a);
    BlockDevice::Create(kZirconBType, &zircon_b);
    BlockDevice::Create(kZirconRType, &zircon_r);
    BlockDevice::Create(kVbMetaAType, &vbmeta_a);
    BlockDevice::Create(kVbMetaBType, &vbmeta_b);
    BlockDevice::Create(kFvmType, &fvm);

    fbl::unique_fd devfs(open("/dev", O_RDWR));
    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::FixedDevicePartitioner::Initialize(std::move(devfs), &partitioner), ZX_OK);

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
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kVbMetaA, &fd), ZX_OK);
    ASSERT_EQ(partitioner->GetBlockSize(fd, &block_size), ZX_OK);
    ASSERT_EQ(block_size, kBlockSize);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kVbMetaB, &fd), ZX_OK);
    ASSERT_EQ(partitioner->GetBlockSize(fd, &block_size), ZX_OK);
    ASSERT_EQ(block_size, kBlockSize);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kFuchsiaVolumeManager, &fd), ZX_OK);
    ASSERT_EQ(partitioner->GetBlockSize(fd, &block_size), ZX_OK);
    ASSERT_EQ(block_size, kBlockSize);
}

TEST(SkipBlockDevicePartitionerTests, UseSkipBlockInterfaceTest) {
    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<SkipBlockDevice> device;
    SkipBlockDevice::Create(&device);

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::SkipBlockDevicePartitioner::Initialize(device->devfs_root(), &partitioner),
              ZX_OK);
    ASSERT_TRUE(partitioner->UseSkipBlockInterface());
}

TEST(SkipBlockDevicePartitionerTests, AddPartitionTest) {
    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<SkipBlockDevice> device;
    SkipBlockDevice::Create(&device);

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::SkipBlockDevicePartitioner::Initialize(device->devfs_root(), &partitioner),
              ZX_OK);
    ASSERT_EQ(partitioner->AddPartition(paver::Partition::kZirconB, nullptr), ZX_ERR_NOT_SUPPORTED);
}

TEST(SkipBlockDevicePartitionerTests, WipeFvmTest) {
    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<SkipBlockDevice> device;
    SkipBlockDevice::Create(&device);

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::SkipBlockDevicePartitioner::Initialize(device->devfs_root(), &partitioner),
              ZX_OK);
    ASSERT_EQ(partitioner->WipeFvm(), ZX_OK);
}

TEST(SkipBlockDevicePartitionerTests, FinalizePartitionTest) {
    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<SkipBlockDevice> device;
    SkipBlockDevice::Create(&device);

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::SkipBlockDevicePartitioner::Initialize(device->devfs_root(), &partitioner),
              ZX_OK);

    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kBootloader), ZX_OK);
    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kZirconA), ZX_OK);
    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kZirconB), ZX_OK);
    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kZirconR), ZX_OK);
    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kVbMetaA), ZX_OK);
    ASSERT_EQ(partitioner->FinalizePartition(paver::Partition::kVbMetaB), ZX_OK);
}

TEST(SkipBlockDevicePartitionerTests, FindPartitionTest) {
    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<SkipBlockDevice> device;
    SkipBlockDevice::Create(&device);
    fbl::unique_ptr<BlockDevice> fvm;
    BlockDevice::Create(kFvmType, &fvm);

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::SkipBlockDevicePartitioner::Initialize(device->devfs_root(), &partitioner),
              ZX_OK);

    fbl::unique_fd fd;
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kBootloader, &fd), ZX_OK);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kZirconA, &fd), ZX_OK);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kZirconB, &fd), ZX_OK);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kZirconR, &fd), ZX_OK);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kVbMetaA, &fd), ZX_OK);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kVbMetaB, &fd), ZX_OK);

    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kFuchsiaVolumeManager, &fd), ZX_OK);
}

TEST(SkipBlockDevicePartitionerTests, GetBlockSizeTest) {
    ASSERT_TRUE(Initialize());
    fbl::unique_ptr<SkipBlockDevice> device;
    SkipBlockDevice::Create(&device);
    fbl::unique_ptr<BlockDevice> fvm;
    BlockDevice::Create(kFvmType, &fvm);

    fbl::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_EQ(paver::SkipBlockDevicePartitioner::Initialize(device->devfs_root(), &partitioner),
              ZX_OK);

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
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kVbMetaA, &fd), ZX_OK);
    ASSERT_EQ(partitioner->GetBlockSize(fd, &block_size), ZX_OK);
    ASSERT_EQ(block_size, kPageSize * kPagesPerBlock);
    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kVbMetaB, &fd), ZX_OK);
    ASSERT_EQ(partitioner->GetBlockSize(fd, &block_size), ZX_OK);
    ASSERT_EQ(block_size, kPageSize * kPagesPerBlock);

    ASSERT_EQ(partitioner->FindPartition(paver::Partition::kFuchsiaVolumeManager, &fd), ZX_OK);
    ASSERT_EQ(partitioner->GetBlockSize(fd, &block_size), ZX_OK);
    ASSERT_EQ(block_size, kBlockSize);
}
