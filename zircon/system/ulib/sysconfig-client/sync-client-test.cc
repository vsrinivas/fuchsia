// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <lib/sysconfig/sync-client.h>
// clang-format on

#include <lib/zx/channel.h>
#include <ramdevice-client/ramnand.h>
#include <zircon/hw/gpt.h>
#include <zxtest/zxtest.h>

namespace {

constexpr uint32_t kOobSize = 8;
constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kPagesPerBlock = 64;
constexpr uint32_t kBlockSize = kPageSize * kPagesPerBlock;
constexpr uint32_t kNumBlocks = 8;

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
            .partition_count = 2,
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
                        .type_guid = GUID_SYS_CONFIG_VALUE,
                        .unique_guid = {},
                        .first_block = 4,
                        .last_block = 7,
                        .copy_count = 4,
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

class SkipBlockDevice {
 public:
  static void Create(const fuchsia_hardware_nand_RamNandInfo& nand_info,
                     std::optional<SkipBlockDevice>* device);

  fbl::unique_fd devfs_root() { return ctl_->devfs_root().duplicate(); }

  fzl::VmoMapper& mapper() { return mapper_; }

  ~SkipBlockDevice() = default;

  SkipBlockDevice(fbl::RefPtr<ramdevice_client::RamNandCtl> ctl, ramdevice_client::RamNand ram_nand,
                  fzl::VmoMapper mapper)
      : ctl_(std::move(ctl)), ram_nand_(std::move(ram_nand)), mapper_(std::move(mapper)) {}

 private:
  fbl::RefPtr<ramdevice_client::RamNandCtl> ctl_;
  ramdevice_client::RamNand ram_nand_;
  fzl::VmoMapper mapper_;
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
  oob->magic = 0x7462626E;  // "nbbt"
  oob->program_erase_cycles = 0;
  oob->generation = 1;
}

void SkipBlockDevice::Create(const fuchsia_hardware_nand_RamNandInfo& nand_info,
                             std::optional<SkipBlockDevice>* device) {
  fzl::VmoMapper mapper;
  zx::vmo vmo;
  ASSERT_OK(mapper.CreateAndMap((kPageSize + kOobSize) * kPagesPerBlock * kNumBlocks,
                                ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo));
  memset(mapper.start(), 0xff, mapper.size());
  CreateBadBlockMap(mapper.start());
  zx::vmo dup;
  ASSERT_OK(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));

  fuchsia_hardware_nand_RamNandInfo info = nand_info;
  info.vmo = dup.release();
  fbl::RefPtr<ramdevice_client::RamNandCtl> ctl;
  ASSERT_OK(ramdevice_client::RamNandCtl::Create(&ctl));
  std::optional<ramdevice_client::RamNand> ram_nand;
  ASSERT_OK(ramdevice_client::RamNand::Create(ctl, &info, &ram_nand));
  fbl::unique_fd fd;
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(ctl->devfs_root(), "sys/platform", &fd));
  device->emplace(std::move(ctl), *std::move(ram_nand), std::move(mapper));
}

void CreatePayload(size_t size, zx::vmo* out) {
  zx::vmo vmo;
  fzl::VmoMapper mapper;
  ASSERT_OK(mapper.CreateAndMap(fbl::round_up(size, ZX_PAGE_SIZE),
                                ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo));
  memset(mapper.start(), 0x4a, mapper.size());
  *out = std::move(vmo);
}

void ValidateBuffer(void* buffer, size_t size) {
  const auto* start = static_cast<uint8_t*>(buffer);
  for (size_t i = 0; i < size; i++) {
    ASSERT_EQ(start[i], 0x5c, "i = %zu", i);
  }
}


class SyncClientTest : public zxtest::Test {
 protected:
  SyncClientTest() {
    ASSERT_NO_FATAL_FAILURES(SkipBlockDevice::Create(kNandInfo, &device_));
  }

  void ValidateWritten(size_t offset, size_t size) {
    for (size_t block = 4; block < 5; block++) {
      const uint8_t* start =
          static_cast<uint8_t*>(device_->mapper().start()) + (block * kBlockSize) + offset;
      for (size_t i = 0; i < size; i++) {
        ASSERT_EQ(start[i], 0x4a, "block = %zu, i = %zu", block, i);
      }
    }
  }

  void ValidateUnwritten(size_t offset, size_t size) {
    for (size_t block = 4; block < 5; block++) {
      const uint8_t* start =
          static_cast<uint8_t*>(device_->mapper().start()) + (block * kBlockSize) + offset;
      for (size_t i = 0; i < size; i++) {
        ASSERT_EQ(start[i], 0xff, "block = %zu, offset: %zu i = %zu", block, offset, i);
      }
    }
  }

  void WriteData(size_t offset, size_t size) {
    for (size_t block = 4; block < 7; block++) {
      uint8_t* start =
          static_cast<uint8_t*>(device_->mapper().start()) + (block * kBlockSize) + offset;
      memset(start, 0x5c, size);
    }
  }

  std::optional<SkipBlockDevice> device_;
};

TEST_F(SyncClientTest, CreateAstro) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));
}

using PartitionType = sysconfig::SyncClient::PartitionType;

constexpr size_t kKilobyte = 1 << 10;

TEST_F(SyncClientTest, WritePartitionSysconfig) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  zx::vmo vmo;
  ASSERT_NO_FATAL_FAILURES(CreatePayload(60 * kKilobyte, &vmo));
  ASSERT_OK(client->WritePartition(PartitionType::kSysconfig, vmo, 0));

  ASSERT_NO_FATAL_FAILURES(ValidateWritten(0, 60 * kKilobyte));
  ASSERT_NO_FATAL_FAILURES(ValidateUnwritten(60 * kKilobyte, 196 * kKilobyte));
}

TEST_F(SyncClientTest, WritePartitionAbrMetadata) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  zx::vmo vmo;
  ASSERT_NO_FATAL_FAILURES(CreatePayload(4 * kKilobyte, &vmo));
  ASSERT_OK(client->WritePartition(PartitionType::kABRMetadata, vmo, 0));

  ASSERT_NO_FATAL_FAILURES(ValidateUnwritten(0, 60 * kKilobyte));
  ASSERT_NO_FATAL_FAILURES(ValidateWritten(60 * kKilobyte, 4 * kKilobyte));
  ASSERT_NO_FATAL_FAILURES(ValidateUnwritten(64 * kKilobyte, 192 * kKilobyte));
}

TEST_F(SyncClientTest, WritePartitionVbMetaA) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  zx::vmo vmo;
  ASSERT_NO_FATAL_FAILURES(CreatePayload(64 * kKilobyte, &vmo));
  ASSERT_OK(client->WritePartition(PartitionType::kVerifiedBootMetadataA, vmo, 0));

  ASSERT_NO_FATAL_FAILURES(ValidateUnwritten(0, 64 * kKilobyte));
  ASSERT_NO_FATAL_FAILURES(ValidateWritten(64 * kKilobyte, 64 * kKilobyte));
  ASSERT_NO_FATAL_FAILURES(ValidateUnwritten(128 * kKilobyte, 128 * kKilobyte));
}

TEST_F(SyncClientTest, WritePartitionVbMetaB) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  zx::vmo vmo;
  ASSERT_NO_FATAL_FAILURES(CreatePayload(64 * kKilobyte, &vmo));
  ASSERT_OK(client->WritePartition(PartitionType::kVerifiedBootMetadataB, vmo, 0));

  ASSERT_NO_FATAL_FAILURES(ValidateUnwritten(0, 128 * kKilobyte));
  ASSERT_NO_FATAL_FAILURES(ValidateWritten(128 * kKilobyte, 64 * kKilobyte));
  ASSERT_NO_FATAL_FAILURES(ValidateUnwritten(192 * kKilobyte, 64 * kKilobyte));
}

TEST_F(SyncClientTest, WritePartitionVbMetaR) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  zx::vmo vmo;
  ASSERT_NO_FATAL_FAILURES(CreatePayload(64 * kKilobyte, &vmo));
  ASSERT_OK(client->WritePartition(PartitionType::kVerifiedBootMetadataR, vmo, 0));

  ASSERT_NO_FATAL_FAILURES(ValidateUnwritten(0, 192 * kKilobyte));
  ASSERT_NO_FATAL_FAILURES(ValidateWritten(192 * kKilobyte, 64 * kKilobyte));
}

TEST_F(SyncClientTest, ReadPartitionSysconfig) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  fzl::OwnedVmoMapper mapper;
  ASSERT_OK(mapper.CreateAndMap(fbl::round_up(60 * kKilobyte, ZX_PAGE_SIZE), "test"));

  ASSERT_NO_FATAL_FAILURES(WriteData(0, 60 * kKilobyte));
  ASSERT_OK(client->ReadPartition(PartitionType::kSysconfig, mapper.vmo(), 0));
  ASSERT_NO_FATAL_FAILURES(ValidateBuffer(mapper.start(), 60 * kKilobyte));
}

TEST_F(SyncClientTest, ReadPartitionAbrMetadata) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  fzl::OwnedVmoMapper mapper;
  ASSERT_OK(mapper.CreateAndMap(fbl::round_up(4 * kKilobyte, ZX_PAGE_SIZE), "test"));

  ASSERT_NO_FATAL_FAILURES(WriteData(60 * kKilobyte, 4 * kKilobyte));
  ASSERT_OK(client->ReadPartition(PartitionType::kABRMetadata, mapper.vmo(), 0));
  ASSERT_NO_FATAL_FAILURES(ValidateBuffer(mapper.start(), 4 * kKilobyte));
}

TEST_F(SyncClientTest, ReadPartitionVbMetaA) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  fzl::OwnedVmoMapper mapper;
  ASSERT_OK(mapper.CreateAndMap(fbl::round_up(64 * kKilobyte, ZX_PAGE_SIZE), "test"));

  ASSERT_NO_FATAL_FAILURES(WriteData(64 * kKilobyte, 64 * kKilobyte));
  ASSERT_OK(client->ReadPartition(PartitionType::kVerifiedBootMetadataA, mapper.vmo(), 0));
  ASSERT_NO_FATAL_FAILURES(ValidateBuffer(mapper.start(), 64 * kKilobyte));
}

TEST_F(SyncClientTest, ReadPartitionVbMetaB) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  fzl::OwnedVmoMapper mapper;
  ASSERT_OK(mapper.CreateAndMap(fbl::round_up(64 * kKilobyte, ZX_PAGE_SIZE), "test"));

  ASSERT_NO_FATAL_FAILURES(WriteData(128 * kKilobyte, 64 * kKilobyte));
  ASSERT_OK(client->ReadPartition(PartitionType::kVerifiedBootMetadataB, mapper.vmo(), 0));
  ASSERT_NO_FATAL_FAILURES(ValidateBuffer(mapper.start(), 64 * kKilobyte));
}

TEST_F(SyncClientTest, ReadPartitionVbMetaR) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  fzl::OwnedVmoMapper mapper;
  ASSERT_OK(mapper.CreateAndMap(fbl::round_up(64 * kKilobyte, ZX_PAGE_SIZE), "test"));

  ASSERT_NO_FATAL_FAILURES(WriteData(192 * kKilobyte, 64 * kKilobyte));
  ASSERT_OK(client->ReadPartition(PartitionType::kVerifiedBootMetadataR, mapper.vmo(), 0));
  ASSERT_NO_FATAL_FAILURES(ValidateBuffer(mapper.start(), 64 * kKilobyte));
}

TEST_F(SyncClientTest, GetPartitionSizeSysconfig) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  ASSERT_EQ(client->GetPartitionSize(PartitionType::kSysconfig), 60 * kKilobyte);
}

TEST_F(SyncClientTest, GetPartitionSizeAbrMetadata) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  ASSERT_EQ(client->GetPartitionSize(PartitionType::kABRMetadata), 4 * kKilobyte);
}

TEST_F(SyncClientTest, GetPartitionSizeVbMetaA) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  ASSERT_EQ(client->GetPartitionSize(PartitionType::kVerifiedBootMetadataA), 64 * kKilobyte);
}

TEST_F(SyncClientTest, GetPartitionSizeVbMetaB) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  ASSERT_EQ(client->GetPartitionSize(PartitionType::kVerifiedBootMetadataB), 64 * kKilobyte);
}

TEST_F(SyncClientTest, GetPartitionSizeVbMetaR) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  ASSERT_EQ(client->GetPartitionSize(PartitionType::kVerifiedBootMetadataR), 64 * kKilobyte);
}

}  // namespace
