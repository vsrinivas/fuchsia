// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device-partitioner.h"

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
#include <zircon/boot/image.h>
#include <zircon/errors.h>
#include <zircon/hw/gpt.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <array>
#include <memory>
#include <utility>

#include <fbl/auto_call.h>
#include <fbl/span.h>
#include <gpt/cros.h>
#include <gpt/gpt.h>
#include <soc/aml-common/aml-guid.h>
#include <zxtest/zxtest.h>

#include "test-utils.h"

namespace {

constexpr uint64_t kGibibyte = 1024 * 1024 * 1024;

using devmgr_integration_test::RecursiveWaitForFile;
using driver_integration_test::IsolatedDevmgr;
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
    ASSERT_TRUE(gpt_part != nullptr);
    EXPECT_TRUE(memcmp(part.type, gpt_part->type, GPT_GUID_LEN) == 0);
    EXPECT_EQ(part.start, gpt_part->first);
    EXPECT_EQ(part.start + part.length - 1, gpt_part->last);
  }
}

TEST(PartitionSpec, ToStringDefaultContentType) {
  EXPECT_EQ(PartitionSpec(paver::Partition::kZirconA).ToString(), GUID_ZIRCON_A_NAME);
  EXPECT_EQ(PartitionSpec(paver::Partition::kVbMetaB).ToString(), GUID_VBMETA_B_NAME);
}

TEST(PartitionSpec, ToStringWithContentType) {
  EXPECT_EQ(PartitionSpec(paver::Partition::kZirconA, "foo_type").ToString(),
            GUID_ZIRCON_A_NAME " (foo_type)");
  EXPECT_EQ(PartitionSpec(paver::Partition::kZirconA, "a b c").ToString(),
            GUID_ZIRCON_A_NAME " (a b c)");
}

class EfiDevicePartitionerTests : public zxtest::Test {
 protected:
  EfiDevicePartitionerTests() {
    IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");
    args.disable_block_watcher = false;
    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));

    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "misc/ramctl", &fd));
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform", &fd));
  }

  IsolatedDevmgr devmgr_;
};

// TODO(fxb/42894): Re-enable after de-flaking
TEST_F(EfiDevicePartitionerTests, DISABLED_InitializeWithoutGptFails) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, &gpt_dev));

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_NE(paver::EfiDevicePartitioner::Initialize(devmgr_.devfs_root().duplicate(),
                                                    paver::Arch::kX64, std::nullopt, &partitioner),
            ZX_OK);
}

TEST_F(EfiDevicePartitionerTests, DISABLED_InitializeWithoutFvmFails) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, &gpt_dev));

  // Set up a valid GPT.
  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_OK(gpt::GptDevice::Create(gpt_dev->fd(), kBlockSize, kBlockCount, &gpt));
  ASSERT_OK(gpt->Sync());

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_NE(paver::EfiDevicePartitioner::Initialize(devmgr_.devfs_root().duplicate(),
                                                    paver::Arch::kX64, std::nullopt, &partitioner),
            ZX_OK);
}

TEST_F(EfiDevicePartitionerTests, DISABLED_AddPartitionZirconB) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = (1LU << 26) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kBlockCount, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(paver::EfiDevicePartitioner::Initialize(
      devmgr_.devfs_root().duplicate(), paver::Arch::kX64, std::move(gpt_fd), &partitioner));

  ASSERT_OK(partitioner->AddPartition(PartitionSpec(paver::Partition::kZirconB), nullptr));
}

TEST_F(EfiDevicePartitionerTests, DISABLED_AddPartitionFvm) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = (1LU << 34) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kBlockCount, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(paver::EfiDevicePartitioner::Initialize(
      devmgr_.devfs_root().duplicate(), paver::Arch::kX64, std::move(gpt_fd), &partitioner));

  ASSERT_OK(
      partitioner->AddPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager), nullptr));
}

TEST_F(EfiDevicePartitionerTests, DISABLED_AddPartitionTooSmall) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(paver::EfiDevicePartitioner::Initialize(
      devmgr_.devfs_root().duplicate(), paver::Arch::kX64, std::move(gpt_fd), &partitioner));

  ASSERT_NE(partitioner->AddPartition(PartitionSpec(paver::Partition::kZirconB), nullptr), ZX_OK);
}

TEST_F(EfiDevicePartitionerTests, DISABLED_AddedPartitionIsFindable) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = (1LU << 26) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kBlockCount, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(paver::EfiDevicePartitioner::Initialize(
      devmgr_.devfs_root().duplicate(), paver::Arch::kX64, std::move(gpt_fd), &partitioner));

  ASSERT_OK(partitioner->AddPartition(PartitionSpec(paver::Partition::kZirconB), nullptr));
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB), nullptr));
  ASSERT_NE(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA), nullptr), ZX_OK);
}

TEST_F(EfiDevicePartitionerTests, DISABLED_InitializePartitionsWithoutExplicitDevice) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = (1LU << 34) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kBlockCount, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(paver::EfiDevicePartitioner::Initialize(
      devmgr_.devfs_root().duplicate(), paver::Arch::kX64, std::move(gpt_fd), &partitioner));

  ASSERT_OK(
      partitioner->AddPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager), nullptr));
  partitioner.reset();

  fbl::unique_fd fd;
  // Note that this time we don't pass in a block device fd.
  ASSERT_OK(paver::EfiDevicePartitioner::Initialize(devmgr_.devfs_root().duplicate(),
                                                    paver::Arch::kX64, std::nullopt, &partitioner));
}

TEST_F(EfiDevicePartitionerTests,
       DISABLED_InitializeWithMultipleCandidateGPTsFailsWithoutExplicitDevice) {
  std::unique_ptr<BlockDevice> gpt_dev1, gpt_dev2;
  constexpr uint64_t kBlockCount = (1LU << 34) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kBlockCount, &gpt_dev1));
  fbl::unique_fd gpt_fd(dup(gpt_dev1->fd()));

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(paver::EfiDevicePartitioner::Initialize(
      devmgr_.devfs_root().duplicate(), paver::Arch::kX64, std::move(gpt_fd), &partitioner));

  ASSERT_OK(
      partitioner->AddPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager), nullptr));
  partitioner.reset();

  partitioner.reset();
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kBlockCount, &gpt_dev2));
  gpt_fd.reset(dup(gpt_dev2->fd()));

  ASSERT_OK(paver::EfiDevicePartitioner::Initialize(
      devmgr_.devfs_root().duplicate(), paver::Arch::kX64, std::move(gpt_fd), &partitioner));
  ASSERT_OK(
      partitioner->AddPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager), nullptr));
  partitioner.reset();

  // Note that this time we don't pass in a block device fd.
  ASSERT_NE(paver::EfiDevicePartitioner::Initialize(devmgr_.devfs_root().duplicate(),
                                                    paver::Arch::kX64, std::nullopt, &partitioner),
            ZX_OK);
}

TEST_F(EfiDevicePartitionerTests, DISABLED_InitializeWithTwoCandidateGPTsSucceedsAfterWipingOne) {
  std::unique_ptr<BlockDevice> gpt_dev1, gpt_dev2;
  constexpr uint64_t kBlockCount = (1LU << 34) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kBlockCount, &gpt_dev1));
  fbl::unique_fd gpt_fd(dup(gpt_dev1->fd()));

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(paver::EfiDevicePartitioner::Initialize(
      devmgr_.devfs_root().duplicate(), paver::Arch::kX64, std::move(gpt_fd), &partitioner));

  ASSERT_OK(
      partitioner->AddPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager), nullptr));
  partitioner.reset();

  partitioner.reset();
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kBlockCount, &gpt_dev2));
  gpt_fd.reset(dup(gpt_dev2->fd()));

  ASSERT_OK(paver::EfiDevicePartitioner::Initialize(
      devmgr_.devfs_root().duplicate(), paver::Arch::kX64, std::move(gpt_fd), &partitioner));
  ASSERT_OK(
      partitioner->AddPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager), nullptr));
  ASSERT_OK(partitioner->WipeFvm());
  partitioner.reset();

  // Note that this time we don't pass in a block device fd.
  ASSERT_OK(paver::EfiDevicePartitioner::Initialize(devmgr_.devfs_root().duplicate(),
                                                    paver::Arch::kX64, std::nullopt, &partitioner));
}

TEST_F(EfiDevicePartitionerTests, DISABLED_AddedPartitionRemovedAfterWipePartitions) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = (1LU << 26) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kBlockCount, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(paver::EfiDevicePartitioner::Initialize(
      devmgr_.devfs_root().duplicate(), paver::Arch::kX64, std::move(gpt_fd), &partitioner));

  ASSERT_OK(partitioner->AddPartition(PartitionSpec(paver::Partition::kZirconB), nullptr));
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB), nullptr));
  ASSERT_OK(partitioner->WipePartitionTables());
  ASSERT_NOT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB), nullptr));
}

TEST_F(EfiDevicePartitionerTests, DISABLED_InitPartitionTables) {
  // 32 GiB disk.
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = (32LU << 30) / kBlockSize;

  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kBlockCount, kBlockSize, &gpt_dev));

  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_OK(gpt::GptDevice::Create(gpt_dev->fd(), kBlockSize, kBlockCount, &gpt));
  ASSERT_OK(gpt->Sync());

  // Write initial partitions to disk.
  const std::array<PartitionDescription, 9> partitions_at_start{
      PartitionDescription{"efi", kEfiType, 0x22, 0x1},
      PartitionDescription{"ZIRCON-A", kZirconAType, 0x23, 0x1},
      PartitionDescription{"zircon_b", kZirconBType, 0x24, 0x1},
      PartitionDescription{"zircon r", kZirconRType, 0x25, 0x1},
      PartitionDescription{"vbmeta-a", kVbMetaAType, 0x26, 0x1},
      PartitionDescription{"VBMETA_B", kVbMetaBType, 0x27, 0x1},
      PartitionDescription{"VBMETA R", kVbMetaRType, 0x28, 0x1},
      PartitionDescription{"abrmeta", kAbrMetaType, 0x29, 0x1},
      PartitionDescription{"FVM", kFvmType, 0x30, 0x1},
  };
  for (auto& part : partitions_at_start) {
    ASSERT_OK(gpt->AddPartition(part.name, part.type, GetRandomGuid(), part.start, part.length, 0),
              "%s", part.name);
  }
  ASSERT_OK(gpt->Sync());

  // Create EFI device partitioner and initialise partition tables.
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(paver::EfiDevicePartitioner::Initialize(
      devmgr_.devfs_root().duplicate(), paver::Arch::kX64, std::move(gpt_fd), &partitioner));
  ASSERT_OK(partitioner->InitPartitionTables());

  // Ensure the final partition layout looks like we expect it to.
  ASSERT_OK(gpt::GptDevice::Create(gpt_dev->fd(), kBlockSize, kBlockCount, &gpt));
  const std::array<PartitionDescription, 9> partitions_at_end{
      PartitionDescription{GUID_EFI_NAME, kEfiType, 0x22, 0x8000},
      PartitionDescription{GUID_ZIRCON_A_NAME, kZirconAType, 0x8022, 0x40000},
      PartitionDescription{GUID_ZIRCON_B_NAME, kZirconBType, 0x48022, 0x40000},
      PartitionDescription{GUID_ZIRCON_R_NAME, kZirconRType, 0x88022, 0x60000},
      PartitionDescription{GUID_VBMETA_A_NAME, kVbMetaAType, 0xe8022, 0x80},
      PartitionDescription{GUID_VBMETA_B_NAME, kVbMetaBType, 0xe80a2, 0x80},
      PartitionDescription{GUID_VBMETA_R_NAME, kVbMetaRType, 0xe8122, 0x80},
      PartitionDescription{GUID_ABR_META_NAME, kAbrMetaType, 0xe81a2, 0x8},
      PartitionDescription{GUID_FVM_NAME, kFvmType, 0xe81aa, 0x2000000},
  };
  ASSERT_NO_FATAL_FAILURES(EnsurePartitionsMatch(gpt.get(), partitions_at_end));

  // Make sure we can find the important partitions.
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloader), nullptr));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA), nullptr));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB), nullptr));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR), nullptr));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaA), nullptr));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaB), nullptr));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaR), nullptr));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kAbrMeta), nullptr));
  EXPECT_OK(
      partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager), nullptr));
}

TEST_F(EfiDevicePartitionerTests, DISABLED_SupportsPartition) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = (1LU << 30) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kBlockCount, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(paver::EfiDevicePartitioner::Initialize(
      devmgr_.devfs_root().duplicate(), paver::Arch::kX64, std::move(gpt_fd), &partitioner));

  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloader)));
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

TEST_F(EfiDevicePartitionerTests, DISABLED_ValidatePayload) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = (1LU << 30) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kBlockCount, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(paver::EfiDevicePartitioner::Initialize(
      devmgr_.devfs_root().duplicate(), paver::Arch::kX64, std::move(gpt_fd), &partitioner));

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

class CrosDevicePartitionerTests : public zxtest::Test {
 protected:
  CrosDevicePartitionerTests() {
    IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");
    args.disable_block_watcher = false;
    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));

    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "misc/ramctl", &fd));
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform", &fd));
  }

  // Create a disk with the given size in bytes.
  void CreateCrosDisk(uint64_t bytes, std::unique_ptr<BlockDevice>* device) {
    constexpr uint64_t kBlockSize = 512;
    ASSERT_TRUE(bytes % kBlockSize == 0);
    uint64_t num_blocks = bytes / kBlockSize;

    ASSERT_NO_FATAL_FAILURES(
        BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, num_blocks, kBlockSize, device));
  }

  // Create GPT from a device.
  void CreateGptDevice(BlockDevice* device, std::unique_ptr<gpt::GptDevice>* gpt) {
    ASSERT_OK(gpt::GptDevice::Create(device->fd(), /*block_size=*/device->block_size(),
                                     /*blocks=*/device->block_count(), gpt));
    ASSERT_OK((*gpt)->Sync());
  }

  // Create a DevicePartition for a device.
  void CreatePartitioner(BlockDevice* device,
                         std::unique_ptr<paver::DevicePartitioner>* partitioner) {
    ASSERT_OK(paver::CrosDevicePartitioner::Initialize(
        devmgr_.devfs_root().duplicate(), paver::Arch::kX64, fbl::unique_fd{dup(device->fd())},
        partitioner));
  }

  IsolatedDevmgr devmgr_;
};

TEST_F(CrosDevicePartitionerTests, DISABLED_InitPartitionTables) {
  std::unique_ptr<BlockDevice> disk;
  ASSERT_NO_FATAL_FAILURES(CreateCrosDisk(32 * kGibibyte, &disk));

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
    ASSERT_OK(gpt->AddPartition(part.name, part.type, GetRandomGuid(), part.start, part.length, 0),
              "%s", part.name);
  }
  ASSERT_OK(gpt->Sync());


  // Create CrOS device partitioner and initialise partition tables.
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_NO_FATAL_FAILURES(CreatePartitioner(disk.get(), &partitioner));
  ASSERT_OK(partitioner->InitPartitionTables());

  // Ensure the final partition layout looks like we expect it to.
  ASSERT_NO_FATAL_FAILURES(CreateGptDevice(disk.get(), &gpt));
  const std::array<PartitionDescription, 4> partitions_at_end{
      PartitionDescription{GUID_ZIRCON_A_NAME, kCrosKernelType, 0x822, 0x20000},
      PartitionDescription{GUID_ZIRCON_B_NAME, kCrosKernelType, 0x20822, 0x20000},
      PartitionDescription{GUID_ZIRCON_R_NAME, kCrosKernelType, 0x40822, 0x20000},
      PartitionDescription{GUID_FVM_NAME, kFvmType, 0x60822, 0x2000000},
  };
  ASSERT_NO_FATAL_FAILURES(EnsurePartitionsMatch(gpt.get(), partitions_at_end));

  // Make sure we can find the important partitions.
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA), nullptr));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB), nullptr));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR), nullptr));
  EXPECT_OK(
      partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager), nullptr));
}

TEST_F(CrosDevicePartitionerTests, DISABLED_SupportsPartition) {
  // Create a 32 GiB disk.
  std::unique_ptr<BlockDevice> disk;
  ASSERT_NO_FATAL_FAILURES(CreateCrosDisk(32 * kGibibyte, &disk));

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
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloader)));
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kAbrMeta)));

  // Unsupported content type.
  EXPECT_FALSE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconA, "foo_type")));
}

TEST_F(CrosDevicePartitionerTests, DISABLED_ValidatePayload) {
  // Create a 32 GiB disk.
  std::unique_ptr<BlockDevice> disk;
  ASSERT_NO_FATAL_FAILURES(CreateCrosDisk(32 * kGibibyte, &disk));

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

TEST_F(CrosDevicePartitionerTests, DISABLED_InitPartitionTablesForRecoveredDevice) {
  std::unique_ptr<BlockDevice> disk;
  ASSERT_NO_FATAL_FAILURES(CreateCrosDisk(32 * kGibibyte, &disk));

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
    ASSERT_OK(gpt->AddPartition(part.name, part.type, GetRandomGuid(), part.start, part.length, 0),
              "%s", part.name);
  }
  ASSERT_OK(gpt->Sync());


  // Create CrOS device partitioner and initialise partition tables.
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_NO_FATAL_FAILURES(CreatePartitioner(disk.get(), &partitioner));
  ASSERT_OK(partitioner->InitPartitionTables());

  // Ensure the final partition layout looks like we expect it to.
  ASSERT_NO_FATAL_FAILURES(CreateGptDevice(disk.get(), &gpt));
  const std::array<PartitionDescription, 4> partitions_at_end{
      PartitionDescription{GUID_ZIRCON_A_NAME, kCrosKernelType, 0x82B, 0x20000},
      PartitionDescription{GUID_ZIRCON_B_NAME, kCrosKernelType, 0x2082B, 0x20000},
      PartitionDescription{GUID_ZIRCON_R_NAME, kCrosKernelType, 0x4082B, 0x20000},
      PartitionDescription{GUID_FVM_NAME, kFvmType, 0x6082B, 0x2000000},
  };

  ASSERT_NO_FATAL_FAILURES(EnsurePartitionsMatch(gpt.get(), partitions_at_end));

  // Make sure we can find the important partitions.
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA), nullptr));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB), nullptr));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR), nullptr));
  EXPECT_OK(
      partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager), nullptr));

}

class FixedDevicePartitionerTests : public zxtest::Test {
 protected:
  FixedDevicePartitionerTests() {
    IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");
    args.disable_block_watcher = false;
    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));

    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "misc/ramctl", &fd));
  }

  IsolatedDevmgr devmgr_;
};

TEST_F(FixedDevicePartitionerTests, UseBlockInterfaceTest) {
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(
      paver::FixedDevicePartitioner::Initialize(devmgr_.devfs_root().duplicate(), &partitioner));
  ASSERT_FALSE(partitioner->IsFvmWithinFtl());
}

TEST_F(FixedDevicePartitionerTests, AddPartitionTest) {
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(
      paver::FixedDevicePartitioner::Initialize(devmgr_.devfs_root().duplicate(), &partitioner));
  ASSERT_EQ(partitioner->AddPartition(PartitionSpec(paver::Partition::kZirconB), nullptr),
            ZX_ERR_NOT_SUPPORTED);
}

TEST_F(FixedDevicePartitionerTests, WipeFvmTest) {
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(
      paver::FixedDevicePartitioner::Initialize(devmgr_.devfs_root().duplicate(), &partitioner));
  ASSERT_OK(partitioner->WipeFvm());
}

TEST_F(FixedDevicePartitionerTests, FinalizePartitionTest) {
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(
      paver::FixedDevicePartitioner::Initialize(devmgr_.devfs_root().duplicate(), &partitioner));

  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kBootloader)));
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

  auto partitioner = paver::DevicePartitioner::Create(devmgr_.devfs_root().duplicate(),
                                                      zx::channel(), paver::Arch::kArm64);
  ASSERT_NE(partitioner.get(), nullptr);

  std::unique_ptr<paver::PartitionClient> partition;
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloader), &partition));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA), &partition));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB), &partition));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR), &partition));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaA), &partition));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaB), &partition));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaR), &partition));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager),
                                       &partition));
}

TEST_F(FixedDevicePartitionerTests, SupportsPartitionTest) {
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(
      paver::FixedDevicePartitioner::Initialize(devmgr_.devfs_root().duplicate(), &partitioner));

  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloader)));
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

class SherlockPartitionerTests : public zxtest::Test {
 protected:
  SherlockPartitionerTests() {
    IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");
    args.disable_block_watcher = false;
    args.board_name = "sherlock";
    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));

    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "misc/ramctl", &fd));
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform", &fd));
  }

  IsolatedDevmgr devmgr_;
};

// TODO(fxb/42894): Re-enable after de-flaking
TEST_F(SherlockPartitionerTests, DISABLED_InitializeWithoutGptFails) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, &gpt_dev));

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_NE(paver::SherlockPartitioner::Initialize(devmgr_.devfs_root().duplicate(), std::nullopt,
                                                   &partitioner),
            ZX_OK);
}

TEST_F(SherlockPartitionerTests, DISABLED_InitializeWithoutFvmFails) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, &gpt_dev));

  // Set up a valid GPT.
  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_OK(gpt::GptDevice::Create(gpt_dev->fd(), kBlockSize, kBlockCount, &gpt));
  ASSERT_OK(gpt->Sync());

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_NE(paver::SherlockPartitioner::Initialize(devmgr_.devfs_root().duplicate(), std::nullopt,
                                                   &partitioner),
            ZX_OK);
}

TEST_F(SherlockPartitionerTests, DISABLED_AddPartitionNotSupported) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = (1LU << 26) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kBlockCount, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(paver::SherlockPartitioner::Initialize(devmgr_.devfs_root().duplicate(),
                                                   std::move(gpt_fd), &partitioner));

  ASSERT_STATUS(partitioner->AddPartition(PartitionSpec(paver::Partition::kZirconB), nullptr),
                ZX_ERR_NOT_SUPPORTED);
}

TEST_F(SherlockPartitionerTests, DISABLED_InitializePartitionTable) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockSize = 512;
  constexpr uint64_t kBlockCount = 0x748034;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kBlockCount, kBlockSize, &gpt_dev));

  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_OK(gpt::GptDevice::Create(gpt_dev->fd(), kBlockSize, kBlockCount, &gpt));
  ASSERT_OK(gpt->Sync());

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
    ASSERT_OK(gpt->AddPartition(part.name, part.type, GetRandomGuid(), part.start, part.length, 0),
              "%s", part.name);
  }
  ASSERT_OK(gpt->Sync());

  fdio_cpp::UnownedFdioCaller caller(gpt_dev->fd());
  auto result = ::llcpp::fuchsia::device::Controller::Call::Rebind(
      caller.channel(), fidl::StringView("/boot/driver/gpt.so"));
  ASSERT_TRUE(result.ok());
  ASSERT_FALSE(result->result.is_err());

  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(paver::SherlockPartitioner::Initialize(devmgr_.devfs_root().duplicate(),
                                                   std::move(gpt_fd), &partitioner));

  ASSERT_OK(partitioner->InitPartitionTables());

  ASSERT_OK(gpt::GptDevice::Create(gpt_dev->fd(), kBlockSize, kBlockCount, &gpt));

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
  std::unique_ptr<paver::PartitionClient> partition;
  ASSERT_NE(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloader), &partition),
            ZX_OK);
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA), &partition));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB), &partition));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR), &partition));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kAbrMeta), &partition));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaA), &partition));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaB), &partition));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaR), &partition));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager),
                                       &partition));
}

TEST_F(SherlockPartitionerTests, DISABLED_FindBootloader) {
  std::unique_ptr<BlockDevice> gpt_dev, boot0_dev, boot1_dev;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kBlockCount, kBlockSize, &gpt_dev));
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kBoot0Type, kBlockCount, kBlockSize, &boot0_dev));
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kBoot1Type, kBlockCount, kBlockSize, &boot1_dev));

  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_OK(gpt::GptDevice::Create(gpt_dev->fd(), kBlockSize, kBlockCount, &gpt));
  ASSERT_OK(gpt->Sync());

  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(paver::SherlockPartitioner::Initialize(devmgr_.devfs_root().duplicate(),
                                                   std::move(gpt_fd), &partitioner));

  std::unique_ptr<paver::PartitionClient> partition;
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloader), &partition));
}

TEST_F(SherlockPartitionerTests, DISABLED_SupportsPartition) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = (1LU << 26) / kBlockSize;
  ASSERT_NO_FATAL_FAILURES(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kBlockCount, &gpt_dev));
  fbl::unique_fd gpt_fd(dup(gpt_dev->fd()));

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_OK(paver::SherlockPartitioner::Initialize(devmgr_.devfs_root().duplicate(),
                                                   std::move(gpt_fd), &partitioner));

  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloader)));
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

TEST(AstroPartitionerTests, IsFvmWithinFtl) {
  std::unique_ptr<SkipBlockDevice> device;
  ASSERT_NO_FATAL_FAILURES(SkipBlockDevice::Create(kNandInfo, &device));

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_EQ(paver::AstroPartitioner::Initialize(device->devfs_root(), &partitioner), ZX_OK);
  ASSERT_TRUE(partitioner->IsFvmWithinFtl());
}

TEST(AstroPartitionerTests, ChooseAstroPartitioner) {
  std::unique_ptr<SkipBlockDevice> device;
  SkipBlockDevice::Create(kNandInfo, &device);
  auto devfs_root = device->devfs_root();
  std::unique_ptr<BlockDevice> zircon_a;
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devfs_root, kZirconAType, &zircon_a));

  auto partitioner =
      paver::DevicePartitioner::Create(std::move(devfs_root), zx::channel(), paver::Arch::kArm64);
  ASSERT_NE(partitioner.get(), nullptr);
  ASSERT_TRUE(partitioner->IsFvmWithinFtl());
}

TEST(AstroPartitionerTests, AddPartitionTest) {
  std::unique_ptr<SkipBlockDevice> device;
  SkipBlockDevice::Create(kNandInfo, &device);

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_EQ(paver::AstroPartitioner::Initialize(device->devfs_root(), &partitioner), ZX_OK);
  ASSERT_EQ(partitioner->AddPartition(PartitionSpec(paver::Partition::kZirconB), nullptr),
            ZX_ERR_NOT_SUPPORTED);
}

TEST(AstroPartitionerTests, WipeFvmTest) {
  std::unique_ptr<SkipBlockDevice> device;
  SkipBlockDevice::Create(kNandInfo, &device);

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_EQ(paver::AstroPartitioner::Initialize(device->devfs_root(), &partitioner), ZX_OK);
  ASSERT_OK(partitioner->WipeFvm());
}

TEST(AstroPartitionerTests, FinalizePartitionTest) {
  std::unique_ptr<SkipBlockDevice> device;
  SkipBlockDevice::Create(kNandInfo, &device);

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_EQ(paver::AstroPartitioner::Initialize(device->devfs_root(), &partitioner), ZX_OK);

  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kBootloader)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kZirconA)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kZirconB)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kZirconR)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kVbMetaA)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kVbMetaB)));
  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kVbMetaR)));
}

TEST(AstroPartitionerTests, FindPartitionTest) {
  std::unique_ptr<SkipBlockDevice> device;
  SkipBlockDevice::Create(kNandInfo, &device);
  auto devfs_root = device->devfs_root();
  std::unique_ptr<BlockDevice> fvm;
  ASSERT_NO_FATAL_FAILURES(BlockDevice::Create(devfs_root, kFvmType, &fvm));

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_EQ(paver::AstroPartitioner::Initialize(std::move(devfs_root), &partitioner), ZX_OK);

  std::unique_ptr<paver::PartitionClient> partition;
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloader), &partition));
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA), &partition));
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB), &partition));
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR), &partition));
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaA), &partition));
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaB), &partition));
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaR), &partition));

  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager),
                                       &partition));
}

TEST(AstroPartitionerTests, SupportsPartition) {
  std::unique_ptr<SkipBlockDevice> device;
  SkipBlockDevice::Create(kNandInfo, &device);

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_EQ(paver::AstroPartitioner::Initialize(device->devfs_root(), &partitioner), ZX_OK);

  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloader)));
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

class As370PartitionerTests : public zxtest::Test {
 protected:
  As370PartitionerTests() {
    IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");
    args.disable_block_watcher = false;
    args.board_name = "visalia";
    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));

    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform", &fd));
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "misc/ramctl", &fd));
  }

  IsolatedDevmgr devmgr_;
};

TEST_F(As370PartitionerTests, IsFvmWithinFtl) {
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_EQ(paver::As370Partitioner::Initialize(devmgr_.devfs_root().duplicate(), &partitioner),
            ZX_OK);
  ASSERT_TRUE(partitioner->IsFvmWithinFtl());
}

TEST_F(As370PartitionerTests, ChooseAs370Partitioner) {
  auto partitioner = paver::DevicePartitioner::Create(devmgr_.devfs_root().duplicate(),
                                                      zx::channel(), paver::Arch::kArm64);
  ASSERT_NE(partitioner.get(), nullptr);
  ASSERT_TRUE(partitioner->IsFvmWithinFtl());
}

TEST_F(As370PartitionerTests, AddPartitionTest) {
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_EQ(paver::As370Partitioner::Initialize(devmgr_.devfs_root().duplicate(), &partitioner),
            ZX_OK);
  ASSERT_EQ(partitioner->AddPartition(PartitionSpec(paver::Partition::kZirconB), nullptr),
            ZX_ERR_NOT_SUPPORTED);
}

TEST_F(As370PartitionerTests, WipeFvmTest) {
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_EQ(paver::As370Partitioner::Initialize(devmgr_.devfs_root().duplicate(), &partitioner),
            ZX_OK);
  ASSERT_OK(partitioner->WipeFvm());
}

TEST_F(As370PartitionerTests, FinalizePartitionTest) {
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_EQ(paver::As370Partitioner::Initialize(devmgr_.devfs_root().duplicate(), &partitioner),
            ZX_OK);

  ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kBootloader)));
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

  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_EQ(paver::As370Partitioner::Initialize(devmgr_.devfs_root().duplicate(), &partitioner),
            ZX_OK);

  std::unique_ptr<paver::PartitionClient> partition;
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager),
                                       &partition));
}

TEST_F(As370PartitionerTests, SupportsPartition) {
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_EQ(paver::As370Partitioner::Initialize(devmgr_.devfs_root().duplicate(), &partitioner),
            ZX_OK);

  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloader)));
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
