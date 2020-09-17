// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/device-partitioner.h"

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <fuchsia/hardware/nand/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <zircon/boot/image.h>
#include <zircon/errors.h>
#include <zircon/hw/gpt.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <array>
#include <memory>
#include <string_view>
#include <utility>

#include <fbl/auto_call.h>
#include <fbl/span.h>
#include <gpt/cros.h>
#include <gpt/gpt.h>
#include <soc/aml-common/aml-guid.h>
#include <zxtest/zxtest.h>

#include "src/storage/lib/paver/as370.h"
#include "src/storage/lib/paver/astro.h"
#include "src/storage/lib/paver/chromebook-x64.h"
#include "src/storage/lib/paver/luis.h"
#include "src/storage/lib/paver/sherlock.h"
#include "src/storage/lib/paver/test/test-utils.h"
#include "src/storage/lib/paver/utils.h"
#include "src/storage/lib/paver/x64.h"

namespace paver {
extern zx_duration_t g_wipe_timeout;
}

namespace {

constexpr uint64_t kMebibyte = 1024 * 1024;
constexpr uint64_t kGibibyte = kMebibyte * 1024;
constexpr uint64_t kTebibyte = kGibibyte * 1024;

using devmgr_integration_test::RecursiveWaitForFile;
using driver_integration_test::IsolatedDevmgr;
using paver::BlockWatcherPauser;
using paver::PartitionSpec;

constexpr uint8_t kBootloaderType[GPT_GUID_LEN] = GUID_BOOTLOADER_VALUE;
constexpr uint8_t kEfiType[GPT_GUID_LEN] = GUID_EFI_VALUE;
constexpr uint8_t kCrosKernelType[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
constexpr uint8_t kCrosRootfsType[GPT_GUID_LEN] = GUID_CROS_ROOTFS_VALUE;
constexpr uint8_t kZirconAType[GPT_GUID_LEN] = GUID_ZIRCON_A_VALUE;
constexpr uint8_t kZirconBType[GPT_GUID_LEN] = GUID_ZIRCON_B_VALUE;
constexpr uint8_t kZirconRType[GPT_GUID_LEN] = GUID_ZIRCON_R_VALUE;
constexpr uint8_t kVbMetaAType[GPT_GUID_LEN] = GUID_VBMETA_A_VALUE;
constexpr uint8_t kVbMetaBType[GPT_GUID_LEN] = GUID_VBMETA_B_VALUE;
constexpr uint8_t kVbMetaRType[GPT_GUID_LEN] = GUID_VBMETA_R_VALUE;
constexpr uint8_t kFvmType[GPT_GUID_LEN] = GUID_FVM_VALUE;
constexpr uint8_t kEmptyType[GPT_GUID_LEN] = GUID_EMPTY_VALUE;
constexpr uint8_t kSysConfigType[GPT_GUID_LEN] = GUID_SYS_CONFIG_VALUE;
constexpr uint8_t kAbrMetaType[GPT_GUID_LEN] = GUID_ABR_META_VALUE;
// constexpr uint8_t kStateCrosGuid[GPT_GUID_LEN] = GUID_CROS_STATE_VALUE;
constexpr uint8_t kStateLinuxGuid[GPT_GUID_LEN] = GUID_LINUX_FILESYSTEM_DATA_VALUE;

constexpr uint8_t kBoot0Type[GPT_GUID_LEN] = GUID_EMMC_BOOT1_VALUE;
constexpr uint8_t kBoot1Type[GPT_GUID_LEN] = GUID_EMMC_BOOT2_VALUE;

constexpr uint8_t kDummyType[GPT_GUID_LEN] = {0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47,
                                              0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4};

const fbl::unique_fd kDummyDevice;

constexpr fuchsia_hardware_nand_RamNandInfo kNandInfo = {
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
                    {
                        .type_guid = GUID_BL2_VALUE,
                        .unique_guid = {},
                        .first_block = 18,
                        .last_block = 22,
                        .copy_count = 0,
                        .copy_byte_offset = 0,
                        .name =
                            {
                                'b',
                                'l',
                                '2',
                            },
                        .hidden = false,
                        .bbt = false,
                    },
                },
        },
    .export_nand_config = true,
    .export_partition_map = true,
};

// Returns the start address of the given partition in |mapper|, or nullptr if
// the partition doesn't exist in |nand_info|.
uint8_t* PartitionStart(const fzl::VmoMapper& mapper,
                        const fuchsia_hardware_nand_RamNandInfo& nand_info,
                        const std::array<uint8_t, GPT_GUID_LEN> guid) {
  const auto& map = nand_info.partition_map;
  const auto* partitions_begin = map.partitions;
  const auto* partitions_end = &map.partitions[map.partition_count];

  const auto* part = std::find_if(partitions_begin, partitions_end,
                                  [&guid](const fuchsia_hardware_nand_Partition& p) {
                                    return memcmp(p.type_guid, guid.data(), guid.size()) == 0;
                                  });
  if (part == partitions_end) {
    return nullptr;
  }

  return reinterpret_cast<uint8_t*>(mapper.start()) +
         (part->first_block * kPageSize * kPagesPerBlock);
}

struct PartitionDescription {
  const char* name;
  const uint8_t* type;
  uint64_t start;
  uint64_t length;
};

uint8_t* GetRandomGuid() {
  static uint8_t random_guid[GPT_GUID_LEN];
  zx_cprng_draw(random_guid, GPT_GUID_LEN);
  return random_guid;
}

void utf16_to_cstring(char* dst, const uint8_t* src, size_t charcount) {
  while (charcount > 0) {
    *dst++ = *src;
    src += 2;
    charcount -= 2;
  }
}

// Find a partition with the given label.
//
// Returns nullptr if no partitions exist, or multiple partitions exist with
// the same label.
//
// Note: some care must be used with this function: the UEFI standard makes no guarantee
// that a GPT won't contain two partitions with the same label; for test data, using
// label names is convenient, however.
gpt_partition_t* FindPartitionWithLabel(const gpt::GptDevice* gpt, std::string_view name) {
  gpt_partition_t* result = nullptr;

  for (uint32_t i = 0; i < gpt->EntryCount(); i++) {
    auto* gpt_part = gpt->GetPartition(i);
    if (gpt_part == nullptr) {
      continue;
    }

    // Convert UTF-16 partition label to ASCII.
    char cstring_name[GPT_NAME_LEN + 1] = {};
    utf16_to_cstring(cstring_name, gpt_part->name, GPT_NAME_LEN);
    cstring_name[GPT_NAME_LEN] = 0;
    auto partition_name = std::string_view(cstring_name, strlen(cstring_name));

    // Ignore partitions with the incorrect name.
    if (partition_name != name) {
      continue;
    }

    // If we have already found a partition with the label, we've discovered
    // multiple partitions with the same label. Return nullptr.
    if (result != nullptr) {
      printf("Found multiple partitions with label '%s'.\n", std::string(name).c_str());
      return nullptr;
    }

    result = gpt_part;
  }

  return result;
}

// Ensure that the partitions on the device matches the given list.
void EnsurePartitionsMatch(const gpt::GptDevice* gpt,
                           fbl::Span<const PartitionDescription> expected) {
  for (auto& part : expected) {
    gpt_partition_t* gpt_part = FindPartitionWithLabel(gpt, part.name);
    ASSERT_TRUE(gpt_part != nullptr, "Partition \"%s\" not found", part.name);
    EXPECT_TRUE(memcmp(part.type, gpt_part->type, GPT_GUID_LEN) == 0);
    EXPECT_EQ(part.start, gpt_part->first);
    EXPECT_EQ(part.start + part.length - 1, gpt_part->last);
  }
}

constexpr paver::Partition kUnknownPartition = static_cast<paver::Partition>(1000);

TEST(PartitionName, Bootloader) {
  EXPECT_STR_EQ(PartitionName(paver::Partition::kBootloaderA, paver::PartitionScheme::kNew),
                GPT_BOOTLOADER_A_NAME);
  EXPECT_STR_EQ(PartitionName(paver::Partition::kBootloaderB, paver::PartitionScheme::kNew),
                GPT_BOOTLOADER_B_NAME);
  EXPECT_STR_EQ(PartitionName(paver::Partition::kBootloaderR, paver::PartitionScheme::kNew),
                GPT_BOOTLOADER_R_NAME);
  EXPECT_STR_EQ(PartitionName(paver::Partition::kBootloaderA, paver::PartitionScheme::kLegacy),
                GUID_EFI_NAME);
  EXPECT_STR_EQ(PartitionName(paver::Partition::kBootloaderB, paver::PartitionScheme::kLegacy),
                GUID_EFI_NAME);
  EXPECT_STR_EQ(PartitionName(paver::Partition::kBootloaderR, paver::PartitionScheme::kLegacy),
                GUID_EFI_NAME);
}

TEST(PartitionName, AbrMetadata) {
  EXPECT_STR_EQ(PartitionName(paver::Partition::kAbrMeta, paver::PartitionScheme::kNew),
                GPT_DURABLE_BOOT_NAME);
  EXPECT_STR_EQ(PartitionName(paver::Partition::kAbrMeta, paver::PartitionScheme::kLegacy),
                GUID_ABR_META_NAME);
}

TEST(PartitionName, UnknownPartition) {
  // We don't define what is returned in this case, but it shouldn't crash and
  // it should be non-empty.
  EXPECT_STR_NE(PartitionName(kUnknownPartition, paver::PartitionScheme::kNew), "");
  EXPECT_STR_NE(PartitionName(kUnknownPartition, paver::PartitionScheme::kLegacy), "");
}

TEST(PartitionSpec, ToStringDefaultContentType) {
  // This is a bit of a change-detector test since we don't actually care about
  // the string value, but it's the cleanest way to check that the string is
  // 1) non-empty and 2) doesn't contain a type suffix.
  EXPECT_EQ(PartitionSpec(paver::Partition::kZirconA).ToString(), "Zircon A");
  EXPECT_EQ(PartitionSpec(paver::Partition::kVbMetaB).ToString(), "VBMeta B");
}

TEST(PartitionSpec, ToStringWithContentType) {
  EXPECT_EQ(PartitionSpec(paver::Partition::kZirconA, "foo").ToString(), "Zircon A (foo)");
  EXPECT_EQ(PartitionSpec(paver::Partition::kVbMetaB, "a b c").ToString(), "VBMeta B (a b c)");
}

TEST(PartitionSpec, ToStringUnknownPartition) {
  EXPECT_NE(PartitionSpec(kUnknownPartition).ToString(), "");
  EXPECT_NE(PartitionSpec(kUnknownPartition, "foo").ToString(), "");
}

class GptDevicePartitionerTests : public zxtest::Test {
 protected:
  GptDevicePartitionerTests(fbl::String board_name = fbl::String(), uint32_t block_size = 512)
      : block_size_(block_size) {
    paver::g_wipe_timeout = 0;
    IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");
    args.disable_block_watcher = false;
    args.path_prefix = "/pkg/";
    args.board_name = board_name;
    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));

    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "misc/ramctl", &fd));
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform", &fd));
  }

  zx::channel GetSvcRoot() {
    const zx::channel& fshost_root = devmgr_.fshost_outgoing_dir();

    zx::channel local, remote;
    auto status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
      return zx::channel();
    }
    fdio_service_connect_at(fshost_root.get(), "svc", remote.release());
    return local;
  }

  // Create a disk with the default size for a BlockDevice.
  void CreateDisk(std::unique_ptr<BlockDevice>* disk) {
    ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, disk));
  }

  // Create a disk with the given size in bytes.
  void CreateDisk(uint64_t bytes, std::unique_ptr<BlockDevice>* disk) {
    ASSERT_TRUE(bytes % block_size_ == 0);
    uint64_t num_blocks = bytes / block_size_;

    ASSERT_NO_FATAL_FAILURES(
        BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, num_blocks, block_size_, disk));
  }

  // Create a disk with the given size in bytes and the given type.
  void CreateDisk(uint64_t bytes, const uint8_t* type, std::unique_ptr<BlockDevice>* disk) {
    ASSERT_TRUE(bytes % block_size_ == 0);
    uint64_t num_blocks = bytes / block_size_;

    ASSERT_NO_FATAL_FAILURES(
        BlockDevice::Create(devmgr_.devfs_root(), type, num_blocks, block_size_, disk));
  }

  // Create a disk with a given size, and allocate some extra room for the GPT
  void CreateDiskWithGpt(uint64_t bytes, std::unique_ptr<BlockDevice>* disk) {
    ASSERT_TRUE(bytes % block_size_ == 0);
    uint64_t num_blocks = bytes / block_size_;

    // Ensure there's always enough space for the GPT.
    num_blocks += kGptBlockCount;

    ASSERT_NO_FATAL_FAILURES(
        BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, num_blocks, block_size_, disk));
  }

  // Create GPT from a device.
  void CreateGptDevice(BlockDevice* device, std::unique_ptr<gpt::GptDevice>* gpt) {
    ASSERT_OK(gpt::GptDevice::Create(device->fd(), /*block_size=*/device->block_size(),
                                     /*blocks=*/device->block_count(), gpt));
    ASSERT_OK((*gpt)->Sync());
  }

  IsolatedDevmgr devmgr_;
  const uint32_t block_size_;
};

TEST_F(GptDevicePartitionerTests, AddPartitionAtLargeOffset) {
  std::unique_ptr<BlockDevice> gpt_dev;
  // Create 2TB disk
  ASSERT_NO_FATAL_FAILURES(CreateDisk(2 * kTebibyte, &gpt_dev));

  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURES(CreateGptDevice(gpt_dev.get(), &gpt));

    // Add a dummy partition of large size (~1.9TB)
    ASSERT_OK(
        gpt->AddPartition("dummy-partition", kEfiType, GetRandomGuid(), 0x1000, 0xF0000000, 0),
        "%s", "dummy-partition");

    ASSERT_OK(gpt->Sync());
  }

  // Iniitialize paver gpt device partitioner
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));
  auto status = paver::GptDevicePartitioner::InitializeGpt(devmgr_.devfs_root().duplicate(),
                                                           GetSvcRoot(), std::move(gpt_fd));
  ASSERT_OK(status);

  // Check if a partition can be added after the "dummy-partition"
  ASSERT_OK(status->gpt->AddPartition("test", uuid::Uuid(GUID_FVM_VALUE), 15LU * kGibibyte, 0));
}

class EfiDevicePartitionerTests : public GptDevicePartitionerTests {
 protected:
  EfiDevicePartitionerTests() : GptDevicePartitionerTests(fbl::String(), 512) {}

  // Create a DevicePartition for a device.
  zx::status<std::unique_ptr<paver::DevicePartitioner>> CreatePartitioner(
      const fbl::unique_fd& device) {
    zx::channel svc_root = GetSvcRoot();
    return paver::EfiDevicePartitioner::Initialize(devmgr_.devfs_root().duplicate(),
                                                   std::move(svc_root), paver::Arch::kX64,
                                                   std::move(device));
  }
};

TEST_F(EfiDevicePartitionerTests, InitializeWithoutGptFails) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(&gpt_dev));

  ASSERT_NOT_OK(CreatePartitioner(kDummyDevice));
}

TEST_F(EfiDevicePartitionerTests, InitializeWithoutFvmSucceeds) {
  std::unique_ptr<BlockDevice> gpt_dev;
  // 32GiB disk.
  constexpr uint64_t kBlockCount = (32LU << 30) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kBlockCount, &gpt_dev));

  // Set up a valid GPT.
  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_OK(gpt::GptDevice::Create(gpt_dev->fd(), kBlockSize, kBlockCount, &gpt));
  ASSERT_OK(gpt->Sync());

  ASSERT_OK(CreatePartitioner(kDummyDevice));
}

TEST_F(EfiDevicePartitionerTests, InitializeTwoCandidatesWithoutFvmFails) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(&gpt_dev));

  // Set up a valid GPT.
  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_NO_FATAL_FAILURES(CreateGptDevice(gpt_dev.get(), &gpt));

  std::unique_ptr<BlockDevice> gpt_dev2;
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, &gpt_dev2));

  // Set up a valid GPT.
  std::unique_ptr<gpt::GptDevice> gpt2;
  ASSERT_OK(gpt::GptDevice::Create(gpt_dev->fd(), kBlockSize, kBlockCount, &gpt2));
  ASSERT_OK(gpt2->Sync());

  ASSERT_NOT_OK(CreatePartitioner(kDummyDevice));
}

TEST_F(EfiDevicePartitionerTests, AddPartitionZirconB) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDiskWithGpt(128 * kMebibyte, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  auto status = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status);

  ASSERT_OK(status->AddPartition(PartitionSpec(paver::Partition::kZirconB)));
}

TEST_F(EfiDevicePartitionerTests, AddPartitionFvm) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDiskWithGpt(16 * kGibibyte, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  auto status = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status);

  ASSERT_OK(status->AddPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
}

TEST_F(EfiDevicePartitionerTests, AddPartitionTooSmall) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(&gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  auto status = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status);

  ASSERT_NOT_OK(status->AddPartition(PartitionSpec(paver::Partition::kZirconB)));
}

TEST_F(EfiDevicePartitionerTests, AddedPartitionIsFindable) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDiskWithGpt(128 * kMebibyte, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  auto status = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status);

  ASSERT_OK(status->AddPartition(PartitionSpec(paver::Partition::kZirconB)));
  ASSERT_OK(status->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
  ASSERT_NOT_OK(status->FindPartition(PartitionSpec(paver::Partition::kZirconA)));
}

TEST_F(EfiDevicePartitionerTests, InitializePartitionsWithoutExplicitDevice) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDiskWithGpt(16 * kGibibyte, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  auto status = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status);

  ASSERT_OK(status->AddPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
  status.value().reset();

  // Note that this time we don't pass in a block device fd.
  ASSERT_OK(CreatePartitioner(kDummyDevice));
}

TEST_F(EfiDevicePartitionerTests, InitializeWithMultipleCandidateGPTsFailsWithoutExplicitDevice) {
  std::unique_ptr<BlockDevice> gpt_dev1, gpt_dev2;
  ASSERT_NO_FATAL_FAILURES(CreateDiskWithGpt(16 * kGibibyte, &gpt_dev1));
  fbl::unique_fd gpt_fd(dup(gpt_dev1->fd()));

  auto status = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status);

  ASSERT_OK(status->AddPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
  status.value().reset();

  ASSERT_NO_FATAL_FAILURES(CreateDiskWithGpt(16 * kGibibyte, &gpt_dev2));
  gpt_fd.reset(dup(gpt_dev2->fd()));

  auto status2 = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status2);
  ASSERT_OK(status2->AddPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
  status2.value().reset();

  // Note that this time we don't pass in a block device fd.
  ASSERT_NOT_OK(CreatePartitioner(kDummyDevice));
}

TEST_F(EfiDevicePartitionerTests, InitializeWithTwoCandidateGPTsSucceedsAfterWipingOne) {
  std::unique_ptr<BlockDevice> gpt_dev1, gpt_dev2;
  ASSERT_NO_FATAL_FAILURES(CreateDiskWithGpt(16 * kGibibyte, &gpt_dev1));
  fbl::unique_fd gpt_fd(dup(gpt_dev1->fd()));

  auto status = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status);

  ASSERT_OK(status->AddPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
  status.value().reset();

  ASSERT_NO_FATAL_FAILURES(CreateDiskWithGpt(16 * kGibibyte, &gpt_dev2));
  gpt_fd.reset(dup(gpt_dev2->fd()));

  auto status2 = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status2);
  ASSERT_OK(status2->AddPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
  ASSERT_OK(status2->WipeFvm());
  status2.value().reset();

  // Note that this time we don't pass in a block device fd.
  ASSERT_OK(CreatePartitioner(kDummyDevice));
}

TEST_F(EfiDevicePartitionerTests, AddedPartitionRemovedAfterWipePartitions) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDiskWithGpt(128 * kMebibyte, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  auto status = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status);

  ASSERT_OK(status->AddPartition(PartitionSpec(paver::Partition::kZirconB)));
  ASSERT_OK(status->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
  ASSERT_OK(status->WipePartitionTables());
  ASSERT_NOT_OK(status->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
}

TEST_F(EfiDevicePartitionerTests, FindOldBootloaderPartitionName) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(32 * kGibibyte, &gpt_dev));

  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURES(CreateGptDevice(gpt_dev.get(), &gpt));

    ASSERT_OK(gpt->AddPartition("efi-system", kEfiType, GetRandomGuid(), 0x22, 0x8000, 0));
    ASSERT_OK(gpt->Sync());
  }

  fdio_cpp::UnownedFdioCaller caller(gpt_dev->fd());
  auto result = ::llcpp::fuchsia::device::Controller::Call::Rebind(
      caller.channel(), fidl::StringView("/boot/driver/gpt.so"));
  ASSERT_TRUE(result.ok());
  ASSERT_FALSE(result->result.is_err());

  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));
  auto partitioner = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(partitioner);
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA)));
}

TEST_F(EfiDevicePartitionerTests, InitPartitionTables) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(32 * kGibibyte, &gpt_dev));

  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURES(CreateGptDevice(gpt_dev.get(), &gpt));

    // Write initial partitions to disk.
    const std::array<PartitionDescription, 11> partitions_at_start{
        PartitionDescription{"efi", kEfiType, 0x22, 0x1},
        PartitionDescription{"efi-system", kEfiType, 0x23, 0x8000},
        PartitionDescription{GUID_EFI_NAME, kEfiType, 0x8023, 0x8000},
        PartitionDescription{"ZIRCON-A", kZirconAType, 0x10023, 0x1},
        PartitionDescription{"zircon_b", kZirconBType, 0x10024, 0x1},
        PartitionDescription{"zircon r", kZirconRType, 0x10025, 0x1},
        PartitionDescription{"vbmeta-a", kVbMetaAType, 0x10026, 0x1},
        PartitionDescription{"VBMETA_B", kVbMetaBType, 0x10027, 0x1},
        PartitionDescription{"VBMETA R", kVbMetaRType, 0x10028, 0x1},
        PartitionDescription{"abrmeta", kAbrMetaType, 0x10029, 0x1},
        PartitionDescription{"FVM", kFvmType, 0x10030, 0x1},
    };
    for (auto& part : partitions_at_start) {
      ASSERT_OK(
          gpt->AddPartition(part.name, part.type, GetRandomGuid(), part.start, part.length, 0),
          "%s", part.name);
    }
    ASSERT_OK(gpt->Sync());
  }

  // Create EFI device partitioner and initialise partition tables.
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));
  auto status = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();
  ASSERT_OK(partitioner->InitPartitionTables());

  // Ensure the final partition layout looks like we expect it to.
  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_NO_FATAL_FAILURES(CreateGptDevice(gpt_dev.get(), &gpt));
  const std::array<PartitionDescription, 10> partitions_at_end{
      PartitionDescription{"efi", kEfiType, 0x22, 0x1},
      PartitionDescription{GUID_EFI_NAME, kEfiType, 0x23, 0x8000},
      PartitionDescription{GUID_ZIRCON_A_NAME, kZirconAType, 0x8023, 0x40000},
      PartitionDescription{GUID_ZIRCON_B_NAME, kZirconBType, 0x48023, 0x40000},
      PartitionDescription{GUID_ZIRCON_R_NAME, kZirconRType, 0x88023, 0x60000},
      PartitionDescription{GUID_VBMETA_A_NAME, kVbMetaAType, 0xe8023, 0x80},
      PartitionDescription{GUID_VBMETA_B_NAME, kVbMetaBType, 0xe80a3, 0x80},
      PartitionDescription{GUID_VBMETA_R_NAME, kVbMetaRType, 0xe8123, 0x80},
      PartitionDescription{GUID_ABR_META_NAME, kAbrMetaType, 0xe81a3, 0x8},
      PartitionDescription{GUID_FVM_NAME, kFvmType, 0xe81ab, 0x2000000},
  };
  ASSERT_NO_FATAL_FAILURES(EnsurePartitionsMatch(gpt.get(), partitions_at_end));

  // Make sure we can find the important partitions.
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kAbrMeta)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
  // Check that we found the correct bootloader partition.
  auto status2 = partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA));
  EXPECT_OK(status2);

  auto status3 = status2->GetPartitionSize();
  EXPECT_OK(status3);
  EXPECT_EQ(status3.value(), 0x8000 * block_size_);
}

TEST_F(EfiDevicePartitionerTests, SupportsPartition) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(1 * kGibibyte, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  auto status = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloaderA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kAbrMeta)));
  EXPECT_TRUE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));

  // Unsupported partition type.
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kUnknown)));

  // Unsupported content type.
  EXPECT_FALSE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconA, "foo_type")));
}

TEST_F(EfiDevicePartitionerTests, ValidatePayload) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(1 * kGibibyte, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  auto status = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  // Test invalid partitions.
  ASSERT_NOT_OK(partitioner->ValidatePayload(PartitionSpec(paver::Partition::kZirconA),
                                             fbl::Span<uint8_t>()));
  ASSERT_NOT_OK(partitioner->ValidatePayload(PartitionSpec(paver::Partition::kZirconB),
                                             fbl::Span<uint8_t>()));
  ASSERT_NOT_OK(partitioner->ValidatePayload(PartitionSpec(paver::Partition::kZirconR),
                                             fbl::Span<uint8_t>()));

  // Non-kernel partitions are not validated.
  ASSERT_OK(partitioner->ValidatePayload(PartitionSpec(paver::Partition::kAbrMeta),
                                         fbl::Span<uint8_t>()));
}

class CrosDevicePartitionerTests : public GptDevicePartitionerTests {
 protected:
  CrosDevicePartitionerTests() : GptDevicePartitionerTests(fbl::String(), 512) {}

  // Create a DevicePartition for a device.
  void CreatePartitioner(BlockDevice* device,
                         std::unique_ptr<paver::DevicePartitioner>* partitioner) {
    zx::channel svc_root = GetSvcRoot();
    auto status = paver::CrosDevicePartitioner::Initialize(devmgr_.devfs_root().duplicate(),
                                                           std::move(svc_root), paver::Arch::kX64,
                                                           fbl::unique_fd{dup(device->fd())});
    ASSERT_OK(status);
    *partitioner = std::move(status.value());
  }
};

TEST_F(CrosDevicePartitionerTests, InitPartitionTables) {
  std::unique_ptr<BlockDevice> disk;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(32 * kGibibyte, &disk));

  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    // Write initial partitions to disk.
    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURES(CreateGptDevice(disk.get(), &gpt));
    const std::array<PartitionDescription, 5> partitions_at_start{
        PartitionDescription{"SYSCFG", kSysConfigType, 0x22, 0x800},
        PartitionDescription{"ZIRCON-A", kCrosKernelType, 0x822, 0x20000},
        PartitionDescription{"ZIRCON-B", kCrosKernelType, 0x20822, 0x20000},
        PartitionDescription{"ZIRCON-R", kCrosKernelType, 0x40822, 0x20000},
        PartitionDescription{"fvm", kFvmType, 0x60822, 0x1000000},
    };
    for (auto& part : partitions_at_start) {
      ASSERT_OK(
          gpt->AddPartition(part.name, part.type, GetRandomGuid(), part.start, part.length, 0),
          "%s", part.name);
    }

    ASSERT_OK(gpt->Sync());
  }

  // Create CrOS device partitioner and initialise partition tables.
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_NO_FATAL_FAILURES(CreatePartitioner(disk.get(), &partitioner));
  ASSERT_OK(partitioner->InitPartitionTables());

  // Ensure the final partition layout looks like we expect it to.
  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_NO_FATAL_FAILURES(CreateGptDevice(disk.get(), &gpt));
  const std::array<PartitionDescription, 4> partitions_at_end{
      PartitionDescription{GUID_ZIRCON_A_NAME, kCrosKernelType, 0x822, 0x20000},
      PartitionDescription{GUID_ZIRCON_B_NAME, kCrosKernelType, 0x20822, 0x20000},
      PartitionDescription{GUID_ZIRCON_R_NAME, kCrosKernelType, 0x40822, 0x20000},
      PartitionDescription{GUID_FVM_NAME, kFvmType, 0x60822, 0x2000000},
  };
  ASSERT_NO_FATAL_FAILURES(EnsurePartitionsMatch(gpt.get(), partitions_at_end));

  // Make sure we can find the important partitions.
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
}

TEST_F(CrosDevicePartitionerTests, SupportsPartition) {
  // Create a 32 GiB disk.
  std::unique_ptr<BlockDevice> disk;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(32 * kGibibyte, &disk));

  // Create EFI device partitioner and initialise partition tables.
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_NO_FATAL_FAILURES(CreatePartitioner(disk.get(), &partitioner));

  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_TRUE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));

  // Unsupported partition type.
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kUnknown)));
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloaderA)));
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kAbrMeta)));

  // Unsupported content type.
  EXPECT_FALSE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconA, "foo_type")));
}

TEST_F(CrosDevicePartitionerTests, ValidatePayload) {
  // Create a 32 GiB disk.
  std::unique_ptr<BlockDevice> disk;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(32 * kGibibyte, &disk));

  // Create EFI device partitioner and initialise partition tables.
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_NO_FATAL_FAILURES(CreatePartitioner(disk.get(), &partitioner));

  // Test invalid partitions.
  ASSERT_NOT_OK(partitioner->ValidatePayload(PartitionSpec(paver::Partition::kZirconA),
                                             fbl::Span<uint8_t>()));
  ASSERT_NOT_OK(partitioner->ValidatePayload(PartitionSpec(paver::Partition::kZirconB),
                                             fbl::Span<uint8_t>()));
  ASSERT_NOT_OK(partitioner->ValidatePayload(PartitionSpec(paver::Partition::kZirconR),
                                             fbl::Span<uint8_t>()));

  // Test valid partition.
  constexpr std::string_view kChromeOsMagicHeader = "CHROMEOS";
  ASSERT_OK(partitioner->ValidatePayload(
      PartitionSpec(paver::Partition::kZirconA),
      fbl::Span<const uint8_t>(reinterpret_cast<const uint8_t*>(kChromeOsMagicHeader.data()),
                               kChromeOsMagicHeader.size())));

  // Non-kernel partitions are not validated.
  ASSERT_OK(partitioner->ValidatePayload(PartitionSpec(paver::Partition::kFuchsiaVolumeManager),
                                         fbl::Span<uint8_t>()));
}

TEST_F(CrosDevicePartitionerTests, InitPartitionTablesForRecoveredDevice) {
  std::unique_ptr<BlockDevice> disk;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(32 * kGibibyte, &disk));
  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    // Write initial partitions to disk.
    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURES(CreateGptDevice(disk.get(), &gpt));

    // Write initial partitions to disk. (reflective of state resulting
    // from CrOS recovery)
    const std::array<PartitionDescription, 9> partitions_at_start{
        PartitionDescription{"efi-system", kEfiType, 0x22, 0x1},
        PartitionDescription{"KERN-A", kCrosKernelType, 0x23, 0x1},
        PartitionDescription{"KERN_B", kCrosKernelType, 0x24, 0x1},
        PartitionDescription{"KERN_C", kCrosKernelType, 0x25, 0x1},
        PartitionDescription{"ROOT_A", kCrosRootfsType, 0x26, 0x1},
        PartitionDescription{"ROOT_B", kCrosRootfsType, 0x27, 0x1},
        PartitionDescription{"ROOT_C", kCrosRootfsType, 0x28, 0x1},
        PartitionDescription{"STATE", kStateLinuxGuid, 0x29, 0x1},
        PartitionDescription{"sys-config", kSysConfigType, 0x2A, 0x1},
    };

    for (auto& part : partitions_at_start) {
      ASSERT_OK(
          gpt->AddPartition(part.name, part.type, GetRandomGuid(), part.start, part.length, 0),
          "%s", part.name);
    }

    ASSERT_OK(gpt->Sync());
  }

  // Create CrOS device partitioner and initialise partition tables.
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_NO_FATAL_FAILURES(CreatePartitioner(disk.get(), &partitioner));
  ASSERT_OK(partitioner->InitPartitionTables());

  // Ensure the final partition layout looks like we expect it to.
  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_NO_FATAL_FAILURES(CreateGptDevice(disk.get(), &gpt));
  const std::array<PartitionDescription, 4> partitions_at_end{
      PartitionDescription{GUID_ZIRCON_A_NAME, kCrosKernelType, 0x82B, 0x20000},
      PartitionDescription{GUID_ZIRCON_B_NAME, kCrosKernelType, 0x2082B, 0x20000},
      PartitionDescription{GUID_ZIRCON_R_NAME, kCrosKernelType, 0x4082B, 0x20000},
      PartitionDescription{GUID_FVM_NAME, kFvmType, 0x6082B, 0x2000000},
  };

  ASSERT_NO_FATAL_FAILURES(EnsurePartitionsMatch(gpt.get(), partitions_at_end));

  // Make sure we can find the important partitions.
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
}

// Get Cros GPT flags for a kernel with the given priority.
uint64_t CrosGptPriorityFlags(uint8_t priority) {
  uint64_t flags = 0;
  ZX_ASSERT(gpt_cros_attr_set_priority(&flags, priority) >= 0);
  return flags;
}

TEST_F(CrosDevicePartitionerTests, KernelPriority) {
  // Create a 32 GiB disk.
  std::unique_ptr<BlockDevice> disk;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(32 * kGibibyte, &disk));

  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    // Set up partition table for test.
    // Add non-ChromeOS partitions.
    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURES(CreateGptDevice(disk.get(), &gpt));
    ASSERT_OK(gpt->AddPartition("CROS_KERNEL", kCrosKernelType, GetRandomGuid(), 0x1000, 0x1000,
                                CrosGptPriorityFlags(3)));
    ASSERT_OK(gpt->AddPartition("NOT_KERNEL", GetRandomGuid(), GetRandomGuid(), 0x2000, 0x10,
                                CrosGptPriorityFlags(7)));

    ASSERT_OK(gpt->Sync());
  }

  // Partition the disk, which will add ChromeOS partitions and adjust priorities.
  {
    std::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_NO_FATAL_FAILURES(CreatePartitioner(disk.get(), &partitioner));
    ASSERT_OK(partitioner->InitPartitionTables());
    ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kZirconA)));
  }

  // Ensure that the "zircon-a" kernel was created with priority 4 (priority of CROS_KERNEL + 1).
  {
    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURES(CreateGptDevice(disk.get(), &gpt));
    gpt_partition_t* partition = FindPartitionWithLabel(gpt.get(), GUID_ZIRCON_A_NAME);
    ASSERT_TRUE(partition != nullptr);
    EXPECT_EQ(gpt_cros_attr_get_priority(partition->flags), 4);
  }

  // Partition the disk again.
  {
    std::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_NO_FATAL_FAILURES(CreatePartitioner(disk.get(), &partitioner));
    ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kZirconA)));
  }

  // Ensure that the "zircon-a" kernel still has priority 4.
  {
    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURES(CreateGptDevice(disk.get(), &gpt));
    gpt_partition_t* partition = FindPartitionWithLabel(gpt.get(), GUID_ZIRCON_A_NAME);
    ASSERT_TRUE(partition != nullptr);
    EXPECT_EQ(gpt_cros_attr_get_priority(partition->flags), 4);
  }
}

class FixedDevicePartitionerTests : public zxtest::Test {
 protected:
  FixedDevicePartitionerTests() {
    IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");
    args.disable_block_watcher = false;
    args.path_prefix = "/pkg/";
    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));

    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "misc/ramctl", &fd));
  }

  IsolatedDevmgr devmgr_;
};

TEST_F(FixedDevicePartitionerTests, UseBlockInterfaceTest) {
  auto status = paver::FixedDevicePartitioner::Initialize(devmgr_.devfs_root().duplicate());
  ASSERT_OK(status);
  ASSERT_FALSE(status->IsFvmWithinFtl());
}

TEST_F(FixedDevicePartitionerTests, AddPartitionTest) {
  auto status = paver::FixedDevicePartitioner::Initialize(devmgr_.devfs_root().duplicate());
  ASSERT_OK(status);
  ASSERT_STATUS(status->AddPartition(PartitionSpec(paver::Partition::kZirconB)),
                ZX_ERR_NOT_SUPPORTED);
}

TEST_F(FixedDevicePartitionerTests, WipeFvmTest) {
  auto status = paver::FixedDevicePartitioner::Initialize(devmgr_.devfs_root().duplicate());
  ASSERT_OK(status);
  ASSERT_OK(status->WipeFvm());
}

TEST_F(FixedDevicePartitionerTests, FinalizePartitionTest) {
  auto status = paver::FixedDevicePartitioner::Initialize(devmgr_.devfs_root().duplicate());
  ASSERT_OK(status);
  auto& partitioner = status.value();

  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kBootloaderA)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kZirconA)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kZirconB)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kZirconR)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kVbMetaA)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kVbMetaB)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kVbMetaR)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
}

TEST_F(FixedDevicePartitionerTests, FindPartitionTest) {
  std::unique_ptr<BlockDevice> fvm, bootloader, zircon_a, zircon_b, zircon_r, vbmeta_a, vbmeta_b,
      vbmeta_r;
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kBootloaderType, &bootloader));
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kZirconAType, &zircon_a));
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kZirconBType, &zircon_b));
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kZirconRType, &zircon_r));
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kVbMetaAType, &vbmeta_a));
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kVbMetaBType, &vbmeta_b));
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kVbMetaRType, &vbmeta_r));
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kFvmType, &fvm));

  std::shared_ptr<paver::Context> context = std::make_shared<paver::Context>();
  auto partitioner = paver::DevicePartitionerFactory::Create(
      devmgr_.devfs_root().duplicate(), zx::channel(), paver::Arch::kArm64, context);
  ASSERT_NE(partitioner.get(), nullptr);

  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
}

TEST_F(FixedDevicePartitionerTests, SupportsPartitionTest) {
  auto status = paver::FixedDevicePartitioner::Initialize(devmgr_.devfs_root().duplicate());
  ASSERT_OK(status);
  auto& partitioner = status.value();

  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloaderA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kAbrMeta)));
  EXPECT_TRUE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));

  // Unsupported partition type.
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kUnknown)));

  // Unsupported content type.
  EXPECT_FALSE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconA, "foo_type")));
}

class SherlockPartitionerTests : public GptDevicePartitionerTests {
 protected:
  SherlockPartitionerTests() : GptDevicePartitionerTests("sherlock", 512) {}

  // Create a DevicePartition for a device.
  zx::status<std::unique_ptr<paver::DevicePartitioner>> CreatePartitioner(
      const fbl::unique_fd& device) {
    zx::channel svc_root = GetSvcRoot();
    return paver::SherlockPartitioner::Initialize(devmgr_.devfs_root().duplicate(),
                                                  std::move(svc_root), device);
  }
};

TEST_F(SherlockPartitionerTests, InitializeWithoutGptFails) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(&gpt_dev));

  ASSERT_NOT_OK(CreatePartitioner(kDummyDevice));
}

TEST_F(SherlockPartitionerTests, InitializeWithoutFvmSucceeds) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(32 * kGibibyte, &gpt_dev));

  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    // Set up a valid GPT.
    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURES(CreateGptDevice(gpt_dev.get(), &gpt));

    ASSERT_OK(CreatePartitioner(kDummyDevice));
  }
}

TEST_F(SherlockPartitionerTests, AddPartitionNotSupported) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(64 * kMebibyte, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  auto status = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status);

  ASSERT_STATUS(status->AddPartition(PartitionSpec(paver::Partition::kZirconB)),
                ZX_ERR_NOT_SUPPORTED);
}

TEST_F(SherlockPartitionerTests, InitializePartitionTable) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = 0x748034;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(kBlockCount * block_size_, &gpt_dev));
  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURES(CreateGptDevice(gpt_dev.get(), &gpt));

    const PartitionDescription kStartingPartitions[] = {
        {"bootloader", kDummyType, 0x22, 0x2000},   {"reserved", kDummyType, 0x12000, 0x20000},
        {"env", kDummyType, 0x36000, 0x4000},       {"fts", kDummyType, 0x3E000, 0x2000},
        {"factory", kDummyType, 0x44000, 0x10000},  {"recovery", kDummyType, 0x58000, 0x10000},
        {"boot", kDummyType, 0x6C000, 0x10000},     {"system", kDummyType, 0x80000, 0x278000},
        {"cache", kDummyType, 0x2FC000, 0x400000},  {"fct", kDummyType, 0x700000, 0x20000},
        {"sysconfig", kDummyType, 0x724000, 0x800}, {"migration", kDummyType, 0x728800, 0x3800},
        {"buf", kDummyType, 0x730000, 0x18000},
    };

    for (const auto& part : fbl::Span(kStartingPartitions)) {
      ASSERT_OK(
          gpt->AddPartition(part.name, part.type, GetRandomGuid(), part.start, part.length, 0),
          "%s", part.name);
    }

    ASSERT_OK(gpt->Sync());
  }

  fdio_cpp::UnownedFdioCaller caller(gpt_dev->fd());
  auto result = ::llcpp::fuchsia::device::Controller::Call::Rebind(
      caller.channel(), fidl::StringView("/boot/driver/gpt.so"));
  ASSERT_TRUE(result.ok());
  ASSERT_FALSE(result->result.is_err());

  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));
  auto status = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  ASSERT_OK(partitioner->InitPartitionTables());

  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_NO_FATAL_FAILURES(CreateGptDevice(gpt_dev.get(), &gpt));

  // Ensure the final partition layout looks like we expect it to.
  const PartitionDescription kFinalPartitions[] = {
      {"bootloader", kDummyType, 0x22, 0x2000},
      {GUID_SYS_CONFIG_NAME, kSysConfigType, 0x2022, 0x678},
      {GUID_ABR_META_NAME, kAbrMetaType, 0x269A, 0x8},
      {GUID_VBMETA_A_NAME, kVbMetaAType, 0x26A2, 0x80},
      {GUID_VBMETA_B_NAME, kVbMetaBType, 0x2722, 0x80},
      {GUID_VBMETA_R_NAME, kVbMetaRType, 0x27A2, 0x80},
      {"migration", kDummyType, 0x2822, 0x3800},
      {"reserved", kDummyType, 0x12000, 0x20000},
      {"env", kDummyType, 0x36000, 0x4000},
      {"fts", kDummyType, 0x3E000, 0x2000},
      {"factory", kDummyType, 0x44000, 0x10000},
      {"recovery", kZirconRType, 0x54000, 0x10000},
      {"boot", kZirconAType, 0x64000, 0x10000},
      {"system", kZirconBType, 0x74000, 0x10000},
      {GUID_FVM_NAME, kFvmType, 0x84000, 0x668000},
      {"fct", kDummyType, 0x6EC000, 0x20000},
      {"buffer", kDummyType, 0x70C000, 0x18000},
  };
  ASSERT_NO_FATAL_FAILURES(EnsurePartitionsMatch(gpt.get(), kFinalPartitions));

  // Make sure we can find the important partitions.
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kAbrMeta)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
}

TEST_F(SherlockPartitionerTests, FindBootloader) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(&gpt_dev));

  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_NO_FATAL_FAILURES(CreateGptDevice(gpt_dev.get(), &gpt));

  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));
  auto status = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  // No boot0/boot1 yet, we shouldn't be able to find the bootloader.
  ASSERT_NOT_OK(
      partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA, "skip_metadata")));

  std::unique_ptr<BlockDevice> boot0_dev, boot1_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(kBlockCount * kBlockSize, kBoot0Type, &boot0_dev));
  ASSERT_NO_FATAL_FAILURES(CreateDisk(kBlockCount * kBlockSize, kBoot1Type, &boot1_dev));

  // Now it should succeed.
  ASSERT_OK(
      partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA, "skip_metadata")));
}

TEST_F(SherlockPartitionerTests, SupportsPartition) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(64 * kMebibyte, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  auto status = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  EXPECT_TRUE(partitioner->SupportsPartition(
      PartitionSpec(paver::Partition::kBootloaderA, "skip_metadata")));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kAbrMeta)));
  EXPECT_TRUE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));

  // Unsupported partition type.
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kUnknown)));

  // Unsupported content type.
  EXPECT_FALSE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconA, "foo_type")));
}

class LuisPartitionerTests : public GptDevicePartitionerTests {
 protected:
  LuisPartitionerTests() : GptDevicePartitionerTests("luis", 512) {}

  // Create a DevicePartition for a device.
  zx::status<std::unique_ptr<paver::DevicePartitioner>> CreatePartitioner(
      const fbl::unique_fd& device) {
    zx::channel svc_root = GetSvcRoot();
    return paver::LuisPartitioner::Initialize(devmgr_.devfs_root().duplicate(), std::move(svc_root),
                                              device);
  }

  void InitializeStartingGPTPartitions(BlockDevice* gpt_dev,
                                       const std::vector<PartitionDescription>& init_partitions) {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_OK(
        gpt::GptDevice::Create(gpt_dev->fd(), gpt_dev->block_size(), gpt_dev->block_count(), &gpt));
    ASSERT_OK(gpt->Sync());

    for (const auto& part : init_partitions) {
      ASSERT_OK(
          gpt->AddPartition(part.name, part.type, GetRandomGuid(), part.start, part.length, 0),
          "%s", part.name);
    }

    ASSERT_OK(gpt->Sync());

    fdio_cpp::UnownedFdioCaller caller(gpt_dev->fd());
    auto result = ::llcpp::fuchsia::device::Controller::Call::Rebind(
        caller.channel(), fidl::StringView("/boot/driver/gpt.so"));
    ASSERT_TRUE(result.ok());
    ASSERT_FALSE(result->result.is_err());
  }
};

TEST_F(LuisPartitionerTests, InitializeWithoutGptFails) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(&gpt_dev));

  ASSERT_NOT_OK(CreatePartitioner(kDummyDevice));
}

TEST_F(LuisPartitionerTests, InitializeWithoutFvmSucceeds) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(32 * kGibibyte, &gpt_dev));

  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    // Set up a valid GPT.
    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURES(CreateGptDevice(gpt_dev.get(), &gpt));

    ASSERT_OK(CreatePartitioner(kDummyDevice));
  }
}

TEST_F(LuisPartitionerTests, AddPartitionNotSupported) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(64 * kMebibyte, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  auto status = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status);

  ASSERT_STATUS(status->AddPartition(PartitionSpec(paver::Partition::kZirconB)),
                ZX_ERR_NOT_SUPPORTED);
}

TEST_F(LuisPartitionerTests, FindPartition) {
  std::unique_ptr<BlockDevice> gpt_dev;
  // kBlockCount should be a value large enough to accomodate all partitions and blocks reserved by
  // gpt. The current value is copied from the case of sherlock. As of now, we assume they have
  // the same disk size requirement.
  constexpr uint64_t kBlockCount = 0x748034;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(kBlockCount * block_size_, &gpt_dev));

  // The initial gpt partitions are randomly chosen and does not necessarily reflect the
  // actual gpt partition layout in product.
  const std::vector<PartitionDescription> kLuisStartingPartitions = {
      {GPT_DURABLE_BOOT_NAME, kDummyType, 0x10400, 0x10000},
      {GPT_BOOTLOADER_A_NAME, kDummyType, 0x30400, 0x10000},
      {GPT_BOOTLOADER_B_NAME, kDummyType, 0x40400, 0x10000},
      {GPT_BOOTLOADER_R_NAME, kDummyType, 0x50400, 0x10000},
      {GPT_VBMETA_A_NAME, kDummyType, 0x60400, 0x10000},
      {GPT_VBMETA_B_NAME, kDummyType, 0x70400, 0x10000},
      {GPT_VBMETA_R_NAME, kDummyType, 0x80400, 0x10000},
      {GPT_ZIRCON_A_NAME, kDummyType, 0x90400, 0x10000},
      {GPT_ZIRCON_B_NAME, kDummyType, 0xa0400, 0x10000},
      {GPT_ZIRCON_R_NAME, kDummyType, 0xb0400, 0x10000},
      {GPT_FACTORY_NAME, kDummyType, 0xc0400, 0x10000},
      {GPT_DURABLE_NAME, kDummyType, 0xd0400, 0x10000},
      {GPT_FVM_NAME, kDummyType, 0xe0400, 0x10000},

  };
  ASSERT_NO_FATAL_FAILURES(InitializeStartingGPTPartitions(gpt_dev.get(), kLuisStartingPartitions));

  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));
  auto status = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  EXPECT_NOT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA)));

  std::unique_ptr<BlockDevice> boot0_dev, boot1_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(kBlockCount * kBlockSize, kBoot0Type, &boot0_dev));
  ASSERT_NO_FATAL_FAILURES(CreateDisk(kBlockCount * kBlockSize, kBoot1Type, &boot1_dev));

  // Make sure we can find the important partitions.
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kAbrMeta)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
}

TEST_F(LuisPartitionerTests, CreateAbrClient) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = 0x748034;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(kBlockCount * block_size_, &gpt_dev));

  const std::vector<PartitionDescription> kStartingPartitions = {
      {GPT_DURABLE_BOOT_NAME, kDummyType, 0x10400, 0x10000},
  };
  ASSERT_NO_FATAL_FAILURES(InitializeStartingGPTPartitions(gpt_dev.get(), kStartingPartitions));
  zx::channel svc_root = GetSvcRoot();
  std::shared_ptr<paver::Context> context;
  EXPECT_OK(paver::LuisAbrClientFactory().New(devmgr_.devfs_root().duplicate(), std::move(svc_root),
                                              context));
}

TEST_F(LuisPartitionerTests, SupportsPartition) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(CreateDisk(64 * kMebibyte, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  auto status = CreatePartitioner(std::move(gpt_fd));
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloaderA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloaderB)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloaderR)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kAbrMeta)));
  EXPECT_TRUE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
  // Unsupported partition type.
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kUnknown)));

  // Unsupported content type.
  EXPECT_FALSE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kAbrMeta, "foo_type")));
}

TEST(AstroPartitionerTests, IsFvmWithinFtl) {
  std::unique_ptr<SkipBlockDevice> device;
  ASSERT_NO_FATAL_FAILURES(SkipBlockDevice::Create(kNandInfo, &device));

  zx::channel svc_root;
  std::shared_ptr<paver::Context> context = std::make_shared<paver::Context>();
  auto status = paver::AstroPartitioner::Initialize(device->devfs_root(), svc_root, context);
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();
  ASSERT_TRUE(partitioner->IsFvmWithinFtl());
}

TEST(AstroPartitionerTests, ChooseAstroPartitioner) {
  std::unique_ptr<SkipBlockDevice> device;
  ASSERT_NO_FATAL_FAILURES(SkipBlockDevice::Create(kNandInfo, &device));
  auto devfs_root = device->devfs_root();
  std::unique_ptr<BlockDevice> zircon_a;
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devfs_root, kZirconAType, &zircon_a));

  std::shared_ptr<paver::Context> context = std::make_shared<paver::Context>();
  auto partitioner = paver::DevicePartitionerFactory::Create(std::move(devfs_root), zx::channel(),
                                                             paver::Arch::kArm64, context);
  ASSERT_NE(partitioner.get(), nullptr);
  ASSERT_TRUE(partitioner->IsFvmWithinFtl());
}

TEST(AstroPartitionerTests, AddPartitionTest) {
  std::unique_ptr<SkipBlockDevice> device;
  ASSERT_NO_FATAL_FAILURES(SkipBlockDevice::Create(kNandInfo, &device));

  zx::channel svc_root;
  std::shared_ptr<paver::Context> context = std::make_shared<paver::Context>();
  auto status = paver::AstroPartitioner::Initialize(device->devfs_root(), svc_root, context);
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();
  ASSERT_STATUS(partitioner->AddPartition(PartitionSpec(paver::Partition::kZirconB)),
                ZX_ERR_NOT_SUPPORTED);
}

TEST(AstroPartitionerTests, WipeFvmTest) {
  std::unique_ptr<SkipBlockDevice> device;
  ASSERT_NO_FATAL_FAILURES(SkipBlockDevice::Create(kNandInfo, &device));

  zx::channel svc_root;
  std::shared_ptr<paver::Context> context = std::make_shared<paver::Context>();
  auto status = paver::AstroPartitioner::Initialize(device->devfs_root(), svc_root, context);
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();
  ASSERT_OK(partitioner->WipeFvm());
}

TEST(AstroPartitionerTests, FinalizePartitionTest) {
  std::unique_ptr<SkipBlockDevice> device;
  ASSERT_NO_FATAL_FAILURES(SkipBlockDevice::Create(kNandInfo, &device));

  zx::channel svc_root;
  std::shared_ptr<paver::Context> context = std::make_shared<paver::Context>();
  auto status = paver::AstroPartitioner::Initialize(device->devfs_root(), svc_root, context);
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kBootloaderA)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kZirconA)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kZirconB)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kZirconR)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kVbMetaA)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kVbMetaB)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kVbMetaR)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kSysconfig)));
}

TEST(AstroPartitionerTests, FindPartitionTest) {
  std::unique_ptr<SkipBlockDevice> device;
  ASSERT_NO_FATAL_FAILURES(SkipBlockDevice::Create(kNandInfo, &device));
  auto devfs_root = device->devfs_root();
  std::unique_ptr<BlockDevice> fvm;
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devfs_root, kFvmType, &fvm));

  zx::channel svc_root;
  std::shared_ptr<paver::Context> context = std::make_shared<paver::Context>();
  auto status = paver::AstroPartitioner::Initialize(device->devfs_root(), svc_root, context);
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA, "bl2")));
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA)));
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR)));
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kSysconfig)));

  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
}

TEST(AstroPartitionerTests, SupportsPartition) {
  std::unique_ptr<SkipBlockDevice> device;
  ASSERT_NO_FATAL_FAILURES(SkipBlockDevice::Create(kNandInfo, &device));

  zx::channel svc_root;
  std::shared_ptr<paver::Context> context = std::make_shared<paver::Context>();
  auto status = paver::AstroPartitioner::Initialize(device->devfs_root(), svc_root, context);
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloaderA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloaderA, "bl2")));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kAbrMeta)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kSysconfig)));
  EXPECT_TRUE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));

  // Unsupported partition type.
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kUnknown)));

  // Unsupported content type.
  EXPECT_FALSE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloaderA, "unknown")));
  EXPECT_FALSE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconA, "foo_type")));
}

// Gets a PartitionClient for the given |spec| and writes |contents| padded to
// the partition's block size.
//
// Call with ASSERT_NO_FATAL_FAILURES.
void WritePartition(const paver::DevicePartitioner* partitioner, const PartitionSpec& spec,
                    std::string_view contents) {
  auto status = partitioner->FindPartition(spec);
  ASSERT_OK(status);
  std::unique_ptr<paver::PartitionClient>& partition = status.value();

  auto status2 = partition->GetBlockSize();
  ASSERT_OK(status2);
  size_t& block_size = status2.value();

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(block_size, ZX_VMO_RESIZABLE, &vmo));
  ASSERT_OK(vmo.write(contents.data(), 0, contents.size()));
  ASSERT_OK(partition->Write(vmo, block_size));
}

TEST(AstroPartitionerTests, BootloaderTplTest) {
  std::unique_ptr<SkipBlockDevice> device;
  ASSERT_NO_FATAL_FAILURES(SkipBlockDevice::Create(kNandInfo, &device));

  zx::channel svc_root;
  std::shared_ptr<paver::Context> context = std::make_shared<paver::Context>();
  auto status = paver::AstroPartitioner::Initialize(device->devfs_root(), svc_root, context);
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  ASSERT_NO_FATAL_FAILURES(
      WritePartition(partitioner.get(), PartitionSpec(paver::Partition::kBootloaderA), "abcd1234"));

  const uint8_t* tpl_partition = PartitionStart(device->mapper(), kNandInfo, GUID_BOOTLOADER_VALUE);
  ASSERT_NOT_NULL(tpl_partition);
  ASSERT_EQ(0, memcmp("abcd1234", tpl_partition, 8));
}

TEST(AstroPartitionerTests, BootloaderBl2Test) {
  std::unique_ptr<SkipBlockDevice> device;
  ASSERT_NO_FATAL_FAILURES(SkipBlockDevice::Create(kNandInfo, &device));

  zx::channel svc_root;
  std::shared_ptr<paver::Context> context = std::make_shared<paver::Context>();
  auto status = paver::AstroPartitioner::Initialize(device->devfs_root(), svc_root, context);
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  ASSERT_NO_FATAL_FAILURES(WritePartition(
      partitioner.get(), PartitionSpec(paver::Partition::kBootloaderA, "bl2"), "123xyz"));

  const uint8_t* bl2_partition = PartitionStart(device->mapper(), kNandInfo, GUID_BL2_VALUE);
  ASSERT_NOT_NULL(bl2_partition);
  // Special BL2 handling - image contents start at offset 4096 (page 1 on Astro).
  ASSERT_EQ(0, memcmp("123xyz", bl2_partition + 4096, 6));
}

class As370PartitionerTests : public zxtest::Test {
 protected:
  As370PartitionerTests() {
    IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");
    args.disable_block_watcher = false;
    args.board_name = "visalia";
    args.path_prefix = "/pkg/";
    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));

    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform", &fd));
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "misc/ramctl", &fd));
  }

  IsolatedDevmgr devmgr_;
};

TEST_F(As370PartitionerTests, IsFvmWithinFtl) {
  auto status = paver::As370Partitioner::Initialize(devmgr_.devfs_root().duplicate());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();
  ASSERT_TRUE(partitioner->IsFvmWithinFtl());
}

TEST_F(As370PartitionerTests, ChooseAs370Partitioner) {
  std::shared_ptr<paver::Context> context = std::make_shared<paver::Context>();
  auto partitioner = paver::DevicePartitionerFactory::Create(
      devmgr_.devfs_root().duplicate(), zx::channel(), paver::Arch::kArm64, context);
  ASSERT_NE(partitioner.get(), nullptr);
  ASSERT_TRUE(partitioner->IsFvmWithinFtl());
}

TEST_F(As370PartitionerTests, AddPartitionTest) {
  auto status = paver::As370Partitioner::Initialize(devmgr_.devfs_root().duplicate());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();
  ASSERT_STATUS(partitioner->AddPartition(PartitionSpec(paver::Partition::kZirconB)),
                ZX_ERR_NOT_SUPPORTED);
}

TEST_F(As370PartitionerTests, WipeFvmTest) {
  auto status = paver::As370Partitioner::Initialize(devmgr_.devfs_root().duplicate());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();
  ASSERT_OK(partitioner->WipeFvm());
}

TEST_F(As370PartitionerTests, FinalizePartitionTest) {
  auto status = paver::As370Partitioner::Initialize(devmgr_.devfs_root().duplicate());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kBootloaderA)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kZirconA)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kZirconB)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kZirconR)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kVbMetaA)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kVbMetaB)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kVbMetaR)));
}

TEST_F(As370PartitionerTests, FindPartitionTest) {
  std::unique_ptr<BlockDevice> fvm;
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kFvmType, &fvm));

  auto status = paver::As370Partitioner::Initialize(devmgr_.devfs_root().duplicate());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
}

TEST_F(As370PartitionerTests, SupportsPartition) {
  auto status = paver::As370Partitioner::Initialize(devmgr_.devfs_root().duplicate());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloaderA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_TRUE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));

  // Unsupported partition type.
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kUnknown)));
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kAbrMeta)));

  // Unsupported content type.
  EXPECT_FALSE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconA, "foo_type")));
}

}  // namespace
