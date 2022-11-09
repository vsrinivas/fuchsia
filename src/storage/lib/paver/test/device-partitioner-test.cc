// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/device-partitioner.h"

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/stdcompat/span.h>
#include <lib/sys/component/cpp/service_client.h>
#include <zircon/boot/image.h>
#include <zircon/errors.h>
#include <zircon/hw/gpt.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <array>
#include <iostream>
#include <memory>
#include <string_view>
#include <utility>

#include <fbl/unique_fd.h>
#include <gpt/cros.h>
#include <gpt/gpt.h>
#include <soc/aml-common/aml-guid.h>
#include <zxtest/zxtest.h>

#include "src/storage/lib/paver/as370.h"
#include "src/storage/lib/paver/astro.h"
#include "src/storage/lib/paver/chromebook-x64.h"
#include "src/storage/lib/paver/luis.h"
#include "src/storage/lib/paver/nelson.h"
#include "src/storage/lib/paver/pinecrest.h"
#include "src/storage/lib/paver/sherlock.h"
#include "src/storage/lib/paver/test/test-utils.h"
#include "src/storage/lib/paver/utils.h"
#include "src/storage/lib/paver/vim3.h"
#include "src/storage/lib/paver/x64.h"

namespace paver {
extern zx_duration_t g_wipe_timeout;
}

namespace {

constexpr fidl::UnownedClientEnd<fuchsia_io::Directory> kInvalidSvcRoot =
    fidl::UnownedClientEnd<fuchsia_io::Directory>(ZX_HANDLE_INVALID);

constexpr uint64_t kMebibyte{UINT64_C(1024) * 1024};
constexpr uint64_t kGibibyte{kMebibyte * 1024};
constexpr uint64_t kTebibyte{kGibibyte * 1024};

using device_watcher::RecursiveWaitForFile;
using driver_integration_test::IsolatedDevmgr;
using paver::BlockWatcherPauser;
using paver::PartitionSpec;

// New Type GUID's
constexpr uint8_t kDurableBootType[GPT_GUID_LEN] = GPT_DURABLE_BOOT_TYPE_GUID;
constexpr uint8_t kVbMetaType[GPT_GUID_LEN] = GPT_VBMETA_ABR_TYPE_GUID;
constexpr uint8_t kZirconType[GPT_GUID_LEN] = GPT_ZIRCON_ABR_TYPE_GUID;
constexpr uint8_t kNewFvmType[GPT_GUID_LEN] = GPT_FVM_TYPE_GUID;

// Legacy Type GUID's
constexpr uint8_t kBootloaderType[GPT_GUID_LEN] = GUID_BOOTLOADER_VALUE;
constexpr uint8_t kEfiType[GPT_GUID_LEN] = GUID_EFI_VALUE;
constexpr uint8_t kCrosKernelType[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
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
constexpr uint8_t kStateLinuxGuid[GPT_GUID_LEN] = GUID_LINUX_FILESYSTEM_DATA_VALUE;

constexpr uint8_t kBoot0Type[GPT_GUID_LEN] = GUID_EMMC_BOOT1_VALUE;
constexpr uint8_t kBoot1Type[GPT_GUID_LEN] = GUID_EMMC_BOOT2_VALUE;

constexpr uint8_t kDummyType[GPT_GUID_LEN] = {0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47,
                                              0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4};

const fbl::unique_fd kDummyDevice;

fuchsia_hardware_nand::wire::RamNandInfo NandInfo() {
  return {
      .nand_info =
          {
              .page_size = kPageSize,
              .pages_per_block = kPagesPerBlock,
              .num_blocks = kNumBlocks,
              .ecc_bits = 8,
              .oob_size = kOobSize,
              .nand_class = fuchsia_hardware_nand::wire::Class::kPartmap,
              .partition_guid = {},
          },
      .partition_map =
          {
              .device_guid = {},
              .partition_count = 7,
              .partitions =
                  {
                      fuchsia_hardware_nand::wire::Partition{
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
}

// Returns the start address of the given partition in |mapper|, or nullptr if
// the partition doesn't exist in |nand_info|.
uint8_t* PartitionStart(const fzl::VmoMapper& mapper,
                        const fuchsia_hardware_nand::wire::RamNandInfo& nand_info,
                        const std::array<uint8_t, GPT_GUID_LEN> guid) {
  const auto& map = nand_info.partition_map;
  const auto* partitions_begin = map.partitions.begin();
  const auto* partitions_end = partitions_begin + map.partition_count;

  const auto* part = std::find_if(
      partitions_begin, partitions_end, [&guid](const fuchsia_hardware_nand::wire::Partition& p) {
        return memcmp(p.type_guid.data(), guid.data(), guid.size()) == 0;
      });
  if (part == partitions_end) {
    return nullptr;
  }

  return reinterpret_cast<uint8_t*>(mapper.start()) +
         (part->first_block * static_cast<size_t>(kPageSize) * kPagesPerBlock);
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
    *dst++ = static_cast<char>(*src);
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
const gpt_partition_t* FindPartitionWithLabel(const gpt::GptDevice* gpt, std::string_view name) {
  const gpt_partition_t* result = nullptr;

  for (uint32_t i = 0; i < gpt->EntryCount(); i++) {
    zx::result<const gpt_partition_t*> gpt_part = gpt->GetPartition(i);
    if (gpt_part.is_error()) {
      continue;
    }

    // Convert UTF-16 partition label to ASCII.
    char cstring_name[GPT_NAME_LEN + 1] = {};
    utf16_to_cstring(cstring_name, (*gpt_part)->name, GPT_NAME_LEN);
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

    result = *gpt_part;
  }

  return result;
}

// Ensure that the partitions on the device matches the given list.
void EnsurePartitionsMatch(const gpt::GptDevice* gpt,
                           cpp20::span<const PartitionDescription> expected) {
  for (auto& part : expected) {
    const gpt_partition_t* gpt_part = FindPartitionWithLabel(gpt, part.name);
    ASSERT_TRUE(gpt_part != nullptr, "Partition \"%s\" not found", part.name);
    EXPECT_TRUE(memcmp(part.type, gpt_part->type, GPT_GUID_LEN) == 0);
    EXPECT_EQ(part.start, gpt_part->first, "Partition %s wrong start", part.name);
    EXPECT_EQ(part.start + part.length - 1, gpt_part->last);
  }
}

constexpr paver::Partition kUnknownPartition = static_cast<paver::Partition>(1000);

TEST(PartitionName, Bootloader) {
  EXPECT_STREQ(PartitionName(paver::Partition::kBootloaderA, paver::PartitionScheme::kNew),
               GPT_BOOTLOADER_A_NAME);
  EXPECT_STREQ(PartitionName(paver::Partition::kBootloaderB, paver::PartitionScheme::kNew),
               GPT_BOOTLOADER_B_NAME);
  EXPECT_STREQ(PartitionName(paver::Partition::kBootloaderR, paver::PartitionScheme::kNew),
               GPT_BOOTLOADER_R_NAME);
  EXPECT_STREQ(PartitionName(paver::Partition::kBootloaderA, paver::PartitionScheme::kLegacy),
               GUID_EFI_NAME);
  EXPECT_STREQ(PartitionName(paver::Partition::kBootloaderB, paver::PartitionScheme::kLegacy),
               GUID_EFI_NAME);
  EXPECT_STREQ(PartitionName(paver::Partition::kBootloaderR, paver::PartitionScheme::kLegacy),
               GUID_EFI_NAME);
}

TEST(PartitionName, AbrMetadata) {
  EXPECT_STREQ(PartitionName(paver::Partition::kAbrMeta, paver::PartitionScheme::kNew),
               GPT_DURABLE_BOOT_NAME);
  EXPECT_STREQ(PartitionName(paver::Partition::kAbrMeta, paver::PartitionScheme::kLegacy),
               GUID_ABR_META_NAME);
}

TEST(PartitionName, UnknownPartition) {
  // We don't define what is returned in this case, but it shouldn't crash and
  // it should be non-empty.
  EXPECT_STRNE(PartitionName(kUnknownPartition, paver::PartitionScheme::kNew), "");
  EXPECT_STRNE(PartitionName(kUnknownPartition, paver::PartitionScheme::kLegacy), "");
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
  explicit GptDevicePartitionerTests(fbl::String board_name = fbl::String(),
                                     uint32_t block_size = 512)
      : block_size_(block_size) {
    paver::g_wipe_timeout = 0;
    IsolatedDevmgr::Args args;
    args.disable_block_watcher = false;

    args.board_name = std::move(board_name);
    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));

    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform/00:00:2d/ramctl", &fd));
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform", &fd));
  }

  fidl::ClientEnd<fuchsia_io::Directory> GetSvcRoot() { return devmgr_.fshost_svc_dir(); }

  // Create a disk with the default size for a BlockDevice.
  void CreateDisk(std::unique_ptr<BlockDevice>* disk) {
    ASSERT_NO_FATAL_FAILURE(BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, disk));
  }

  // Create a disk with the given size in bytes.
  void CreateDisk(uint64_t bytes, std::unique_ptr<BlockDevice>* disk) {
    ASSERT_TRUE(bytes % block_size_ == 0);
    uint64_t num_blocks = bytes / block_size_;

    ASSERT_NO_FATAL_FAILURE(
        BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, num_blocks, block_size_, disk));
  }

  // Create a disk with the given size in bytes and the given type.
  void CreateDisk(uint64_t bytes, const uint8_t* type, std::unique_ptr<BlockDevice>* disk) {
    ASSERT_TRUE(bytes % block_size_ == 0);
    uint64_t num_blocks = bytes / block_size_;

    ASSERT_NO_FATAL_FAILURE(
        BlockDevice::Create(devmgr_.devfs_root(), type, num_blocks, block_size_, disk));
  }

  // Create a disk with a given size, and allocate some extra room for the GPT
  void CreateDiskWithGpt(uint64_t bytes, std::unique_ptr<BlockDevice>* disk) {
    ASSERT_TRUE(bytes % block_size_ == 0);
    uint64_t num_blocks = bytes / block_size_;

    // Ensure there's always enough space for the GPT.
    num_blocks += kGptBlockCount;

    ASSERT_NO_FATAL_FAILURE(
        BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, num_blocks, block_size_, disk));
  }

  // Create GPT from a device.
  static void CreateGptDevice(BlockDevice* device, std::unique_ptr<gpt::GptDevice>* gpt) {
    // TODO(https://fxbug.dev/112484): this relies on multiplexing.
    zx::result clone =
        component::Clone(device->block_interface(), component::AssumeProtocolComposesNode);
    ASSERT_OK(clone);
    ASSERT_OK(gpt::GptDevice::Create(std::move(clone.value()),
                                     /*blocksize=*/device->block_size(),
                                     /*blocks=*/device->block_count(), gpt));
    ASSERT_OK((*gpt)->Sync());
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
    ASSERT_NO_FATAL_FAILURE(CreateGptDevice(gpt_dev, &gpt));

    for (const auto& part : init_partitions) {
      ASSERT_OK(
          gpt->AddPartition(part.name, part.type, GetRandomGuid(), part.start, part.length, 0),
          "%s", part.name);
    }

    ASSERT_OK(gpt->Sync());

    // TODO(https://fxbug.dev/112484): this relies on multiplexing.
    auto result = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_device::Controller>(
                                     gpt_dev->block_interface().channel()))
                      ->Rebind(fidl::StringView("gpt.so"));
    ASSERT_TRUE(result.ok());
    ASSERT_FALSE(result->is_error());
  }

  void ReadBlocks(const BlockDevice* blk_dev, size_t offset_in_blocks, size_t size_in_blocks,
                  uint8_t* out) const {
    fidl::UnownedClientEnd client = blk_dev->block_interface();
    zx::result owned = component::Clone(client, component::AssumeProtocolComposesNode);
    ASSERT_OK(owned.status_value());
    auto block_client = std::make_unique<paver::BlockPartitionClient>(std::move(owned.value()));

    zx::vmo vmo;
    const size_t vmo_size = size_in_blocks * block_size_;
    ASSERT_OK(zx::vmo::create(vmo_size, 0, &vmo));
    ASSERT_OK(block_client->Read(vmo, vmo_size, offset_in_blocks, 0));
    ASSERT_OK(vmo.read(out, 0, vmo_size));
  }

  void WriteBlocks(const BlockDevice* blk_dev, size_t offset_in_blocks, size_t size_in_blocks,
                   uint8_t* buffer) const {
    fidl::UnownedClientEnd client = blk_dev->block_interface();
    zx::result owned = component::Clone(client, component::AssumeProtocolComposesNode);
    ASSERT_OK(owned.status_value());
    auto block_client = std::make_unique<paver::BlockPartitionClient>(std::move(owned.value()));

    zx::vmo vmo;
    const size_t vmo_size = size_in_blocks * block_size_;
    ASSERT_OK(zx::vmo::create(vmo_size, 0, &vmo));
    ASSERT_OK(vmo.write(buffer, 0, vmo_size));
    ASSERT_OK(block_client->Write(vmo, vmo_size, offset_in_blocks, 0));
  }

  void ValidateBlockContent(const BlockDevice* blk_dev, size_t offset_in_blocks,
                            size_t size_in_blocks, uint8_t value) {
    std::vector<uint8_t> buffer(size_in_blocks * block_size_);
    ASSERT_NO_FATAL_FAILURE(ReadBlocks(blk_dev, offset_in_blocks, size_in_blocks, buffer.data()));
    for (size_t i = 0; i < buffer.size(); i++) {
      ASSERT_EQ(value, buffer[i], "at index: %zu", i);
    }
  }

  IsolatedDevmgr devmgr_;
  const uint32_t block_size_;
};

TEST_F(GptDevicePartitionerTests, AddPartitionAtLargeOffset) {
  std::unique_ptr<BlockDevice> gpt_dev;
  // Create 2TB disk
  ASSERT_NO_FATAL_FAILURE(CreateDisk(2 * kTebibyte, &gpt_dev));

  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURE(CreateGptDevice(gpt_dev.get(), &gpt));

    // Add a dummy partition of large size (~1.9TB)
    ASSERT_OK(
        gpt->AddPartition("dummy-partition", kEfiType, GetRandomGuid(), 0x1000, 0xF0000000, 0),
        "%s", "dummy-partition");

    ASSERT_OK(gpt->Sync());
  }

  // Iniitialize paver gpt device partitioner
  //
  // TODO(https://fxbug.dev/112484): this relies on multiplexing.
  fidl::UnownedClientEnd client = gpt_dev->block_interface();
  zx::result owned = component::Clone(client, component::AssumeProtocolComposesNode);
  ASSERT_OK(owned.status_value());
  fbl::unique_fd gpt_fd;
  ASSERT_OK(fdio_fd_create(owned.value().TakeChannel().release(), gpt_fd.reset_and_get_address()));
  auto status = paver::GptDevicePartitioner::InitializeGpt(devmgr_.devfs_root().duplicate(),
                                                           GetSvcRoot(), gpt_fd);
  ASSERT_OK(status);

  // Check if a partition can be added after the "dummy-partition"
  ASSERT_OK(status->gpt->AddPartition("test", uuid::Uuid(GUID_FVM_VALUE), 15LU * kGibibyte, 0));
}

class EfiDevicePartitionerTests : public GptDevicePartitionerTests {
 protected:
  EfiDevicePartitionerTests() : GptDevicePartitionerTests(fbl::String(), 512) {}

  // Create a DevicePartition for a device.
  zx::result<std::unique_ptr<paver::DevicePartitioner>> CreatePartitioner(
      const fbl::unique_fd& device) {
    fidl::ClientEnd<fuchsia_io::Directory> svc_root = GetSvcRoot();
    return paver::EfiDevicePartitioner::Initialize(devmgr_.devfs_root().duplicate(), svc_root,
                                                   paver::Arch::kX64, std::move(device));
  }
};

TEST_F(EfiDevicePartitionerTests, InitializeWithoutGptFails) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(&gpt_dev));

  ASSERT_NOT_OK(CreatePartitioner(kDummyDevice));
}

TEST_F(EfiDevicePartitionerTests, InitializeWithoutFvmSucceeds) {
  std::unique_ptr<BlockDevice> gpt_dev;
  // 64GiB disk.
  constexpr uint64_t kBlockCount = (64LU << 30) / kBlockSize;
  ASSERT_NO_FATAL_FAILURE(
      BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kBlockCount, &gpt_dev));

  // Set up a valid GPT.
  //
  // TODO(https://fxbug.dev/112484): this relies on multiplexing.
  zx::result clone =
      component::Clone(gpt_dev->block_interface(), component::AssumeProtocolComposesNode);
  ASSERT_OK(clone);
  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_OK(gpt::GptDevice::Create(std::move(clone.value()), kBlockSize, kBlockCount, &gpt));
  ASSERT_OK(gpt->Sync());

  ASSERT_OK(CreatePartitioner(kDummyDevice));
}

TEST_F(EfiDevicePartitionerTests, InitializeTwoCandidatesWithoutFvmFails) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(&gpt_dev));

  // Set up a valid GPT.
  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_NO_FATAL_FAILURE(CreateGptDevice(gpt_dev.get(), &gpt));

  std::unique_ptr<BlockDevice> gpt_dev2;
  ASSERT_NO_FATAL_FAILURE(BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, &gpt_dev2));

  // Set up a valid GPT.
  //
  // TODO(https://fxbug.dev/112484): this relies on multiplexing.
  zx::result clone =
      component::Clone(gpt_dev->block_interface(), component::AssumeProtocolComposesNode);
  ASSERT_OK(clone);
  std::unique_ptr<gpt::GptDevice> gpt2;
  ASSERT_OK(gpt::GptDevice::Create(std::move(clone.value()), kBlockSize, kBlockCount, &gpt2));
  ASSERT_OK(gpt2->Sync());

  ASSERT_NOT_OK(CreatePartitioner(kDummyDevice));
}

zx::result<fbl::unique_fd> fd(const BlockDevice& gpt_dev) {
  // TODO(https://fxbug.dev/112484): this relies on multiplexing.
  fidl::UnownedClientEnd client = gpt_dev.block_interface();
  zx::result owned = component::Clone(client, component::AssumeProtocolComposesNode);
  if (owned.is_error()) {
    return owned.take_error();
  }
  fbl::unique_fd fd;
  zx_status_t status =
      fdio_fd_create(owned.value().TakeChannel().release(), fd.reset_and_get_address());
  return zx::make_result(status, std::move(fd));
}

TEST_F(EfiDevicePartitionerTests, AddPartitionZirconB) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDiskWithGpt(128 * kMebibyte, &gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);

  ASSERT_OK(status->AddPartition(PartitionSpec(paver::Partition::kZirconB)));
}

TEST_F(EfiDevicePartitionerTests, AddPartitionFvm) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDiskWithGpt(56 * kGibibyte, &gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);

  ASSERT_OK(status->AddPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
}

TEST_F(EfiDevicePartitionerTests, AddPartitionTooSmall) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(&gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);

  ASSERT_NOT_OK(status->AddPartition(PartitionSpec(paver::Partition::kZirconB)));
}

TEST_F(EfiDevicePartitionerTests, AddedPartitionIsFindable) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDiskWithGpt(128 * kMebibyte, &gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);

  ASSERT_OK(status->AddPartition(PartitionSpec(paver::Partition::kZirconB)));
  ASSERT_OK(status->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
  ASSERT_NOT_OK(status->FindPartition(PartitionSpec(paver::Partition::kZirconA)));
}

TEST_F(EfiDevicePartitionerTests, InitializePartitionsWithoutExplicitDevice) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDiskWithGpt(56 * kGibibyte, &gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);

  ASSERT_OK(status->AddPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
  status.value().reset();

  // Note that this time we don't pass in a block device fd.
  ASSERT_OK(CreatePartitioner(kDummyDevice));
}

TEST_F(EfiDevicePartitionerTests, InitializeWithMultipleCandidateGPTsFailsWithoutExplicitDevice) {
  std::unique_ptr<BlockDevice> gpt_dev1, gpt_dev2;
  ASSERT_NO_FATAL_FAILURE(CreateDiskWithGpt(56 * kGibibyte, &gpt_dev1));
  zx::result gpt_fd1 = fd(*gpt_dev1);
  ASSERT_OK(gpt_fd1.status_value());

  auto status = CreatePartitioner(gpt_fd1.value());
  ASSERT_OK(status);

  ASSERT_OK(status->AddPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
  status.value().reset();

  ASSERT_NO_FATAL_FAILURE(CreateDiskWithGpt(56 * kGibibyte, &gpt_dev2));
  zx::result gpt_fd2 = fd(*gpt_dev2);
  ASSERT_OK(gpt_fd2.status_value());

  auto status2 = CreatePartitioner(gpt_fd2.value());
  ASSERT_OK(status2);
  ASSERT_OK(status2->AddPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
  status2.value().reset();

  // Note that this time we don't pass in a block device fd.
  ASSERT_NOT_OK(CreatePartitioner(kDummyDevice));
}

TEST_F(EfiDevicePartitionerTests, InitializeWithTwoCandidateGPTsSucceedsAfterWipingOne) {
  std::unique_ptr<BlockDevice> gpt_dev1, gpt_dev2;
  ASSERT_NO_FATAL_FAILURE(CreateDiskWithGpt(56 * kGibibyte, &gpt_dev1));
  zx::result gpt_fd1 = fd(*gpt_dev1);
  ASSERT_OK(gpt_fd1.status_value());

  auto status = CreatePartitioner(gpt_fd1.value());
  ASSERT_OK(status);

  ASSERT_OK(status->AddPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
  status.value().reset();

  ASSERT_NO_FATAL_FAILURE(CreateDiskWithGpt(56 * kGibibyte, &gpt_dev2));
  zx::result gpt_fd2 = fd(*gpt_dev2);
  ASSERT_OK(gpt_fd2.status_value());

  auto status2 = CreatePartitioner(gpt_fd2.value());
  ASSERT_OK(status2);
  ASSERT_OK(status2->AddPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
  ASSERT_OK(status2->WipePartitionTables());
  status2.value().reset();

  // Note that this time we don't pass in a block device fd.
  ASSERT_OK(CreatePartitioner(kDummyDevice));
}

TEST_F(EfiDevicePartitionerTests, AddedPartitionRemovedAfterWipePartitions) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDiskWithGpt(128 * kMebibyte, &gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);

  ASSERT_OK(status->AddPartition(PartitionSpec(paver::Partition::kZirconB)));
  ASSERT_OK(status->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
  ASSERT_OK(status->WipePartitionTables());
  ASSERT_NOT_OK(status->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
}

TEST_F(EfiDevicePartitionerTests, FindOldBootloaderPartitionName) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(32 * kGibibyte, &gpt_dev));

  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURE(CreateGptDevice(gpt_dev.get(), &gpt));

    ASSERT_OK(gpt->AddPartition("efi-system", kEfiType, GetRandomGuid(), 0x22, 0x8000, 0));
    ASSERT_OK(gpt->Sync());
  }

  // TODO(https://fxbug.dev/112484): this relies on multiplexing.
  fidl::UnownedClientEnd<fuchsia_device::Controller> channel(
      gpt_dev->block_interface().channel()->get());
  auto result = fidl::WireCall(channel)->Rebind(fidl::StringView("gpt.so"));
  ASSERT_TRUE(result.ok());
  ASSERT_FALSE(result->is_error());

  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());
  auto partitioner = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(partitioner);
  ASSERT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA)));
}

TEST_F(EfiDevicePartitionerTests, InitPartitionTables) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(64 * kGibibyte, &gpt_dev));

  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURE(CreateGptDevice(gpt_dev.get(), &gpt));

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
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());
  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();
  ASSERT_OK(partitioner->InitPartitionTables());

  // Ensure the final partition layout looks like we expect it to.
  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_NO_FATAL_FAILURE(CreateGptDevice(gpt_dev.get(), &gpt));
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
      PartitionDescription{GUID_FVM_NAME, kFvmType, 0xe81ab, 0x7000000},
  };
  ASSERT_NO_FATAL_FAILURE(EnsurePartitionsMatch(gpt.get(), partitions_at_end));

  // Make sure we can find the important partitions.
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kAbrMeta)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
  EXPECT_OK(partitioner->FindPartition(
      PartitionSpec(paver::Partition::kFuchsiaVolumeManager, paver::kOpaqueVolumeContentType)));
  // Check that we found the correct bootloader partition.
  auto status2 = partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA));
  EXPECT_OK(status2);

  auto status3 = status2->GetPartitionSize();
  EXPECT_OK(status3);
  EXPECT_EQ(status3.value(), 0x8000 * block_size_);
}

TEST_F(EfiDevicePartitionerTests, SupportsPartition) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(1 * kGibibyte, &gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
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
  EXPECT_TRUE(partitioner->SupportsPartition(
      PartitionSpec(paver::Partition::kFuchsiaVolumeManager, paver::kOpaqueVolumeContentType)));

  // Unsupported partition type.
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kUnknown)));

  // Unsupported content type.
  EXPECT_FALSE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconA, "foo_type")));
}

TEST_F(EfiDevicePartitionerTests, ValidatePayload) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(1 * kGibibyte, &gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  // Test invalid partitions.
  ASSERT_NOT_OK(partitioner->ValidatePayload(PartitionSpec(paver::Partition::kZirconA),
                                             cpp20::span<uint8_t>()));
  ASSERT_NOT_OK(partitioner->ValidatePayload(PartitionSpec(paver::Partition::kZirconB),
                                             cpp20::span<uint8_t>()));
  ASSERT_NOT_OK(partitioner->ValidatePayload(PartitionSpec(paver::Partition::kZirconR),
                                             cpp20::span<uint8_t>()));

  // Non-kernel partitions are not validated.
  ASSERT_OK(partitioner->ValidatePayload(PartitionSpec(paver::Partition::kAbrMeta),
                                         cpp20::span<uint8_t>()));
}

class CrosDevicePartitionerTests : public GptDevicePartitionerTests {
 protected:
  CrosDevicePartitionerTests() : GptDevicePartitionerTests(fbl::String(), 512) {}

  // Create a DevicePartition for a device.
  void CreatePartitioner(BlockDevice* device,
                         std::unique_ptr<paver::DevicePartitioner>* partitioner) {
    zx::result gpt_fd = fd(*device);
    ASSERT_OK(gpt_fd.status_value());
    fidl::ClientEnd<fuchsia_io::Directory> svc_root = GetSvcRoot();
    zx::result status = paver::CrosDevicePartitioner::Initialize(
        devmgr_.devfs_root().duplicate(), svc_root, paver::Arch::kX64, gpt_fd.value());
    ASSERT_OK(status);
    *partitioner = std::move(status.value());
  }
};

TEST_F(CrosDevicePartitionerTests, InitPartitionTables) {
  std::unique_ptr<BlockDevice> disk;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(64 * kGibibyte, &disk));

  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    // Write initial partitions to disk.
    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURE(CreateGptDevice(disk.get(), &gpt));
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
  ASSERT_NO_FATAL_FAILURE(CreatePartitioner(disk.get(), &partitioner));
  ASSERT_OK(partitioner->InitPartitionTables());

  // Ensure the final partition layout looks like we expect it to.
  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_NO_FATAL_FAILURE(CreateGptDevice(disk.get(), &gpt));
  const std::array<PartitionDescription, 7> partitions_at_end{
      PartitionDescription{GPT_ZIRCON_A_NAME, kCrosKernelType, 0x22, 0x20000},
      PartitionDescription{GPT_ZIRCON_B_NAME, kCrosKernelType, 0x20022, 0x20000},
      PartitionDescription{GPT_ZIRCON_R_NAME, kCrosKernelType, 0x40022, 0x20000},
      PartitionDescription{GPT_VBMETA_A_NAME, kVbMetaType, 0x60022, 0x80},
      PartitionDescription{GPT_VBMETA_B_NAME, kVbMetaType, 0x600a2, 0x80},
      PartitionDescription{GPT_VBMETA_R_NAME, kVbMetaType, 0x60122, 0x80},
      PartitionDescription{GPT_FVM_NAME, kNewFvmType, 0x601a2, 0x7000000},
  };
  ASSERT_NO_FATAL_FAILURE(EnsurePartitionsMatch(gpt.get(), partitions_at_end));

  // Make sure we can find the important partitions.
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
}

TEST_F(CrosDevicePartitionerTests, SupportsPartition) {
  // Create a 32 GiB disk.
  std::unique_ptr<BlockDevice> disk;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(32 * kGibibyte, &disk));

  // Create EFI device partitioner and initialise partition tables.
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_NO_FATAL_FAILURE(CreatePartitioner(disk.get(), &partitioner));

  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_TRUE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
  EXPECT_TRUE(partitioner->SupportsPartition(
      PartitionSpec(paver::Partition::kFuchsiaVolumeManager, paver::kOpaqueVolumeContentType)));

  // Unsupported partition type.
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kUnknown)));
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloaderA)));
  EXPECT_FALSE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kAbrMeta)));

  // Unsupported content type.
  EXPECT_FALSE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kZirconA, "foo_type")));
}

TEST_F(CrosDevicePartitionerTests, ValidatePayload) {
  // Create a 32 GiB disk.
  std::unique_ptr<BlockDevice> disk;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(32 * kGibibyte, &disk));

  // Create EFI device partitioner and initialise partition tables.
  std::unique_ptr<paver::DevicePartitioner> partitioner;
  ASSERT_NO_FATAL_FAILURE(CreatePartitioner(disk.get(), &partitioner));

  // Test invalid partitions.
  ASSERT_NOT_OK(partitioner->ValidatePayload(PartitionSpec(paver::Partition::kZirconA),
                                             cpp20::span<uint8_t>()));
  ASSERT_NOT_OK(partitioner->ValidatePayload(PartitionSpec(paver::Partition::kZirconB),
                                             cpp20::span<uint8_t>()));
  ASSERT_NOT_OK(partitioner->ValidatePayload(PartitionSpec(paver::Partition::kZirconR),
                                             cpp20::span<uint8_t>()));

  // Test valid partition.
  constexpr std::string_view kChromeOsMagicHeader = "CHROMEOS";
  ASSERT_OK(partitioner->ValidatePayload(
      PartitionSpec(paver::Partition::kZirconA),
      cpp20::span<const uint8_t>(reinterpret_cast<const uint8_t*>(kChromeOsMagicHeader.data()),
                                 kChromeOsMagicHeader.size())));

  // Non-kernel partitions are not validated.
  ASSERT_OK(partitioner->ValidatePayload(PartitionSpec(paver::Partition::kFuchsiaVolumeManager),
                                         cpp20::span<uint8_t>()));
}

// Get Cros GPT flags for a kernel with the given priority.
uint64_t CrosGptPriorityFlags(uint8_t priority) {
  uint64_t flags = 0;
  ZX_ASSERT(gpt_cros_attr_set_priority(&flags, priority) >= 0);
  return flags;
}

uint8_t CrosGptHighestKernelPriority(const gpt::GptDevice* gpt) {
  uint8_t top_priority = 0;
  for (uint32_t i = 0; i < gpt::kPartitionCount; ++i) {
    zx::result<const gpt_partition_t*> partition_or = gpt->GetPartition(i);
    if (partition_or.is_error()) {
      continue;
    }
    const gpt_partition_t* partition = partition_or.value();
    const uint8_t priority = gpt_cros_attr_get_priority(partition->flags);
    // Ignore anything not of type CROS KERNEL.
    if (uuid::Uuid(partition->type) != uuid::Uuid(GUID_CROS_KERNEL_VALUE)) {
      continue;
    }

    if (priority > top_priority) {
      top_priority = priority;
    }
  }

  return top_priority;
}

TEST_F(CrosDevicePartitionerTests, KernelPriority) {
  // Create a 32 GiB disk.
  std::unique_ptr<BlockDevice> disk;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(64 * kGibibyte, &disk));

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
    ASSERT_NO_FATAL_FAILURE(CreateGptDevice(disk.get(), &gpt));
    ASSERT_OK(gpt->AddPartition("CROS_KERNEL", kCrosKernelType, GetRandomGuid(), 0x1000, 0x1000,
                                CrosGptPriorityFlags(3)));
    ASSERT_OK(gpt->AddPartition("NOT_KERNEL", GetRandomGuid(), GetRandomGuid(), 0x2000, 0x10,
                                CrosGptPriorityFlags(7)));

    ASSERT_OK(gpt->Sync());
  }

  // Partition the disk, which will add ChromeOS partitions and adjust priorities.
  {
    std::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_NO_FATAL_FAILURE(CreatePartitioner(disk.get(), &partitioner));
    ASSERT_OK(partitioner->InitPartitionTables());
    ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kZirconA)));
  }

  // Ensure that the "zircon-a" kernel has the highest priority.
  {
    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURE(CreateGptDevice(disk.get(), &gpt));
    const gpt_partition_t* zircon_a_partition =
        FindPartitionWithLabel(gpt.get(), GPT_ZIRCON_A_NAME);
    ASSERT_TRUE(zircon_a_partition != nullptr);
    EXPECT_EQ(gpt_cros_attr_get_priority(zircon_a_partition->flags),
              CrosGptHighestKernelPriority(gpt.get()));
  }

  // Partition the disk again.
  {
    std::unique_ptr<paver::DevicePartitioner> partitioner;
    ASSERT_NO_FATAL_FAILURE(CreatePartitioner(disk.get(), &partitioner));
    ASSERT_OK(partitioner->FinalizePartition(PartitionSpec(paver::Partition::kZirconA)));
  }

  // Ensure that the "zircon-a" kernel still has the highest priority.
  {
    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURE(CreateGptDevice(disk.get(), &gpt));
    const gpt_partition_t* zircon_a_partition =
        FindPartitionWithLabel(gpt.get(), GPT_ZIRCON_A_NAME);
    ASSERT_TRUE(zircon_a_partition != nullptr);
    EXPECT_EQ(gpt_cros_attr_get_priority(zircon_a_partition->flags),
              CrosGptHighestKernelPriority(gpt.get()));
  }
}

class FixedDevicePartitionerTests : public zxtest::Test {
 protected:
  FixedDevicePartitionerTests() {
    IsolatedDevmgr::Args args;
    args.disable_block_watcher = false;

    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));

    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform/00:00:2d/ramctl", &fd));
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
  ASSERT_NO_FATAL_FAILURE(BlockDevice::Create(devmgr_.devfs_root(), kBootloaderType, &bootloader));
  ASSERT_NO_FATAL_FAILURE(BlockDevice::Create(devmgr_.devfs_root(), kZirconAType, &zircon_a));
  ASSERT_NO_FATAL_FAILURE(BlockDevice::Create(devmgr_.devfs_root(), kZirconBType, &zircon_b));
  ASSERT_NO_FATAL_FAILURE(BlockDevice::Create(devmgr_.devfs_root(), kZirconRType, &zircon_r));
  ASSERT_NO_FATAL_FAILURE(BlockDevice::Create(devmgr_.devfs_root(), kVbMetaAType, &vbmeta_a));
  ASSERT_NO_FATAL_FAILURE(BlockDevice::Create(devmgr_.devfs_root(), kVbMetaBType, &vbmeta_b));
  ASSERT_NO_FATAL_FAILURE(BlockDevice::Create(devmgr_.devfs_root(), kVbMetaRType, &vbmeta_r));
  ASSERT_NO_FATAL_FAILURE(BlockDevice::Create(devmgr_.devfs_root(), kFvmType, &fvm));

  std::shared_ptr<paver::Context> context = std::make_shared<paver::Context>();
  auto partitioner = paver::DevicePartitionerFactory::Create(
      devmgr_.devfs_root().duplicate(), kInvalidSvcRoot, paver::Arch::kArm64, context);
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
  zx::result<std::unique_ptr<paver::DevicePartitioner>> CreatePartitioner(
      const fbl::unique_fd& device) {
    fidl::ClientEnd<fuchsia_io::Directory> svc_root = GetSvcRoot();
    return paver::SherlockPartitioner::Initialize(devmgr_.devfs_root().duplicate(), svc_root,
                                                  device);
  }
};

TEST_F(SherlockPartitionerTests, InitializeWithoutGptFails) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(&gpt_dev));

  ASSERT_NOT_OK(CreatePartitioner(kDummyDevice));
}

TEST_F(SherlockPartitionerTests, InitializeWithoutFvmSucceeds) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(32 * kGibibyte, &gpt_dev));

  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    // Set up a valid GPT.
    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURE(CreateGptDevice(gpt_dev.get(), &gpt));

    ASSERT_OK(CreatePartitioner(kDummyDevice));
  }
}

TEST_F(SherlockPartitionerTests, AddPartitionNotSupported) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(64 * kMebibyte, &gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);

  ASSERT_STATUS(status->AddPartition(PartitionSpec(paver::Partition::kZirconB)),
                ZX_ERR_NOT_SUPPORTED);
}

TEST_F(SherlockPartitionerTests, InitializePartitionTable) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = 0x748034;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * block_size_, &gpt_dev));
  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURE(CreateGptDevice(gpt_dev.get(), &gpt));

    const PartitionDescription kStartingPartitions[] = {
        {"bootloader", kDummyType, 0x22, 0x2000},   {"reserved", kDummyType, 0x12000, 0x20000},
        {"env", kDummyType, 0x36000, 0x4000},       {"fts", kDummyType, 0x3E000, 0x2000},
        {"factory", kDummyType, 0x44000, 0x10000},  {"recovery", kDummyType, 0x58000, 0x10000},
        {"boot", kDummyType, 0x6C000, 0x10000},     {"system", kDummyType, 0x80000, 0x278000},
        {"cache", kDummyType, 0x2FC000, 0x400000},  {"fct", kDummyType, 0x700000, 0x20000},
        {"sysconfig", kDummyType, 0x724000, 0x800}, {"migration", kDummyType, 0x728800, 0x3800},
        {"buf", kDummyType, 0x730000, 0x18000},
    };

    for (const auto& part : cpp20::span(kStartingPartitions)) {
      ASSERT_OK(
          gpt->AddPartition(part.name, part.type, GetRandomGuid(), part.start, part.length, 0),
          "%s", part.name);
    }

    ASSERT_OK(gpt->Sync());
  }

  // TODO(https://fxbug.dev/112484): this relies on multiplexing.
  auto result = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_device::Controller>(
                                   gpt_dev->block_interface().channel()))
                    ->Rebind(fidl::StringView("gpt.so"));
  ASSERT_TRUE(result.ok());
  ASSERT_FALSE(result->is_error());

  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());
  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  ASSERT_OK(partitioner->InitPartitionTables());

  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_NO_FATAL_FAILURE(CreateGptDevice(gpt_dev.get(), &gpt));

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
  ASSERT_NO_FATAL_FAILURE(EnsurePartitionsMatch(gpt.get(), kFinalPartitions));

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

TEST_F(SherlockPartitionerTests, FindPartitionNewGuids) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = 0x748034;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * block_size_, &gpt_dev));

  // partition size / location is arbitrary
  const std::vector<PartitionDescription> kSherlockNewPartitions = {
      {GPT_DURABLE_BOOT_NAME, kDurableBootType, 0x10400, 0x10000},
      {GPT_VBMETA_A_NAME, kVbMetaType, 0x20400, 0x10000},
      {GPT_VBMETA_B_NAME, kVbMetaType, 0x30400, 0x10000},
      {GPT_VBMETA_R_NAME, kVbMetaType, 0x40400, 0x10000},
      {GPT_ZIRCON_A_NAME, kZirconType, 0x50400, 0x10000},
      {GPT_ZIRCON_B_NAME, kZirconType, 0x60400, 0x10000},
      {GPT_ZIRCON_R_NAME, kZirconType, 0x70400, 0x10000},
      {GPT_FVM_NAME, kNewFvmType, 0x80400, 0x10000},
  };
  ASSERT_NO_FATAL_FAILURE(InitializeStartingGPTPartitions(gpt_dev.get(), kSherlockNewPartitions));

  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());
  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

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

TEST_F(SherlockPartitionerTests, FindPartitionNewGuidsWithWrongTypeGUIDS) {
  // Due to a bootloader bug (b/173801312), the type GUID's may be reset in certain conditions.
  // This test verifies that the sherlock partitioner only looks at the partition name.

  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = 0x748034;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * block_size_, &gpt_dev));

  const std::vector<PartitionDescription> kSherlockNewPartitions = {
      {GPT_DURABLE_BOOT_NAME, kStateLinuxGuid, 0x10400, 0x10000},
      {GPT_VBMETA_A_NAME, kStateLinuxGuid, 0x20400, 0x10000},
      {GPT_VBMETA_B_NAME, kStateLinuxGuid, 0x30400, 0x10000},
      {GPT_VBMETA_R_NAME, kStateLinuxGuid, 0x40400, 0x10000},
      {GPT_ZIRCON_A_NAME, kStateLinuxGuid, 0x50400, 0x10000},
      {GPT_ZIRCON_B_NAME, kStateLinuxGuid, 0x60400, 0x10000},
      {GPT_ZIRCON_R_NAME, kStateLinuxGuid, 0x70400, 0x10000},
      {GPT_FVM_NAME, kStateLinuxGuid, 0x80400, 0x10000},
  };
  ASSERT_NO_FATAL_FAILURE(InitializeStartingGPTPartitions(gpt_dev.get(), kSherlockNewPartitions));

  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());
  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

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

TEST_F(SherlockPartitionerTests, FindPartitionSecondary) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = 0x748034;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * block_size_, &gpt_dev));

  const std::vector<PartitionDescription> kSherlockNewPartitions = {
      {GPT_DURABLE_BOOT_NAME, kStateLinuxGuid, 0x10400, 0x10000},
      {GPT_VBMETA_A_NAME, kStateLinuxGuid, 0x20400, 0x10000},
      {GPT_VBMETA_B_NAME, kStateLinuxGuid, 0x30400, 0x10000},
      // Removed vbmeta_r to validate that it is not found
      {"boot", kStateLinuxGuid, 0x50400, 0x10000},
      {"system", kStateLinuxGuid, 0x60400, 0x10000},
      {"recovery", kStateLinuxGuid, 0x70400, 0x10000},
      {GPT_FVM_NAME, kStateLinuxGuid, 0x80400, 0x10000},
  };
  ASSERT_NO_FATAL_FAILURE(InitializeStartingGPTPartitions(gpt_dev.get(), kSherlockNewPartitions));

  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());
  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  // Make sure we can find the important partitions.
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kAbrMeta)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_NOT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
}

TEST_F(SherlockPartitionerTests, ShouldNotFindPartitionBoot) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = 0x748034;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * block_size_, &gpt_dev));

  const std::vector<PartitionDescription> kSherlockNewPartitions = {
      {"bootloader", kStateLinuxGuid, 0x10400, 0x10000},
  };
  ASSERT_NO_FATAL_FAILURE(InitializeStartingGPTPartitions(gpt_dev.get(), kSherlockNewPartitions));

  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());
  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  // Make sure we can't find zircon_a, which is aliased to "boot". The GPT logic would
  // previously only check prefixes, so "boot" would match with "bootloader".
  EXPECT_NOT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA)));
}

TEST_F(SherlockPartitionerTests, FindBootloader) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(&gpt_dev));

  std::unique_ptr<gpt::GptDevice> gpt;
  ASSERT_NO_FATAL_FAILURE(CreateGptDevice(gpt_dev.get(), &gpt));

  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());
  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  // No boot0/boot1 yet, we shouldn't be able to find the bootloader.
  ASSERT_NOT_OK(
      partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA, "skip_metadata")));

  std::unique_ptr<BlockDevice> boot0_dev, boot1_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * kBlockSize, kBoot0Type, &boot0_dev));
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * kBlockSize, kBoot1Type, &boot1_dev));

  // Now it should succeed.
  ASSERT_OK(
      partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA, "skip_metadata")));
}

TEST_F(SherlockPartitionerTests, SupportsPartition) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(64 * kMebibyte, &gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
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
  zx::result<std::unique_ptr<paver::DevicePartitioner>> CreatePartitioner(
      const fbl::unique_fd& device) {
    fidl::ClientEnd<fuchsia_io::Directory> svc_root = GetSvcRoot();
    return paver::LuisPartitioner::Initialize(devmgr_.devfs_root().duplicate(), svc_root, device);
  }
};

TEST_F(LuisPartitionerTests, InitializeWithoutGptFails) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(&gpt_dev));

  ASSERT_NOT_OK(CreatePartitioner(kDummyDevice));
}

TEST_F(LuisPartitionerTests, InitializeWithoutFvmSucceeds) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(32 * kGibibyte, &gpt_dev));

  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    // Set up a valid GPT.
    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURE(CreateGptDevice(gpt_dev.get(), &gpt));

    ASSERT_OK(CreatePartitioner(kDummyDevice));
  }
}

TEST_F(LuisPartitionerTests, AddPartitionNotSupported) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(64 * kMebibyte, &gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);

  ASSERT_STATUS(status->AddPartition(PartitionSpec(paver::Partition::kZirconB)),
                ZX_ERR_NOT_SUPPORTED);
}

TEST_F(LuisPartitionerTests, FindPartition) {
  std::unique_ptr<BlockDevice> gpt_dev;
  // kBlockCount should be a value large enough to accommodate all partitions and blocks reserved
  // by gpt. The current value is copied from the case of sherlock. As of now, we assume they
  // have the same disk size requirement.
  constexpr uint64_t kBlockCount = 0x748034;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * block_size_, &gpt_dev));

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
      {GPT_FVM_NAME, kDummyType, 0xe0400, 0x10000},
  };
  ASSERT_NO_FATAL_FAILURE(InitializeStartingGPTPartitions(gpt_dev.get(), kLuisStartingPartitions));

  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());
  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  EXPECT_NOT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA)));

  std::unique_ptr<BlockDevice> boot0_dev, boot1_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * kBlockSize, kBoot0Type, &boot0_dev));
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * kBlockSize, kBoot1Type, &boot1_dev));

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
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * block_size_, &gpt_dev));

  const std::vector<PartitionDescription> kStartingPartitions = {
      {GPT_DURABLE_BOOT_NAME, kDummyType, 0x10400, 0x10000},
  };
  ASSERT_NO_FATAL_FAILURE(InitializeStartingGPTPartitions(gpt_dev.get(), kStartingPartitions));
  fidl::ClientEnd<fuchsia_io::Directory> svc_root = GetSvcRoot();
  std::shared_ptr<paver::Context> context;
  EXPECT_OK(paver::LuisAbrClientFactory().New(devmgr_.devfs_root().duplicate(), svc_root, context));
}

TEST_F(LuisPartitionerTests, SupportsPartition) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(64 * kMebibyte, &gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
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

class NelsonPartitionerTests : public GptDevicePartitionerTests {
 protected:
  static constexpr size_t kNelsonBlockSize = 512;
  static constexpr size_t kTplSize = 1024;
  static constexpr size_t kBootloaderSize = paver::kNelsonBL2Size + kTplSize;
  static constexpr uint8_t kBL2ImageValue = 0x01;
  static constexpr uint8_t kTplImageValue = 0x02;
  static constexpr size_t kTplSlotAOffset = 0x3000;
  static constexpr size_t kTplSlotBOffset = 0x4000;
  static constexpr size_t kUserTplBlockCount = 0x1000;

  NelsonPartitionerTests() : GptDevicePartitionerTests("nelson", kNelsonBlockSize) {}

  // Create a DevicePartition for a device.
  zx::result<std::unique_ptr<paver::DevicePartitioner>> CreatePartitioner(
      const fbl::unique_fd& device) {
    fidl::ClientEnd<fuchsia_io::Directory> svc_root = GetSvcRoot();
    return paver::NelsonPartitioner::Initialize(devmgr_.devfs_root().duplicate(), svc_root, device);
  }

  static void CreateBootloaderPayload(zx::vmo* out) {
    fzl::VmoMapper mapper;
    ASSERT_OK(
        mapper.CreateAndMap(kBootloaderSize, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, out));
    uint8_t* start = static_cast<uint8_t*>(mapper.start());
    memset(start, kBL2ImageValue, paver::kNelsonBL2Size);
    memset(start + paver::kNelsonBL2Size, kTplImageValue, kTplSize);
  }

  void TestBootloaderWrite(const PartitionSpec& spec, uint8_t tpl_a_expected,
                           uint8_t tpl_b_expected) {
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    std::unique_ptr<BlockDevice> gpt_dev, boot0, boot1;
    ASSERT_NO_FATAL_FAILURE(InitializeBlockDeviceForBootloaderTest(&gpt_dev, &boot0, &boot1));

    zx::result gpt_fd = fd(*gpt_dev);
    ASSERT_OK(gpt_fd.status_value());

    auto status = CreatePartitioner(gpt_fd.value());
    ASSERT_OK(status);
    std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();
    {
      auto partition_client = partitioner->FindPartition(spec);
      ASSERT_OK(partition_client);

      zx::vmo bootloader_payload;
      ASSERT_NO_FATAL_FAILURE(CreateBootloaderPayload(&bootloader_payload));
      ASSERT_OK(partition_client->Write(bootloader_payload, kBootloaderSize));
    }
    const size_t bl2_blocks = paver::kNelsonBL2Size / block_size_;
    const size_t tpl_blocks = kTplSize / block_size_;

    // info block stays unchanged. assume that storage data initialized as 0.
    ASSERT_NO_FATAL_FAILURE(ValidateBlockContent(boot0.get(), 0, 1, 0));
    ASSERT_NO_FATAL_FAILURE(ValidateBlockContent(boot0.get(), 1, bl2_blocks, kBL2ImageValue));
    ASSERT_NO_FATAL_FAILURE(
        ValidateBlockContent(boot0.get(), 1 + bl2_blocks, tpl_blocks, kTplImageValue));

    // info block stays unchanged
    ASSERT_NO_FATAL_FAILURE(ValidateBlockContent(boot1.get(), 0, 1, 0));
    ASSERT_NO_FATAL_FAILURE(ValidateBlockContent(boot1.get(), 1, bl2_blocks, kBL2ImageValue));
    ASSERT_NO_FATAL_FAILURE(
        ValidateBlockContent(boot1.get(), 1 + bl2_blocks, tpl_blocks, kTplImageValue));

    ASSERT_NO_FATAL_FAILURE(
        ValidateBlockContent(gpt_dev.get(), kTplSlotAOffset, tpl_blocks, tpl_a_expected));
    ASSERT_NO_FATAL_FAILURE(
        ValidateBlockContent(gpt_dev.get(), kTplSlotBOffset, tpl_blocks, tpl_b_expected));
  }

  void TestBootloaderRead(const PartitionSpec& spec, uint8_t tpl_a_data, uint8_t tpl_b_data,
                          zx::result<>* out_status, uint8_t* out) {
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    std::unique_ptr<BlockDevice> gpt_dev, boot0, boot1;
    ASSERT_NO_FATAL_FAILURE(InitializeBlockDeviceForBootloaderTest(&gpt_dev, &boot0, &boot1));

    const size_t bl2_blocks = paver::kNelsonBL2Size / block_size_;
    const size_t tpl_blocks = kTplSize / block_size_;

    // Setup initial storage data
    struct initial_storage_data {
      const BlockDevice* blk_dev;
      uint64_t start_block;
      uint64_t size_in_blocks;
      uint8_t data;
    } initial_storage[] = {
        {boot0.get(), 1, bl2_blocks, kBL2ImageValue},               // bl2 in boot0
        {boot1.get(), 1, bl2_blocks, kBL2ImageValue},               // bl2 in boot1
        {boot0.get(), 1 + bl2_blocks, tpl_blocks, kTplImageValue},  // tpl in boot0
        {boot1.get(), 1 + bl2_blocks, tpl_blocks, kTplImageValue},  // tpl in boot1
        {gpt_dev.get(), kTplSlotAOffset, tpl_blocks, tpl_a_data},   // tpl_a
        {gpt_dev.get(), kTplSlotBOffset, tpl_blocks, tpl_b_data},   // tpl_b
    };
    for (auto& info : initial_storage) {
      std::vector<uint8_t> data(info.size_in_blocks * block_size_, info.data);
      ASSERT_NO_FATAL_FAILURE(
          WriteBlocks(info.blk_dev, info.start_block, info.size_in_blocks, data.data()));
    }

    zx::result gpt_fd = fd(*gpt_dev);
    ASSERT_OK(gpt_fd.status_value());
    auto status = CreatePartitioner(gpt_fd.value());
    ASSERT_OK(status);
    std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

    fzl::OwnedVmoMapper read_buf;
    ASSERT_OK(read_buf.CreateAndMap(kBootloaderSize, "test-read-bootloader"));
    auto partition_client = partitioner->FindPartition(spec);
    ASSERT_OK(partition_client);
    *out_status = partition_client->Read(read_buf.vmo(), kBootloaderSize);
    memcpy(out, read_buf.start(), kBootloaderSize);
  }

  static void ValidateBootloaderRead(const uint8_t* buf, uint8_t expected_bl2,
                                     uint8_t expected_tpl) {
    for (size_t i = 0; i < paver::kNelsonBL2Size; i++) {
      ASSERT_EQ(buf[i], expected_bl2, "bl2 mismatch at idx: %zu", i);
    }

    for (size_t i = 0; i < kTplSize; i++) {
      ASSERT_EQ(buf[i + paver::kNelsonBL2Size], expected_tpl, "tpl mismatch at idx: %zu", i);
    }
  }

  void InitializeBlockDeviceForBootloaderTest(std::unique_ptr<BlockDevice>* gpt_dev,
                                              std::unique_ptr<BlockDevice>* boot0,
                                              std::unique_ptr<BlockDevice>* boot1) {
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    ASSERT_NO_FATAL_FAILURE(CreateDisk(64 * kMebibyte, gpt_dev));
    static const std::vector<PartitionDescription> kNelsonBootloaderTestPartitions = {
        {"tpl_a", kDummyType, kTplSlotAOffset, kUserTplBlockCount},
        {"tpl_b", kDummyType, kTplSlotBOffset, kUserTplBlockCount},
    };
    ASSERT_NO_FATAL_FAILURE(
        InitializeStartingGPTPartitions(gpt_dev->get(), kNelsonBootloaderTestPartitions));

    ASSERT_NO_FATAL_FAILURE(CreateDisk(kUserTplBlockCount * kNelsonBlockSize, kBoot0Type, boot0));
    ASSERT_NO_FATAL_FAILURE(CreateDisk(kUserTplBlockCount * kNelsonBlockSize, kBoot1Type, boot1));
  }
};

TEST_F(NelsonPartitionerTests, InitializeWithoutGptFails) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(&gpt_dev));

  ASSERT_NOT_OK(CreatePartitioner(kDummyDevice));
}

TEST_F(NelsonPartitionerTests, InitializeWithoutFvmSucceeds) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(32 * kGibibyte, &gpt_dev));

  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    // Set up a valid GPT.
    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURE(CreateGptDevice(gpt_dev.get(), &gpt));

    ASSERT_OK(CreatePartitioner(kDummyDevice));
  }
}

TEST_F(NelsonPartitionerTests, AddPartitionNotSupported) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(64 * kMebibyte, &gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);

  ASSERT_STATUS(status->AddPartition(PartitionSpec(paver::Partition::kZirconB)),
                ZX_ERR_NOT_SUPPORTED);
}

TEST_F(NelsonPartitionerTests, FindPartition) {
  std::unique_ptr<BlockDevice> gpt_dev;
  // kBlockCount should be a value large enough to accommodate all partitions and blocks reserved
  // by gpt. The current value is copied from the case of sherlock. The actual size of fvm
  // partition on nelson is yet to be finalized.
  constexpr uint64_t kBlockCount = 0x748034;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * block_size_, &gpt_dev));

  // The initial gpt partitions are randomly chosen and does not necessarily reflect the
  // actual gpt partition layout in product.
  const std::vector<PartitionDescription> kNelsonStartingPartitions = {
      {GUID_ABR_META_NAME, kAbrMetaType, 0x10400, 0x10000},
      {"tpl_a", kDummyType, 0x30400, 0x10000},
      {"tpl_b", kDummyType, 0x40400, 0x10000},
      {"boot_a", kZirconAType, 0x50400, 0x10000},
      {"boot_b", kZirconBType, 0x60400, 0x10000},
      {"system_a", kDummyType, 0x70400, 0x10000},
      {"system_b", kDummyType, 0x80400, 0x10000},
      {GPT_VBMETA_A_NAME, kVbMetaAType, 0x90400, 0x10000},
      {GPT_VBMETA_B_NAME, kVbMetaBType, 0xa0400, 0x10000},
      {"reserved_a", kDummyType, 0xc0400, 0x10000},
      {"reserved_b", kDummyType, 0xd0400, 0x10000},
      {"reserved_c", kVbMetaRType, 0xe0400, 0x10000},
      {"cache", kZirconRType, 0xf0400, 0x10000},
      {"data", kFvmType, 0x100400, 0x10000},

  };
  ASSERT_NO_FATAL_FAILURE(
      InitializeStartingGPTPartitions(gpt_dev.get(), kNelsonStartingPartitions));

  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());
  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  EXPECT_NOT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA)));

  std::unique_ptr<BlockDevice> boot0_dev, boot1_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * kBlockSize, kBoot0Type, &boot0_dev));
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * kBlockSize, kBoot1Type, &boot1_dev));

  // Make sure we can find the important partitions.
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA, "bl2")));
  EXPECT_OK(
      partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA, "bootloader")));
  EXPECT_OK(
      partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderB, "bootloader")));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA, "tpl")));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderB, "tpl")));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kAbrMeta)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
}

TEST_F(NelsonPartitionerTests, CreateAbrClient) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = 0x748034;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * block_size_, &gpt_dev));

  const std::vector<PartitionDescription> kStartingPartitions = {
      {GUID_ABR_META_NAME, kAbrMetaType, 0x10400, 0x10000},
  };
  ASSERT_NO_FATAL_FAILURE(InitializeStartingGPTPartitions(gpt_dev.get(), kStartingPartitions));
  fidl::ClientEnd<fuchsia_io::Directory> svc_root = GetSvcRoot();
  std::shared_ptr<paver::Context> context;
  EXPECT_OK(
      paver::NelsonAbrClientFactory().New(devmgr_.devfs_root().duplicate(), svc_root, context));
}

TEST_F(NelsonPartitionerTests, SupportsPartition) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(64 * kMebibyte, &gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloaderA, "bl2")));
  EXPECT_TRUE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloaderA, "bootloader")));
  EXPECT_TRUE(
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloaderB, "bootloader")));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloaderA, "tpl")));
  EXPECT_TRUE(partitioner->SupportsPartition(PartitionSpec(paver::Partition::kBootloaderB, "tpl")));
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

TEST_F(NelsonPartitionerTests, ValidatePayload) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(64 * kMebibyte, &gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  // Test invalid bootloader payload size.
  std::vector<uint8_t> payload_bl2_size(paver::kNelsonBL2Size);
  ASSERT_NOT_OK(
      partitioner->ValidatePayload(PartitionSpec(paver::Partition::kBootloaderA, "bootloader"),
                                   cpp20::span<uint8_t>(payload_bl2_size)));
  ASSERT_NOT_OK(
      partitioner->ValidatePayload(PartitionSpec(paver::Partition::kBootloaderB, "bootloader"),
                                   cpp20::span<uint8_t>(payload_bl2_size)));

  std::vector<uint8_t> payload_bl2_tpl_size(static_cast<size_t>(2) * 1024 * 1024);
  ASSERT_OK(
      partitioner->ValidatePayload(PartitionSpec(paver::Partition::kBootloaderA, "bootloader"),
                                   cpp20::span<uint8_t>(payload_bl2_tpl_size)));
  ASSERT_OK(
      partitioner->ValidatePayload(PartitionSpec(paver::Partition::kBootloaderB, "bootloader"),
                                   cpp20::span<uint8_t>(payload_bl2_tpl_size)));
}

TEST_F(NelsonPartitionerTests, WriteBootloaderA) {
  TestBootloaderWrite(PartitionSpec(paver::Partition::kBootloaderA, "bootloader"), kTplImageValue,
                      0x00);
}

TEST_F(NelsonPartitionerTests, WriteBootloaderB) {
  TestBootloaderWrite(PartitionSpec(paver::Partition::kBootloaderB, "bootloader"), 0x00,
                      kTplImageValue);
}

TEST_F(NelsonPartitionerTests, ReadBootloaderAFail) {
  auto spec = PartitionSpec(paver::Partition::kBootloaderA, "bootloader");
  std::vector<uint8_t> read_buf(kBootloaderSize);
  zx::result<> status = zx::ok();
  ASSERT_NO_FATAL_FAILURE(TestBootloaderRead(spec, 0x03, kTplImageValue, &status, read_buf.data()));
  ASSERT_NOT_OK(status);
}

TEST_F(NelsonPartitionerTests, ReadBootloaderBFail) {
  auto spec = PartitionSpec(paver::Partition::kBootloaderB, "bootloader");
  std::vector<uint8_t> read_buf(kBootloaderSize);
  zx::result<> status = zx::ok();
  ASSERT_NO_FATAL_FAILURE(TestBootloaderRead(spec, kTplImageValue, 0x03, &status, read_buf.data()));
  ASSERT_NOT_OK(status);
}

TEST_F(NelsonPartitionerTests, ReadBootloaderASucceed) {
  auto spec = PartitionSpec(paver::Partition::kBootloaderA, "bootloader");
  std::vector<uint8_t> read_buf(kBootloaderSize);
  zx::result<> status = zx::ok();
  ASSERT_NO_FATAL_FAILURE(TestBootloaderRead(spec, kTplImageValue, 0x03, &status, read_buf.data()));
  ASSERT_OK(status);
  ASSERT_NO_FATAL_FAILURE(ValidateBootloaderRead(read_buf.data(), kBL2ImageValue, kTplImageValue));
}

TEST_F(NelsonPartitionerTests, ReadBootloaderBSucceed) {
  std::vector<uint8_t> read_buf(kBootloaderSize);
  auto spec = PartitionSpec(paver::Partition::kBootloaderB, "bootloader");
  zx::result<> status = zx::ok();
  ASSERT_NO_FATAL_FAILURE(TestBootloaderRead(spec, 0x03, kTplImageValue, &status, read_buf.data()));
  ASSERT_OK(status);
  ASSERT_NO_FATAL_FAILURE(ValidateBootloaderRead(read_buf.data(), kBL2ImageValue, kTplImageValue));
}

class PinecrestPartitionerTests : public GptDevicePartitionerTests {
 protected:
  PinecrestPartitionerTests() : GptDevicePartitionerTests("pinecrest", 512) {}

  // Create a DevicePartition for a device.
  zx::result<std::unique_ptr<paver::DevicePartitioner>> CreatePartitioner(
      const fbl::unique_fd& device) {
    fidl::ClientEnd<fuchsia_io::Directory> svc_root = GetSvcRoot();
    return paver::PinecrestPartitioner::Initialize(devmgr_.devfs_root().duplicate(), svc_root,
                                                   device);
  }
};

TEST_F(PinecrestPartitionerTests, InitializeWithoutGptFails) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(&gpt_dev));

  ASSERT_NOT_OK(CreatePartitioner(kDummyDevice));
}

TEST_F(PinecrestPartitionerTests, InitializeWithoutFvmSucceeds) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(32 * kGibibyte, &gpt_dev));

  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    // Set up a valid GPT.
    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURE(CreateGptDevice(gpt_dev.get(), &gpt));

    ASSERT_OK(CreatePartitioner(kDummyDevice));
  }
}

TEST_F(PinecrestPartitionerTests, AddPartitionNotSupported) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(64 * kMebibyte, &gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);

  ASSERT_STATUS(status->AddPartition(PartitionSpec(paver::Partition::kZirconB)),
                ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PinecrestPartitionerTests, FindPartitionByGuid) {
  std::unique_ptr<BlockDevice> gpt_dev;
  // kBlockCount should be a value large enough to accommodate all partitions and blocks reserved by
  // gpt. The current value is copied from the case of sherlock.
  constexpr uint64_t kBlockCount = 0x748034;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * block_size_, &gpt_dev));

  // The initial gpt partitions are randomly chosen and does not necessarily reflect the
  // actual gpt partition layout in product.
  const std::vector<PartitionDescription> kPinecrestOldPartitions = {
      {GUID_ABR_META_NAME, kAbrMetaType, 0x10400, 0x10000},
      {"boot_a", kZirconAType, 0x50400, 0x10000},
      {"boot_b", kZirconBType, 0x60400, 0x10000},
      {"system_a", kDummyType, 0x70400, 0x10000},
      {"system_b", kDummyType, 0x80400, 0x10000},
      {GPT_VBMETA_A_NAME, kVbMetaAType, 0x90400, 0x10000},
      {GPT_VBMETA_B_NAME, kVbMetaBType, 0xa0400, 0x10000},
      {"reserved_a", kDummyType, 0xc0400, 0x10000},
      {"reserved_b", kDummyType, 0xd0400, 0x10000},
      {"reserved_c", kVbMetaRType, 0xe0400, 0x10000},
      {"cache", kZirconRType, 0xf0400, 0x10000},
      {"data", kFvmType, 0x100400, 0x10000},

  };
  ASSERT_NO_FATAL_FAILURE(InitializeStartingGPTPartitions(gpt_dev.get(), kPinecrestOldPartitions));

  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());
  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  // Make sure we can find the important partitions.
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kAbrMeta)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
}

TEST_F(PinecrestPartitionerTests, FindPartitionByName) {
  std::unique_ptr<BlockDevice> gpt_dev;
  // kBlockCount should be a value large enough to accommodate all partitions and blocks reserved by
  // gpt. The current value is copied from the case of sherlock.
  constexpr uint64_t kBlockCount = 0x748034;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * block_size_, &gpt_dev));

  // The initial gpt partitions are randomly chosen and does not necessarily reflect the
  // actual gpt partition layout in product.
  const std::vector<PartitionDescription> kPinecrestNewPartitions = {
      {GUID_ABR_META_NAME, kDummyType, 0x10400, 0x10000},
      {GPT_VBMETA_A_NAME, kDummyType, 0x20400, 0x10000},
      {GPT_VBMETA_B_NAME, kDummyType, 0x30400, 0x10000},
      {GPT_VBMETA_R_NAME, kDummyType, 0x40400, 0x10000},
      {GPT_ZIRCON_A_NAME, kDummyType, 0x50400, 0x10000},
      {GPT_ZIRCON_B_NAME, kDummyType, 0x60400, 0x10000},
      {GPT_ZIRCON_R_NAME, kDummyType, 0x70400, 0x10000},
      {GPT_FVM_NAME, kNewFvmType, 0x80400, 0x10000},
  };
  ASSERT_NO_FATAL_FAILURE(InitializeStartingGPTPartitions(gpt_dev.get(), kPinecrestNewPartitions));

  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());
  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  // Make sure we can find the important partitions.
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kAbrMeta)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
}

TEST_F(PinecrestPartitionerTests, SupportsPartition) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(64 * kMebibyte, &gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

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

class Vim3PartitionerTests : public GptDevicePartitionerTests {
 protected:
  static constexpr size_t kVim3BlockSize = 512;
  Vim3PartitionerTests() : GptDevicePartitionerTests("vim3", kVim3BlockSize) {}

  // Create a DevicePartition for a device.
  zx::result<std::unique_ptr<paver::DevicePartitioner>> CreatePartitioner(
      const fbl::unique_fd& device) {
    fidl::ClientEnd<fuchsia_io::Directory> svc_root = GetSvcRoot();
    return paver::Vim3Partitioner::Initialize(devmgr_.devfs_root().duplicate(), svc_root, device);
  }
};

TEST_F(Vim3PartitionerTests, InitializeWithoutGptFails) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(&gpt_dev));

  ASSERT_NOT_OK(CreatePartitioner(kDummyDevice));
}

TEST_F(Vim3PartitionerTests, InitializeWithoutFvmSucceeds) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(32 * kGibibyte, &gpt_dev));

  {
    // Pause the block watcher while we write partitions to the disk.
    // This is to avoid the block watcher seeing an intermediate state of the partition table
    // and incorrectly treating it as an MBR.
    // The watcher is automatically resumed when this goes out of scope.
    auto pauser = paver::BlockWatcherPauser::Create(GetSvcRoot());
    ASSERT_OK(pauser);

    // Set up a valid GPT.
    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_NO_FATAL_FAILURE(CreateGptDevice(gpt_dev.get(), &gpt));

    ASSERT_OK(CreatePartitioner(kDummyDevice));
  }
}

TEST_F(Vim3PartitionerTests, AddPartitionNotSupported) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(64 * kMebibyte, &gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);

  ASSERT_STATUS(status->AddPartition(PartitionSpec(paver::Partition::kZirconB)),
                ZX_ERR_NOT_SUPPORTED);
}

TEST_F(Vim3PartitionerTests, FindPartition) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = 0x800000;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * block_size_, &gpt_dev));

  // The initial gpt partitions are randomly chosen and does not necessarily reflect the
  // actual gpt partition layout in product.
  std::vector<PartitionDescription> vim3_partitions = {
      {GPT_DURABLE_BOOT_NAME, kDummyType, 0, 0x10000}, {GPT_VBMETA_A_NAME, kDummyType, 0, 0x10000},
      {GPT_VBMETA_B_NAME, kDummyType, 0, 0x10000},     {GPT_VBMETA_R_NAME, kDummyType, 0, 0x10000},
      {GPT_ZIRCON_A_NAME, kDummyType, 0, 0x10000},     {GPT_ZIRCON_B_NAME, kDummyType, 0, 0x10000},
      {GPT_ZIRCON_R_NAME, kDummyType, 0, 0x10000},     {GPT_FVM_NAME, kDummyType, 0, 0x10000},
  };
  vim3_partitions[0].start = 0x10400;
  for (size_t i = 1; i < vim3_partitions.size(); i++) {
    vim3_partitions[i].start = vim3_partitions[i - 1].start + vim3_partitions[i - 1].length;
  }
  ASSERT_NO_FATAL_FAILURE(InitializeStartingGPTPartitions(gpt_dev.get(), vim3_partitions));

  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());
  auto status = CreatePartitioner(gpt_fd.value());
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  EXPECT_NOT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA)));

  std::unique_ptr<BlockDevice> boot0_dev, boot1_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * kBlockSize, kBoot0Type, &boot0_dev));
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * kBlockSize, kBoot1Type, &boot1_dev));

  // Make sure we can find the important partitions.
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kBootloaderA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kZirconR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaA)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaB)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kVbMetaR)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kAbrMeta)));
  EXPECT_OK(partitioner->FindPartition(PartitionSpec(paver::Partition::kFuchsiaVolumeManager)));
}

TEST_F(Vim3PartitionerTests, CreateAbrClient) {
  std::unique_ptr<BlockDevice> gpt_dev;
  constexpr uint64_t kBlockCount = 0x748034;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(kBlockCount * block_size_, &gpt_dev));

  const std::vector<PartitionDescription> kStartingPartitions = {
      {GPT_DURABLE_BOOT_NAME, kDummyType, 0x10400, 0x10000},
  };
  ASSERT_NO_FATAL_FAILURE(InitializeStartingGPTPartitions(gpt_dev.get(), kStartingPartitions));
  fidl::ClientEnd<fuchsia_io::Directory> svc_root = GetSvcRoot();
  std::shared_ptr<paver::Context> context;
  EXPECT_OK(paver::Vim3AbrClientFactory().New(devmgr_.devfs_root().duplicate(), svc_root, context));
}

TEST_F(Vim3PartitionerTests, SupportsPartition) {
  std::unique_ptr<BlockDevice> gpt_dev;
  ASSERT_NO_FATAL_FAILURE(CreateDisk(64 * kMebibyte, &gpt_dev));
  zx::result gpt_fd = fd(*gpt_dev);
  ASSERT_OK(gpt_fd.status_value());

  auto status = CreatePartitioner(gpt_fd.value());
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
      partitioner->SupportsPartition(PartitionSpec(paver::Partition::kAbrMeta, "foo_type")));
}

TEST(AstroPartitionerTests, IsFvmWithinFtl) {
  std::unique_ptr<SkipBlockDevice> device;
  ASSERT_NO_FATAL_FAILURE(SkipBlockDevice::Create(NandInfo(), &device));

  fidl::ClientEnd<fuchsia_io::Directory> svc_root;
  std::shared_ptr<paver::Context> context = std::make_shared<paver::Context>();
  auto status = paver::AstroPartitioner::Initialize(device->devfs_root(), svc_root, context);
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();
  ASSERT_TRUE(partitioner->IsFvmWithinFtl());
}

TEST(AstroPartitionerTests, ChooseAstroPartitioner) {
  std::unique_ptr<SkipBlockDevice> device;
  ASSERT_NO_FATAL_FAILURE(SkipBlockDevice::Create(NandInfo(), &device));
  auto devfs_root = device->devfs_root();
  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devfs_root, "sys/platform/00:00:2d/ramctl", &fd));
  std::unique_ptr<BlockDevice> zircon_a;
  ASSERT_NO_FATAL_FAILURE(BlockDevice::Create(devfs_root, kZirconAType, &zircon_a));

  std::shared_ptr<paver::Context> context = std::make_shared<paver::Context>();
  auto partitioner = paver::DevicePartitionerFactory::Create(std::move(devfs_root), kInvalidSvcRoot,
                                                             paver::Arch::kArm64, context);
  ASSERT_NE(partitioner.get(), nullptr);
  ASSERT_TRUE(partitioner->IsFvmWithinFtl());
}

TEST(AstroPartitionerTests, AddPartitionTest) {
  std::unique_ptr<SkipBlockDevice> device;
  ASSERT_NO_FATAL_FAILURE(SkipBlockDevice::Create(NandInfo(), &device));

  fidl::ClientEnd<fuchsia_io::Directory> svc_root;
  std::shared_ptr<paver::Context> context = std::make_shared<paver::Context>();
  auto status = paver::AstroPartitioner::Initialize(device->devfs_root(), svc_root, context);
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();
  ASSERT_STATUS(partitioner->AddPartition(PartitionSpec(paver::Partition::kZirconB)),
                ZX_ERR_NOT_SUPPORTED);
}

TEST(AstroPartitionerTests, WipeFvmTest) {
  std::unique_ptr<SkipBlockDevice> device;
  ASSERT_NO_FATAL_FAILURE(SkipBlockDevice::Create(NandInfo(), &device));

  fidl::ClientEnd<fuchsia_io::Directory> svc_root;
  std::shared_ptr<paver::Context> context = std::make_shared<paver::Context>();
  auto status = paver::AstroPartitioner::Initialize(device->devfs_root(), svc_root, context);
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();
  ASSERT_OK(partitioner->WipeFvm());
}

TEST(AstroPartitionerTests, FinalizePartitionTest) {
  std::unique_ptr<SkipBlockDevice> device;
  ASSERT_NO_FATAL_FAILURE(SkipBlockDevice::Create(NandInfo(), &device));

  fidl::ClientEnd<fuchsia_io::Directory> svc_root;
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
  ASSERT_NO_FATAL_FAILURE(SkipBlockDevice::Create(NandInfo(), &device));
  auto devfs_root = device->devfs_root();
  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devfs_root, "sys/platform/00:00:2d/ramctl", &fd));
  std::unique_ptr<BlockDevice> fvm;
  ASSERT_NO_FATAL_FAILURE(BlockDevice::Create(devfs_root, kFvmType, &fvm));

  fidl::ClientEnd<fuchsia_io::Directory> svc_root;
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
  ASSERT_NO_FATAL_FAILURE(SkipBlockDevice::Create(NandInfo(), &device));

  fidl::ClientEnd<fuchsia_io::Directory> svc_root;
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
// Call with ASSERT_NO_FATAL_FAILURE.
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
  ASSERT_NO_FATAL_FAILURE(SkipBlockDevice::Create(NandInfo(), &device));

  fidl::ClientEnd<fuchsia_io::Directory> svc_root;
  std::shared_ptr<paver::Context> context = std::make_shared<paver::Context>();
  auto status = paver::AstroPartitioner::Initialize(device->devfs_root(), svc_root, context);
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  ASSERT_NO_FATAL_FAILURE(
      WritePartition(partitioner.get(), PartitionSpec(paver::Partition::kBootloaderA), "abcd1234"));

  const uint8_t* tpl_partition =
      PartitionStart(device->mapper(), NandInfo(), GUID_BOOTLOADER_VALUE);
  ASSERT_NOT_NULL(tpl_partition);
  ASSERT_EQ(0, memcmp("abcd1234", tpl_partition, 8));
}

TEST(AstroPartitionerTests, BootloaderBl2Test) {
  std::unique_ptr<SkipBlockDevice> device;
  ASSERT_NO_FATAL_FAILURE(SkipBlockDevice::Create(NandInfo(), &device));

  fidl::ClientEnd<fuchsia_io::Directory> svc_root;
  std::shared_ptr<paver::Context> context = std::make_shared<paver::Context>();
  auto status = paver::AstroPartitioner::Initialize(device->devfs_root(), svc_root, context);
  ASSERT_OK(status);
  std::unique_ptr<paver::DevicePartitioner>& partitioner = status.value();

  ASSERT_NO_FATAL_FAILURE(WritePartition(
      partitioner.get(), PartitionSpec(paver::Partition::kBootloaderA, "bl2"), "123xyz"));

  const uint8_t* bl2_partition = PartitionStart(device->mapper(), NandInfo(), GUID_BL2_VALUE);
  ASSERT_NOT_NULL(bl2_partition);
  // Special BL2 handling - image contents start at offset 4096 (page 1 on Astro).
  ASSERT_EQ(0, memcmp("123xyz", bl2_partition + 4096, 6));
}

class As370PartitionerTests : public zxtest::Test {
 protected:
  As370PartitionerTests() {
    IsolatedDevmgr::Args args;
    args.disable_block_watcher = false;
    args.board_name = "visalia";

    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));

    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform", &fd));
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform/00:00:2d/ramctl", &fd));
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
      devmgr_.devfs_root().duplicate(), kInvalidSvcRoot, paver::Arch::kArm64, context);
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
  ASSERT_NO_FATAL_FAILURE(BlockDevice::Create(devmgr_.devfs_root(), kFvmType, &fvm));

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
