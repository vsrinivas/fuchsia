// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <lib/sysconfig/sync-client.h>
#include <lib/sysconfig/sysconfig-header.h>

// clang-format on

#include <lib/zx/channel.h>
#include <zircon/hw/gpt.h>

#include <fbl/algorithm.h>
#include <ramdevice-client-test/ramnandctl.h>
#include <zxtest/zxtest.h>

namespace {

constexpr uint32_t kOobSize = 8;
constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kPagesPerBlock = 64;
constexpr uint32_t kBlockSize = kPageSize * kPagesPerBlock;
constexpr uint32_t kNumBlocks = 8;

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
              .partition_count = 2,
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
}

class SkipBlockDevice {
 public:
  static void Create(fuchsia_hardware_nand::wire::RamNandInfo nand_info,
                     std::optional<SkipBlockDevice>* device);

  fbl::unique_fd devfs_root() { return ctl_->devfs_root().duplicate(); }

  fzl::VmoMapper& mapper() { return mapper_; }

  ~SkipBlockDevice() = default;

  SkipBlockDevice(std::unique_ptr<ramdevice_client_test::RamNandCtl> ctl,
                  ramdevice_client::RamNand ram_nand, fzl::VmoMapper mapper)
      : ctl_(std::move(ctl)), ram_nand_(std::move(ram_nand)), mapper_(std::move(mapper)) {}

 private:
  std::unique_ptr<ramdevice_client_test::RamNandCtl> ctl_;
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

void SkipBlockDevice::Create(fuchsia_hardware_nand::wire::RamNandInfo nand_info,
                             std::optional<SkipBlockDevice>* device) {
  fzl::VmoMapper mapper;
  zx::vmo vmo;
  ASSERT_OK(mapper.CreateAndMap((kPageSize + kOobSize) * kPagesPerBlock * kNumBlocks,
                                ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo));
  memset(mapper.start(), 0xff, mapper.size());
  CreateBadBlockMap(mapper.start());
  ASSERT_OK(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &nand_info.vmo));

  std::unique_ptr<ramdevice_client_test::RamNandCtl> ctl;
  ASSERT_OK(ramdevice_client_test::RamNandCtl::Create(&ctl));
  std::optional<ramdevice_client::RamNand> ram_nand;
  ASSERT_OK(ctl->CreateRamNand(std::move(nand_info), &ram_nand));
  fbl::unique_fd fd;
  ASSERT_OK(device_watcher::RecursiveWaitForFile(ctl->devfs_root(), "sys/platform", &fd));
  device->emplace(std::move(ctl), *std::move(ram_nand), std::move(mapper));
}

void CreatePayload(size_t size, zx::vmo* out, uint8_t data = 0x4a) {
  zx::vmo vmo;
  fzl::VmoMapper mapper;
  ASSERT_OK(mapper.CreateAndMap(fbl::round_up(size, zx_system_get_page_size()),
                                ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo));
  memset(mapper.start(), data, mapper.size());
  *out = std::move(vmo);
}

void ValidateBuffer(void* buffer, size_t size, uint8_t expected = 0x5c) {
  const auto* start = static_cast<uint8_t*>(buffer);
  for (size_t i = 0; i < size; i++) {
    ASSERT_EQ(start[i], expected, "i = %zu", i);
  }
}

class SyncClientTest : public zxtest::Test {
 protected:
  SyncClientTest() { ASSERT_NO_FATAL_FAILURE(SkipBlockDevice::Create(NandInfo(), &device_)); }

  void ValidateWritten(size_t offset, size_t size, uint8_t expected = 0x4a) {
    for (size_t block = 4; block < 5; block++) {
      const uint8_t* start =
          static_cast<uint8_t*>(device_->mapper().start()) + (block * kBlockSize) + offset;
      for (size_t i = 0; i < size; i++) {
        ASSERT_EQ(start[i], expected, "block = %zu, i = %zu", block, i);
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

  void WriteData(size_t offset, size_t size, uint8_t data = 0x5c) {
    for (size_t block = 4; block < 7; block++) {
      uint8_t* start =
          static_cast<uint8_t*>(device_->mapper().start()) + (block * kBlockSize) + offset;
      memset(start, data, size);
    }
  }

  void TestLayoutUpdate(std::optional<sysconfig_header> current_header,
                        const sysconfig_header& target_header);

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
  ASSERT_NO_FATAL_FAILURE(CreatePayload(60 * kKilobyte, &vmo));
  ASSERT_OK(client->WritePartition(PartitionType::kSysconfig, vmo, 0));

  ASSERT_NO_FATAL_FAILURE(ValidateWritten(0, 60 * kKilobyte));
  ASSERT_NO_FATAL_FAILURE(ValidateUnwritten(60 * kKilobyte, 196 * kKilobyte));
}

TEST_F(SyncClientTest, WritePartitionAbrMetadata) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  zx::vmo vmo;
  ASSERT_NO_FATAL_FAILURE(CreatePayload(4 * kKilobyte, &vmo));
  ASSERT_OK(client->WritePartition(PartitionType::kABRMetadata, vmo, 0));

  ASSERT_NO_FATAL_FAILURE(ValidateUnwritten(0, 60 * kKilobyte));
  ASSERT_NO_FATAL_FAILURE(ValidateWritten(60 * kKilobyte, 4 * kKilobyte));
  ASSERT_NO_FATAL_FAILURE(ValidateUnwritten(64 * kKilobyte, 192 * kKilobyte));
}

TEST_F(SyncClientTest, WritePartitionVbMetaA) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  zx::vmo vmo;
  ASSERT_NO_FATAL_FAILURE(CreatePayload(64 * kKilobyte, &vmo));
  ASSERT_OK(client->WritePartition(PartitionType::kVerifiedBootMetadataA, vmo, 0));

  ASSERT_NO_FATAL_FAILURE(ValidateUnwritten(0, 64 * kKilobyte));
  ASSERT_NO_FATAL_FAILURE(ValidateWritten(64 * kKilobyte, 64 * kKilobyte));
  ASSERT_NO_FATAL_FAILURE(ValidateUnwritten(128 * kKilobyte, 128 * kKilobyte));
}

TEST_F(SyncClientTest, WritePartitionVbMetaB) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  zx::vmo vmo;
  ASSERT_NO_FATAL_FAILURE(CreatePayload(64 * kKilobyte, &vmo));
  ASSERT_OK(client->WritePartition(PartitionType::kVerifiedBootMetadataB, vmo, 0));

  ASSERT_NO_FATAL_FAILURE(ValidateUnwritten(0, 128 * kKilobyte));
  ASSERT_NO_FATAL_FAILURE(ValidateWritten(128 * kKilobyte, 64 * kKilobyte));
  ASSERT_NO_FATAL_FAILURE(ValidateUnwritten(192 * kKilobyte, 64 * kKilobyte));
}

TEST_F(SyncClientTest, WritePartitionVbMetaR) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  zx::vmo vmo;
  ASSERT_NO_FATAL_FAILURE(CreatePayload(64 * kKilobyte, &vmo));
  ASSERT_OK(client->WritePartition(PartitionType::kVerifiedBootMetadataR, vmo, 0));

  ASSERT_NO_FATAL_FAILURE(ValidateUnwritten(0, 192 * kKilobyte));
  ASSERT_NO_FATAL_FAILURE(ValidateWritten(192 * kKilobyte, 64 * kKilobyte));
}

TEST_F(SyncClientTest, ReadPartitionSysconfig) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  fzl::OwnedVmoMapper mapper;
  ASSERT_OK(mapper.CreateAndMap(fbl::round_up(60 * kKilobyte, zx_system_get_page_size()), "test"));

  ASSERT_NO_FATAL_FAILURE(WriteData(0, 60 * kKilobyte));
  ASSERT_OK(client->ReadPartition(PartitionType::kSysconfig, mapper.vmo(), 0));
  ASSERT_NO_FATAL_FAILURE(ValidateBuffer(mapper.start(), 60 * kKilobyte));
}

TEST_F(SyncClientTest, ReadPartitionAbrMetadata) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  fzl::OwnedVmoMapper mapper;
  ASSERT_OK(mapper.CreateAndMap(fbl::round_up(4 * kKilobyte, zx_system_get_page_size()), "test"));

  ASSERT_NO_FATAL_FAILURE(WriteData(60 * kKilobyte, 4 * kKilobyte));
  ASSERT_OK(client->ReadPartition(PartitionType::kABRMetadata, mapper.vmo(), 0));
  ASSERT_NO_FATAL_FAILURE(ValidateBuffer(mapper.start(), 4 * kKilobyte));
}

TEST_F(SyncClientTest, ReadPartitionVbMetaA) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  fzl::OwnedVmoMapper mapper;
  ASSERT_OK(mapper.CreateAndMap(fbl::round_up(64 * kKilobyte, zx_system_get_page_size()), "test"));

  ASSERT_NO_FATAL_FAILURE(WriteData(64 * kKilobyte, 64 * kKilobyte));
  ASSERT_OK(client->ReadPartition(PartitionType::kVerifiedBootMetadataA, mapper.vmo(), 0));
  ASSERT_NO_FATAL_FAILURE(ValidateBuffer(mapper.start(), 64 * kKilobyte));
}

TEST_F(SyncClientTest, ReadPartitionVbMetaB) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  fzl::OwnedVmoMapper mapper;
  ASSERT_OK(mapper.CreateAndMap(fbl::round_up(64 * kKilobyte, zx_system_get_page_size()), "test"));

  ASSERT_NO_FATAL_FAILURE(WriteData(128 * kKilobyte, 64 * kKilobyte));
  ASSERT_OK(client->ReadPartition(PartitionType::kVerifiedBootMetadataB, mapper.vmo(), 0));
  ASSERT_NO_FATAL_FAILURE(ValidateBuffer(mapper.start(), 64 * kKilobyte));
}

TEST_F(SyncClientTest, ReadPartitionVbMetaR) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  fzl::OwnedVmoMapper mapper;
  ASSERT_OK(mapper.CreateAndMap(fbl::round_up(64 * kKilobyte, zx_system_get_page_size()), "test"));

  ASSERT_NO_FATAL_FAILURE(WriteData(192 * kKilobyte, 64 * kKilobyte));
  ASSERT_OK(client->ReadPartition(PartitionType::kVerifiedBootMetadataR, mapper.vmo(), 0));
  ASSERT_NO_FATAL_FAILURE(ValidateBuffer(mapper.start(), 64 * kKilobyte));
}

TEST_F(SyncClientTest, GetPartitionSizeSysconfig) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));
  size_t size;
  ASSERT_OK(client->GetPartitionSize(PartitionType::kSysconfig, &size));
  ASSERT_EQ(size, 60 * kKilobyte);
}

TEST_F(SyncClientTest, GetPartitionSizeAbrMetadata) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  size_t size;
  ASSERT_OK(client->GetPartitionSize(PartitionType::kABRMetadata, &size));
  ASSERT_EQ(size, 4 * kKilobyte);
}

TEST_F(SyncClientTest, GetPartitionSizeVbMetaA) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  size_t size;
  ASSERT_OK(client->GetPartitionSize(PartitionType::kVerifiedBootMetadataA, &size));
  ASSERT_EQ(size, 64 * kKilobyte);
}

TEST_F(SyncClientTest, GetPartitionSizeVbMetaB) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  size_t size;
  ASSERT_OK(client->GetPartitionSize(PartitionType::kVerifiedBootMetadataB, &size));
  ASSERT_EQ(size, 64 * kKilobyte);
}

TEST_F(SyncClientTest, GetPartitionSizeVbMetaR) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));

  size_t size;
  ASSERT_OK(client->GetPartitionSize(PartitionType::kVerifiedBootMetadataR, &size));
  ASSERT_EQ(size, 64 * kKilobyte);
}

namespace {
sysconfig_header GetNonLegacyHeaderForTest() {
  return {
      .magic = SYSCONFIG_HEADER_MAGIC_ARRAY,
      .sysconfig_data = {200 * kKilobyte, 56 * kKilobyte},
      .abr_metadata = {196 * kKilobyte, 4 * kKilobyte},
      .vb_metadata_a = {4 * kKilobyte, 64 * kKilobyte},
      .vb_metadata_b = {68 * kKilobyte, 64 * kKilobyte},
      .vb_metadata_r = {132 * kKilobyte, 64 * kKilobyte},
  };
}
}  // namespace

TEST(SysconfigHeaderTest, ValidHeader) {
  auto header = GetNonLegacyHeaderForTest();
  update_sysconfig_header_magic_and_crc(&header);
  ASSERT_TRUE(sysconfig_header_valid(&header, kPageSize, kBlockSize));
}

TEST(SysconfigHeaderTest, InvalidMagic) {
  auto invalid_magic = GetNonLegacyHeaderForTest();
  invalid_magic.magic[0] = 'A';
  ASSERT_FALSE(sysconfig_header_valid(&invalid_magic, kPageSize, kBlockSize));
}

TEST(SysconfigHeaderTest, InvalidCrc) {
  auto base = GetNonLegacyHeaderForTest();
  auto invalid_crc = base;
  invalid_crc.crc_value += 1;
  ASSERT_FALSE(sysconfig_header_valid(&invalid_crc, kPageSize, kBlockSize));
  // However, crc_value shall not affect comparison.
  ASSERT_TRUE(sysconfig_header_equal(&invalid_crc, &base));
}

TEST_F(SyncClientTest, HeaderNotPageAligned) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));
  auto not_page_alinged = GetNonLegacyHeaderForTest();
  not_page_alinged.sysconfig_data.size = 55 * kKilobyte;
  update_sysconfig_header_magic_and_crc(&not_page_alinged);
  ASSERT_FALSE(sysconfig_header_valid(&not_page_alinged, kPageSize, kBlockSize));
  ASSERT_STATUS(client->UpdateLayout(not_page_alinged), ZX_ERR_INVALID_ARGS);
}

TEST_F(SyncClientTest, HeaderInvalidOffset) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));
  auto invalid_offset = GetNonLegacyHeaderForTest();
  invalid_offset.sysconfig_data.offset = 256 * kKilobyte;
  update_sysconfig_header_magic_and_crc(&invalid_offset);
  ASSERT_FALSE(sysconfig_header_valid(&invalid_offset, kPageSize, kBlockSize));
  ASSERT_STATUS(client->UpdateLayout(invalid_offset), ZX_ERR_INVALID_ARGS);
}

TEST_F(SyncClientTest, HeaderInvalidSize) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));
  auto invalid_size = GetNonLegacyHeaderForTest();
  invalid_size.sysconfig_data.size = 252 * kKilobyte;
  update_sysconfig_header_magic_and_crc(&invalid_size);
  ASSERT_FALSE(sysconfig_header_valid(&invalid_size, kPageSize, kBlockSize));
  ASSERT_STATUS(client->UpdateLayout(invalid_size), ZX_ERR_INVALID_ARGS);
}

TEST_F(SyncClientTest, HeaderInvalidSizePlusOffset) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));
  auto invalid_size_offset = GetNonLegacyHeaderForTest();
  invalid_size_offset.sysconfig_data = {200 * kKilobyte, 60 * kKilobyte};
  update_sysconfig_header_magic_and_crc(&invalid_size_offset);
  ASSERT_FALSE(sysconfig_header_valid(&invalid_size_offset, kPageSize, kBlockSize));
  ASSERT_STATUS(client->UpdateLayout(invalid_size_offset), ZX_ERR_INVALID_ARGS);
}

TEST_F(SyncClientTest, HeaderOverlapSubpart) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));
  auto overlap_subpart = GetNonLegacyHeaderForTest();
  overlap_subpart.sysconfig_data = {196 * kKilobyte, 56 * kKilobyte};
  update_sysconfig_header_magic_and_crc(&overlap_subpart);
  ASSERT_FALSE(sysconfig_header_valid(&overlap_subpart, kPageSize, kBlockSize));
  ASSERT_STATUS(client->UpdateLayout(overlap_subpart), ZX_ERR_INVALID_ARGS);
}

TEST_F(SyncClientTest, HeaderPage0NotReserved) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));
  auto page0_not_reserved = GetNonLegacyHeaderForTest();
  page0_not_reserved.vb_metadata_a.offset = 0;
  update_sysconfig_header_magic_and_crc(&page0_not_reserved);
  ASSERT_FALSE(sysconfig_header_valid(&page0_not_reserved, kPageSize, kBlockSize));
  ASSERT_STATUS(client->UpdateLayout(page0_not_reserved), ZX_ERR_INVALID_ARGS);
}

void SyncClientTest::TestLayoutUpdate(const std::optional<sysconfig_header> current_header,
                                      const sysconfig_header& target_header) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));
  auto memory = static_cast<uint8_t*>(device_->mapper().start()) + 4 * kBlockSize;
  using PartitionType = sysconfig::SyncClient::PartitionType;
  auto init_header = current_header;
  // If current header is not provided, assume legacy layout.
  if (!init_header) {
    init_header = {
        .magic = SYSCONFIG_HEADER_MAGIC_ARRAY,
        .sysconfig_data = {0 * kKilobyte, 60 * kKilobyte},
        .abr_metadata = {60 * kKilobyte, 4 * kKilobyte},
        .vb_metadata_a = {64 * kKilobyte, 64 * kKilobyte},
        .vb_metadata_b = {128 * kKilobyte, 64 * kKilobyte},
        .vb_metadata_r = {192 * kKilobyte, 64 * kKilobyte},
        .crc_value = 2716817057,  // pre-calculated crc
    };
  }

  // Initialize the memory according to init_header.
  memset(memory, 0xff, 256 * kKilobyte);
  memset(&memory[init_header->sysconfig_data.offset], 1, init_header->sysconfig_data.size);
  memset(&memory[init_header->abr_metadata.offset], 2, init_header->abr_metadata.size);
  memset(&memory[init_header->vb_metadata_a.offset], 3, init_header->vb_metadata_a.size);
  memset(&memory[init_header->vb_metadata_b.offset], 4, init_header->vb_metadata_b.size);
  memset(&memory[init_header->vb_metadata_r.offset], 5, init_header->vb_metadata_r.size);
  // Write header to storage if provided.
  if (current_header) {
    update_sysconfig_header_magic_and_crc(&*init_header);
    memcpy(memory, &*init_header, sizeof(*init_header));
  }

  auto update_header = target_header;
  update_sysconfig_header_magic_and_crc(&update_header);
  ASSERT_OK(client->UpdateLayout(update_header));

  struct PartitionRange {
    uint64_t offset;
    uint64_t size;
    PartitionRange(sysconfig_subpartition info) : offset(info.offset), size(info.size) {}
  };

  struct ValidationData {
    PartitionType name;
    PartitionRange old_info;
    PartitionRange new_info;
    uint8_t expected;
  } validation_data[] = {
      {PartitionType::kSysconfig, init_header->sysconfig_data, update_header.sysconfig_data, 1},
      {PartitionType::kABRMetadata, init_header->abr_metadata, update_header.abr_metadata, 2},
      {PartitionType::kVerifiedBootMetadataA, init_header->vb_metadata_a,
       update_header.vb_metadata_a, 3},
      {PartitionType::kVerifiedBootMetadataB, init_header->vb_metadata_b,
       update_header.vb_metadata_b, 4},
      {PartitionType::kVerifiedBootMetadataR, init_header->vb_metadata_r,
       update_header.vb_metadata_r, 5},
  };

  for (auto item : validation_data) {
    auto content_size = std::min(item.old_info.size, item.new_info.size);
    ASSERT_NO_FATAL_FAILURE(ValidateWritten(item.new_info.offset, content_size, item.expected));
    size_t part_size, part_offset;
    ASSERT_OK(client->GetPartitionSize(item.name, &part_size));
    ASSERT_EQ(part_size, item.new_info.size);
    ASSERT_OK(client->GetPartitionOffset(item.name, &part_offset));
    ASSERT_EQ(part_offset, item.new_info.offset);
    fzl::OwnedVmoMapper vmo;
    // vmo::CreateAndMap does not allow creating a zero size vmo. But we are testing
    // empty sub-partition cases. Thus, give a minimum size below.
    ASSERT_OK(vmo.CreateAndMap(std::max(part_size, static_cast<size_t>(kPageSize)), "",
                               ZX_VM_PERM_READ | ZX_VM_PERM_WRITE));
    ASSERT_OK(client->ReadPartition(item.name, vmo.vmo(), 0));
    ASSERT_NO_FATAL_FAILURE(ValidateBuffer(vmo.start(), content_size, item.expected));
  }
}

TEST_F(SyncClientTest, UpdateLayoutShrink) {
  sysconfig_header shrunken_size_only = {
      .sysconfig_data = {4 * kKilobyte, 32 * kKilobyte},
      .abr_metadata = {60 * kKilobyte, 4 * kKilobyte},
      .vb_metadata_a = {64 * kKilobyte, 32 * kKilobyte},
      .vb_metadata_b = {128 * kKilobyte, 32 * kKilobyte},
      .vb_metadata_r = {192 * kKilobyte, 32 * kKilobyte},
  };
  TestLayoutUpdate(std::nullopt, shrunken_size_only);
}

TEST_F(SyncClientTest, UpdateLayoutShrinkAndExpand) {
  sysconfig_header shrunken_and_expand = {
      .sysconfig_data = {4 * kKilobyte, 20 * kKilobyte},
      .abr_metadata = {24 * kKilobyte, 40 * kKilobyte},
      .vb_metadata_a = {64 * kKilobyte, 32 * kKilobyte},
      .vb_metadata_b = {128 * kKilobyte, 32 * kKilobyte},
      .vb_metadata_r = {192 * kKilobyte, 32 * kKilobyte},
  };
  TestLayoutUpdate(std::nullopt, shrunken_and_expand);
}

TEST_F(SyncClientTest, UpdateLayoutReverseOrder) {
  sysconfig_header reverse_order = {
      .sysconfig_data = {192 * kKilobyte, 64 * kKilobyte},
      .abr_metadata = {128 * kKilobyte, 64 * kKilobyte},
      .vb_metadata_a = {64 * kKilobyte, 64 * kKilobyte},
      .vb_metadata_b = {60 * kKilobyte, 4 * kKilobyte},
      .vb_metadata_r = {4 * kKilobyte, 56 * kKilobyte},
  };
  TestLayoutUpdate(std::nullopt, reverse_order);
}

TEST_F(SyncClientTest, UpdateLayoutReverseOrderWithGap) {
  // To create gap between sub-paritions as well as order change.
  sysconfig_header reverse_order = {
      .sysconfig_data = {192 * kKilobyte, 32 * kKilobyte},
      .abr_metadata = {128 * kKilobyte, 32 * kKilobyte},
      .vb_metadata_a = {64 * kKilobyte, 32 * kKilobyte},
      .vb_metadata_b = {52 * kKilobyte, 12 * kKilobyte},
      .vb_metadata_r = {4 * kKilobyte, 32 * kKilobyte},
  };
  TestLayoutUpdate(std::nullopt, reverse_order);
}

sysconfig_header shrunken_configdata_abr_expand_at_end = {
    .sysconfig_data = {4 * kKilobyte, 20 * kKilobyte},
    .abr_metadata = {216 * kKilobyte, 40 * kKilobyte},
    .vb_metadata_a = {24 * kKilobyte, 64 * kKilobyte},
    .vb_metadata_b = {88 * kKilobyte, 64 * kKilobyte},
    .vb_metadata_r = {152 * kKilobyte, 64 * kKilobyte},
};

sysconfig_header empty_configdata_abr_expand_at_end = {
    .sysconfig_data = {4 * kKilobyte, 0},
    .abr_metadata = {196 * kKilobyte, 60 * kKilobyte},
    .vb_metadata_a = {4 * kKilobyte, 64 * kKilobyte},
    .vb_metadata_b = {68 * kKilobyte, 64 * kKilobyte},
    .vb_metadata_r = {132 * kKilobyte, 64 * kKilobyte},
};

TEST_F(SyncClientTest, UpdateLayoutShrinkConfigDataExpandAbrAtEnd) {
  TestLayoutUpdate(std::nullopt, shrunken_configdata_abr_expand_at_end);
}

TEST_F(SyncClientTest, UpdateLayoutEmptyConfigDataExpandAbrAtEnd) {
  TestLayoutUpdate(std::nullopt, empty_configdata_abr_expand_at_end);
}

TEST_F(SyncClientTest, UpdateLayoutFromShrukenToEmptyConfigData) {
  TestLayoutUpdate(shrunken_configdata_abr_expand_at_end, empty_configdata_abr_expand_at_end);
}

class SyncClientBufferedTest : public SyncClientTest {
 public:
  struct PartitionInfo {
    PartitionType partition;
    size_t partition_offset;
    size_t partition_size;
    std::optional<uint8_t> write_value;
  };

  // Tests that SyncClientBuffered correctly writes to caches and storage for one or more
  // sub-partitions.
  void TestWrite(const std::vector<PartitionInfo>& parts_to_test_write);

  // Tests that SyncClientBuffered correctly reads from cache and storage for one or more
  // sub-partitions.
  void TestRead(const std::vector<PartitionInfo>& parts_to_test_read);

  // Tests that SyncClientBuffered correctly writes according to a non-legacy header for
  // one or more sub-partitions.
  void TestWriteWithHeader(const std::vector<PartitionInfo>& parts_to_test_write);

 protected:
  static void ValidateReadBuffer(const void* buffer, size_t len, uint8_t expected) {
    auto mem = static_cast<const uint8_t*>(buffer);
    for (size_t i = 0; i < len; i++) {
      ASSERT_EQ(mem[i], expected, "offset = %zu", i);
    }
  }

  static std::optional<uint8_t> GetExpectedWriteValue(size_t index,
                                                      const std::vector<PartitionInfo>& parts,
                                                      uint8_t unwritten_default) {
    for (auto& part : parts) {
      if (index >= part.partition_offset && index < part.partition_offset + part.partition_size) {
        return part.write_value;
      }
    }
    return unwritten_default;
  }

  void ValidateMemory(const std::vector<PartitionInfo>& parts) {
    const uint8_t* start = static_cast<uint8_t*>(device_->mapper().start()) + (4 * kBlockSize);
    for (size_t i = 0; i < 256 * kKilobyte; i++) {
      if (auto expected = GetExpectedWriteValue(i, parts, 0xff); expected) {
        ASSERT_EQ(start[i], *expected, "offset = %zu", i);
      }
    }
  }
};

void SyncClientBufferedTest::TestWrite(const std::vector<PartitionInfo>& parts_to_test_write) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));
  sysconfig::SyncClientBuffered sync_client_buffered(*std::move(client));

  // Write something to cache.
  for (auto& part : parts_to_test_write) {
    zx::vmo vmo;
    ASSERT_NO_FATAL_FAILURE(
        CreatePayload(part.partition_size, &vmo, part.write_value ? *part.write_value : 0x4a));
    ASSERT_OK(sync_client_buffered.WritePartition(part.partition, vmo, 0));
  }

  // Verify that cache is correctly written.
  for (auto& part : parts_to_test_write) {
    if (part.write_value) {
      auto cache_buffer = sync_client_buffered.GetCacheBuffer(part.partition);
      ASSERT_NOT_NULL(cache_buffer);
      ValidateReadBuffer(cache_buffer, part.partition_size, *part.write_value);
    }
  }

  // Verify that nothing is written to memory yet.
  ASSERT_NO_FATAL_FAILURE(ValidateMemory({}));

  ASSERT_OK(sync_client_buffered.Flush());

  // Veiry that memory is correctly written after flushing
  ASSERT_NO_FATAL_FAILURE(ValidateMemory(parts_to_test_write));
}

constexpr SyncClientBufferedTest::PartitionInfo kLegacySysconfigPartitionInfo = {
    PartitionType::kSysconfig, 0, 60 * kKilobyte, 0x1};
constexpr SyncClientBufferedTest::PartitionInfo kLegacyAbrPartitionInfo = {
    PartitionType::kABRMetadata, 60 * kKilobyte, 4 * kKilobyte, 0x2};
constexpr SyncClientBufferedTest::PartitionInfo kLegacyVbAPartitionInfo = {
    PartitionType::kVerifiedBootMetadataA, 64 * kKilobyte, 64 * kKilobyte, 0x3};
constexpr SyncClientBufferedTest::PartitionInfo kLegacyVbBPartitionInfo = {
    PartitionType::kVerifiedBootMetadataB, 128 * kKilobyte, 64 * kKilobyte, 0x4};
constexpr SyncClientBufferedTest::PartitionInfo kLegacyVbRPartitionInfo = {
    PartitionType::kVerifiedBootMetadataR, 192 * kKilobyte, 64 * kKilobyte, 0x5};

TEST_F(SyncClientBufferedTest, WritePartitionSysconfig) {
  TestWrite({kLegacySysconfigPartitionInfo});
}

TEST_F(SyncClientBufferedTest, WritePartitionAbrMetadata) { TestWrite({kLegacyAbrPartitionInfo}); }

TEST_F(SyncClientBufferedTest, WritePartitionVbMetaA) { TestWrite({kLegacyVbAPartitionInfo}); }

TEST_F(SyncClientBufferedTest, WritePartitionVbMetaB) { TestWrite({kLegacyVbBPartitionInfo}); }

TEST_F(SyncClientBufferedTest, WritePartitionVbMetaR) { TestWrite({kLegacyVbRPartitionInfo}); }

TEST_F(SyncClientBufferedTest, WriteAllPartitions) {
  TestWrite({
      kLegacySysconfigPartitionInfo,
      kLegacyAbrPartitionInfo,
      kLegacyVbAPartitionInfo,
      kLegacyVbBPartitionInfo,
      kLegacyVbRPartitionInfo,
  });
}

void SyncClientBufferedTest::TestRead(const std::vector<PartitionInfo>& parts_to_test_read) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));
  sysconfig::SyncClientBuffered sync_client_buffered(*std::move(client));

  // Write something to cache.
  for (auto& part : parts_to_test_read) {
    zx::vmo vmo;
    ASSERT_NO_FATAL_FAILURE(
        CreatePayload(part.partition_size, &vmo, part.write_value ? *part.write_value : 0x4a));
    ASSERT_OK(sync_client_buffered.WritePartition(part.partition, vmo, 0));
  }

  // Verify that data from cache is correctly read.
  for (auto& part : parts_to_test_read) {
    fzl::OwnedVmoMapper mapper;
    ASSERT_OK(mapper.CreateAndMap(part.partition_size, "test"));
    ASSERT_OK(sync_client_buffered.ReadPartition(part.partition, mapper.vmo(), 0));
    uint8_t expected = part.write_value ? *part.write_value : 0x4a;
    ASSERT_NO_FATAL_FAILURE(ValidateReadBuffer(mapper.start(), part.partition_size, expected));
  }

  ASSERT_OK(sync_client_buffered.Flush());

  // Verify that data can still be correctly read after flushing to memory.
  for (auto& part : parts_to_test_read) {
    fzl::OwnedVmoMapper mapper;
    ASSERT_OK(mapper.CreateAndMap(part.partition_size, "test"));
    ASSERT_OK(sync_client_buffered.ReadPartition(part.partition, mapper.vmo(), 0));
    uint8_t expected = part.write_value ? *part.write_value : 0x4a;
    ASSERT_NO_FATAL_FAILURE(ValidateReadBuffer(mapper.start(), part.partition_size, expected));
  }

  // Overwrite the memory with new data directly.
  for (auto& part : parts_to_test_read) {
    ASSERT_NO_FATAL_FAILURE(WriteData(part.partition_offset, part.partition_size));
  }

  // Verify that new data from memory is correctly read.
  for (auto& part : parts_to_test_read) {
    fzl::OwnedVmoMapper mapper;
    ASSERT_OK(mapper.CreateAndMap(part.partition_size, "test"));
    ASSERT_OK(sync_client_buffered.ReadPartition(part.partition, mapper.vmo(), 0));
    ASSERT_NO_FATAL_FAILURE(ValidateReadBuffer(mapper.start(), part.partition_size, 0x5c));
  }
}

TEST_F(SyncClientBufferedTest, ReadPartitionSysconfig) {
  TestRead({kLegacySysconfigPartitionInfo});
}

TEST_F(SyncClientBufferedTest, ReadPartitionAbrMetadata) { TestRead({kLegacyAbrPartitionInfo}); }

TEST_F(SyncClientBufferedTest, ReadPartitionVbMetaA) { TestRead({kLegacyVbAPartitionInfo}); }

TEST_F(SyncClientBufferedTest, ReadPartitionVbMetaB) { TestRead({kLegacyVbBPartitionInfo}); }

TEST_F(SyncClientBufferedTest, ReadPartitionVbMetaR) { TestRead({kLegacyVbRPartitionInfo}); }

TEST_F(SyncClientBufferedTest, ReadAllPartitions) {
  TestRead({
      kLegacySysconfigPartitionInfo,
      kLegacyAbrPartitionInfo,
      kLegacyVbAPartitionInfo,
      kLegacyVbBPartitionInfo,
      kLegacyVbRPartitionInfo,
  });
}

sysconfig_subpartition GetSubpartitionInfo(const sysconfig_header& header,
                                           sysconfig::SyncClient::PartitionType partition) {
  switch (partition) {
    case sysconfig::SyncClient::PartitionType::kSysconfig:
      return header.sysconfig_data;
    case sysconfig::SyncClient::PartitionType::kABRMetadata:
      return header.abr_metadata;
    case sysconfig::SyncClient::PartitionType::kVerifiedBootMetadataA:
      return header.vb_metadata_a;
    case sysconfig::SyncClient::PartitionType::kVerifiedBootMetadataB:
      return header.vb_metadata_b;
    case sysconfig::SyncClient::PartitionType::kVerifiedBootMetadataR:
      return header.vb_metadata_r;
  }
  ZX_ASSERT(false);  // Unreachable.
}

void SyncClientBufferedTest::TestWriteWithHeader(
    const std::vector<PartitionInfo>& parts_to_test_write) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));
  sysconfig::SyncClientBuffered sync_client_buffered(*std::move(client));
  // Sysconfig partition starts at the 5th block in this test environment.
  // Please refer to NandInfo() definition.
  auto memory = static_cast<uint8_t*>(device_->mapper().start()) + 4 * kBlockSize;

  sysconfig_header header = {
      .magic = SYSCONFIG_HEADER_MAGIC_ARRAY,
      .sysconfig_data = {200 * kKilobyte, 56 * kKilobyte},
      .abr_metadata = {196 * kKilobyte, 4 * kKilobyte},
      .vb_metadata_a = {4 * kKilobyte, 64 * kKilobyte},
      .vb_metadata_b = {68 * kKilobyte, 64 * kKilobyte},
      .vb_metadata_r = {132 * kKilobyte, 64 * kKilobyte},
  };
  update_sysconfig_header_magic_and_crc(&header);
  memcpy(memory, &header, sizeof(header));

  for (auto& part : parts_to_test_write) {
    if (!part.write_value) {
      continue;
    }
    auto subpartition_info = GetSubpartitionInfo(header, part.partition);
    zx::vmo vmo;
    ASSERT_NO_FATAL_FAILURE(
        CreatePayload(subpartition_info.size, &vmo, part.write_value ? *part.write_value : 0x4a));
    ASSERT_OK(sync_client_buffered.WritePartition(part.partition, vmo, 0));
  }

  ASSERT_OK(sync_client_buffered.Flush());

  // set sub-partition offset and size according to our test header.
  auto parts_copy = parts_to_test_write;
  for (auto& part : parts_copy) {
    auto subpartition_info = GetSubpartitionInfo(header, part.partition);
    part.partition_offset = subpartition_info.offset;
    part.partition_size = subpartition_info.size;
  }

  // Header in storage shall not change
  ASSERT_BYTES_EQ(memory, &header, sizeof(header));

  // make a dummy header partition to exempt it from validation
  parts_copy.push_back({.partition_offset = 0, .partition_size = 4 * kKilobyte, .write_value = {}});

  ASSERT_NO_FATAL_FAILURE(ValidateMemory(parts_copy));
}

TEST_F(SyncClientBufferedTest, WritePartitionSysconfigWithHeader) {
  // Subpartition position determined by header. No need to pass offset and size here.
  TestWriteWithHeader({{PartitionType::kSysconfig, 0, 0, 0x4a}});
}

TEST_F(SyncClientBufferedTest, WritePartitionAbrMetadataWithHeader) {
  TestWriteWithHeader({{PartitionType::kABRMetadata, 0, 0, 0x4a}});
}

TEST_F(SyncClientBufferedTest, WritePartitionVbMetaAWithHeader) {
  TestWriteWithHeader({{PartitionType::kVerifiedBootMetadataA, 0, 0, 0x4a}});
}

TEST_F(SyncClientBufferedTest, WritePartitionVbMetaBWithHeader) {
  TestWriteWithHeader({{PartitionType::kVerifiedBootMetadataB, 0, 0, 0x4a}});
}

TEST_F(SyncClientBufferedTest, WritePartitionVbMetaRWithHeader) {
  TestWriteWithHeader({{PartitionType::kVerifiedBootMetadataR, 0, 0, 0x4a}});
}

TEST_F(SyncClientBufferedTest, WriteAllPartitionsWithHeader) {
  TestWriteWithHeader({{PartitionType::kSysconfig, 0, 0, 0x1},
                       {PartitionType::kABRMetadata, 0, 0, 0x2},
                       {PartitionType::kVerifiedBootMetadataA, 0, 0, 0x3},
                       {PartitionType::kVerifiedBootMetadataB, 0, 0, 0x4},
                       {PartitionType::kVerifiedBootMetadataR, 0, 0, 0x5}});
}

enum class VerifyAbrPageMagic { ON, OFF };
void VerifyAbrMetaDataPage(const abr_metadata_ext& abr_data, uint8_t value,
                           VerifyAbrPageMagic opt = VerifyAbrPageMagic::ON) {
  uint8_t expected[ABR_WEAR_LEVELING_ABR_DATA_SIZE];
  memset(expected, value, ABR_WEAR_LEVELING_ABR_DATA_SIZE);
  ASSERT_BYTES_EQ(&abr_data, expected, ABR_WEAR_LEVELING_ABR_DATA_SIZE);
  if (opt == VerifyAbrPageMagic::ON) {
    uint8_t magic[] = {
        ABR_WEAR_LEVELING_MAGIC_BYTE_0,
        ABR_WEAR_LEVELING_MAGIC_BYTE_1,
        ABR_WEAR_LEVELING_MAGIC_BYTE_2,
        ABR_WEAR_LEVELING_MAGIC_BYTE_3,
    };
    ASSERT_BYTES_EQ(abr_data.magic, magic, ABR_WEAR_LEVELING_MAGIC_LEN);
  }
}

TEST_F(SyncClientBufferedTest, AbrWearLevelingUnsupportedLayout) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));
  sysconfig::SyncClientAbrWearLeveling astro_client(*std::move(client));
  size_t partition_size;
  ASSERT_OK(astro_client.GetPartitionSize(PartitionType::kABRMetadata, &partition_size));

  zx::vmo vmo;
  ASSERT_NO_FATAL_FAILURE(CreatePayload(partition_size, &vmo, 0xa5));
  ASSERT_OK(astro_client.WritePartition(PartitionType::kABRMetadata, vmo, 0));
  ASSERT_OK(astro_client.Flush());

  // The new abr data should still be at the 16th page as it will use default flush.
  ASSERT_NO_FATAL_FAILURE(
      ValidateMemory({{PartitionType::kABRMetadata, 60 * kKilobyte, 4 * kKilobyte, 0xa5}}));

  fzl::OwnedVmoMapper mapper;
  ASSERT_OK(mapper.CreateAndMap(partition_size, "test"));
  ASSERT_OK(astro_client.ReadPartition(PartitionType::kABRMetadata, mapper.vmo(), 0));
  abr_metadata_ext abr_data;
  memcpy(&abr_data, mapper.start(), sizeof(abr_metadata_ext));
  ASSERT_NO_FATAL_FAILURE(VerifyAbrMetaDataPage(abr_data, 0xa5, VerifyAbrPageMagic::OFF));
}

sysconfig_header WriteHeaderSupportingAbrWearLeveling(void* memory) {
  // Provide a supporting header in storage.
  auto header = sysconfig::SyncClientAbrWearLeveling::GetAbrWearLevelingSupportedLayout();
  memcpy(memory, &header, sizeof(header));
  return header;
}

TEST_F(SyncClientBufferedTest, AbrWearLeveling) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));
  sysconfig::SyncClientAbrWearLeveling astro_client(*std::move(client));
  auto memory = static_cast<uint8_t*>(device_->mapper().start()) + 4 * kBlockSize;
  auto header = WriteHeaderSupportingAbrWearLeveling(memory);

  uint8_t empty_page[kPageSize];
  memset(empty_page, 0xff, sizeof(empty_page));
  size_t abr_part_size;
  ASSERT_OK(astro_client.GetPartitionSize(PartitionType::kABRMetadata, &abr_part_size));
  const size_t num_pages = header.abr_metadata.size / kPageSize;
  // Verify that client can write abr meta data <num_pages> times without an erase.
  for (uint8_t i = 0; i < num_pages; i++) {
    zx::vmo vmo;
    // Fill the payload with value |i+1|.
    ASSERT_NO_FATAL_FAILURE(CreatePayload(abr_part_size, &vmo, i + 1));
    ASSERT_OK(astro_client.WritePartition(PartitionType::kABRMetadata, vmo, 0));
    ASSERT_OK(astro_client.Flush());
    ASSERT_EQ(astro_client.GetEraseCount(), 0);

    abr_metadata_ext abr_data;

    // Validate that memory is as expected:
    //
    // 1. Previous and newly written pages are as expected.
    auto abr_subpart = memory + header.abr_metadata.offset;
    for (uint8_t j = 0; j <= i; j++) {
      memcpy(&abr_data, abr_subpart + j * kPageSize, sizeof(abr_data));
      ASSERT_NO_FATAL_FAILURE(VerifyAbrMetaDataPage(abr_data, j + 1));
    }
    // 2. Pages after stay empty.
    for (uint8_t j = i + 1; j < num_pages; j++) {
      ASSERT_BYTES_EQ(abr_subpart + j * kPageSize, empty_page, kPageSize, "@ page %d", j);
    }

    // Verify that latest abr meta data can be correctly read.
    fzl::OwnedVmoMapper mapper;
    ASSERT_OK(mapper.CreateAndMap(abr_part_size, "test"));
    ASSERT_OK(astro_client.ReadPartition(PartitionType::kABRMetadata, mapper.vmo(), 0));

    memcpy(&abr_data, mapper.start(), sizeof(abr_metadata_ext));
    ASSERT_NO_FATAL_FAILURE(VerifyAbrMetaDataPage(abr_data, i + 1));
  }

  // Verify that the |num_pages + 1|th write should introduce an erase.
  zx::vmo vmo;
  ASSERT_NO_FATAL_FAILURE(CreatePayload(abr_part_size, &vmo, 0xAB));
  ASSERT_OK(astro_client.WritePartition(PartitionType::kABRMetadata, vmo, 0));
  ASSERT_OK(astro_client.Flush());
  ASSERT_EQ(astro_client.GetEraseCount(), 1);

  // Verify that the (<num_pages> + 1)th write is correct.
  fzl::OwnedVmoMapper mapper;
  ASSERT_OK(mapper.CreateAndMap(abr_part_size, "test"));
  ASSERT_OK(astro_client.ReadPartition(PartitionType::kABRMetadata, mapper.vmo(), 0));

  abr_metadata_ext abr_data;
  memcpy(&abr_data, mapper.start(), sizeof(abr_metadata_ext));
  ASSERT_NO_FATAL_FAILURE(VerifyAbrMetaDataPage(abr_data, 0xAB));
}

TEST_F(SyncClientBufferedTest, AbrWearLevelingMultiplePartitionsModifiedInCache) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));
  sysconfig::SyncClientAbrWearLeveling astro_client(*std::move(client));
  auto memory = static_cast<uint8_t*>(device_->mapper().start()) + 4 * kBlockSize;
  auto header = WriteHeaderSupportingAbrWearLeveling(memory);

  auto write_partition = [&](PartitionType partition, uint8_t data) {
    zx::vmo vmo;
    size_t part_size;
    ASSERT_OK(astro_client.GetPartitionSize(partition, &part_size));
    ASSERT_NO_FATAL_FAILURE(CreatePayload(part_size, &vmo, data));
    ASSERT_OK(astro_client.WritePartition(partition, vmo, 0));
  };

  // Write multiple sub-partitions: vb_a, vb_r and abr
  ASSERT_NO_FATAL_FAILURE(write_partition(PartitionType::kVerifiedBootMetadataA, 0xAB));
  ASSERT_NO_FATAL_FAILURE(write_partition(PartitionType::kVerifiedBootMetadataR, 0xCD));
  ASSERT_NO_FATAL_FAILURE(write_partition(PartitionType::kABRMetadata, 0xEF));

  // Verify that for flushing changes more than just abr metadata introduce an erase
  ASSERT_EQ(astro_client.GetEraseCount(), 0);
  ASSERT_OK(astro_client.Flush());
  ASSERT_EQ(astro_client.GetEraseCount(), 1);

  // Verify that abr data is correctly written
  size_t abr_part_size;
  ASSERT_OK(astro_client.GetPartitionSize(PartitionType::kABRMetadata, &abr_part_size));
  fzl::OwnedVmoMapper mapper;
  ASSERT_OK(mapper.CreateAndMap(abr_part_size, "test"));
  ASSERT_OK(astro_client.ReadPartition(PartitionType::kABRMetadata, mapper.vmo(), 0));

  abr_metadata_ext abr_data;
  memcpy(&abr_data, mapper.start(), sizeof(abr_metadata_ext));
  ASSERT_NO_FATAL_FAILURE(VerifyAbrMetaDataPage(abr_data, 0xEF));

  // Veiry that all partition are written correctly
  ASSERT_NO_FATAL_FAILURE(ValidateMemory(
      {// a place holder header partition to exempt it from validation
       {.partition_offset = 0, .partition_size = 4 * kKilobyte, .write_value = {}},
       // dont care sysconfig
       {PartitionType::kSysconfig, header.sysconfig_data.offset, header.sysconfig_data.size, {}},
       // abr metadata in the first page has just been validated. Exempt it.
       {PartitionType::kABRMetadata, header.abr_metadata.offset, kPageSize, {}},
       // the rest of pages in abr partition should be reset to 0xff
       {PartitionType::kABRMetadata, header.abr_metadata.offset + kPageSize,
        header.abr_metadata.size - kPageSize, 0xff},
       // vb metadata a
       {PartitionType::kVerifiedBootMetadataA, header.vb_metadata_a.offset,
        header.vb_metadata_a.size, 0xAB},
       // vb metadata r
       {PartitionType::kVerifiedBootMetadataR, header.vb_metadata_r.offset,
        header.vb_metadata_r.size, 0xCD}}));

  // Introduces an additional abr writes.
  // Since we just performed a reset flush, there should be enough empty pages for the
  // write. It shouldn't introduce additional erase.
  ASSERT_NO_FATAL_FAILURE(write_partition(PartitionType::kABRMetadata, 0x01));

  ASSERT_OK(astro_client.Flush());
  ASSERT_EQ(astro_client.GetEraseCount(), 1);

  // Read back the new abr meta and validate it is correct.
  ASSERT_OK(astro_client.ReadPartition(PartitionType::kABRMetadata, mapper.vmo(), 0));
  memcpy(&abr_data, mapper.start(), sizeof(abr_metadata_ext));
  ASSERT_NO_FATAL_FAILURE(VerifyAbrMetaDataPage(abr_data, 0x1));
}

TEST_F(SyncClientBufferedTest, AbrWearLevelingDefaultToFirstPage) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));
  sysconfig::SyncClientAbrWearLeveling astro_client(*std::move(client));
  auto memory = static_cast<uint8_t*>(device_->mapper().start()) + 4 * kBlockSize;
  auto header = WriteHeaderSupportingAbrWearLeveling(memory);

  // Write data to all pages such that none contains valid magic values.
  const size_t num_pages = header.abr_metadata.size / kPageSize;
  for (uint8_t i = 0; i < num_pages; i++) {
    zx::vmo vmo;
    WriteData(header.abr_metadata.offset + i * kPageSize, kPageSize, i + 1);
  }

  // Verify that the read will default to the 1st page in abr sub-partition.
  fzl::OwnedVmoMapper mapper;
  ASSERT_OK(mapper.CreateAndMap(kPageSize, "test"));
  ASSERT_OK(astro_client.ReadPartition(PartitionType::kABRMetadata, mapper.vmo(), 0));

  abr_metadata_ext abr_data;
  memcpy(&abr_data, mapper.start(), sizeof(abr_metadata_ext));
  ASSERT_NO_FATAL_FAILURE(VerifyAbrMetaDataPage(abr_data, 0x1, VerifyAbrPageMagic::OFF));
}

TEST_F(SyncClientBufferedTest, ValidateAbrMetadataInStorageFail) {
  std::optional<sysconfig::SyncClient> client;
  ASSERT_OK(sysconfig::SyncClient::Create(device_->devfs_root(), &client));
  sysconfig::SyncClientAbrWearLeveling astro_client(*std::move(client));
  auto memory = static_cast<uint8_t*>(device_->mapper().start()) + 4 * kBlockSize;
  auto header = WriteHeaderSupportingAbrWearLeveling(memory);

  size_t abr_part_size;
  ASSERT_OK(astro_client.GetPartitionSize(PartitionType::kABRMetadata, &abr_part_size));
  const size_t num_pages = header.abr_metadata.size / kPageSize;
  // Verify that abr read validation can detect error wherever the latest abr page is.
  for (uint8_t i = 0; i < num_pages; i++) {
    zx::vmo vmo;
    // Fill the payload with value |i+1|.
    ASSERT_NO_FATAL_FAILURE(CreatePayload(abr_part_size, &vmo, i + 1));
    ASSERT_OK(astro_client.WritePartition(PartitionType::kABRMetadata, vmo, 0));
    ASSERT_OK(astro_client.Flush());

    abr_metadata_ext abr_data;
    memset(abr_data.abr_data, i, sizeof(abr_data.abr_data));
    ASSERT_NOT_OK(astro_client.ValidateAbrMetadataInStorage(&abr_data));
  }
}

}  // namespace
