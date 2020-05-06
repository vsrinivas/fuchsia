// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <lib/sysconfig/sync-client.h>
#include <lib/sysconfig/sysconfig-header.h>

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

void CreatePayload(size_t size, zx::vmo* out, uint8_t data = 0x4a) {
  zx::vmo vmo;
  fzl::VmoMapper mapper;
  ASSERT_OK(mapper.CreateAndMap(fbl::round_up(size, ZX_PAGE_SIZE),
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
  SyncClientTest() { ASSERT_NO_FATAL_FAILURES(SkipBlockDevice::Create(kNandInfo, &device_)); }

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

  void WriteData(size_t offset, size_t size) {
    for (size_t block = 4; block < 7; block++) {
      uint8_t* start =
          static_cast<uint8_t*>(device_->mapper().start()) + (block * kBlockSize) + offset;
      memset(start, 0x5c, size);
    }
  }

  void TestLayoutUpdate(const std::optional<sysconfig_header> current_header,
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
    ASSERT_NO_FATAL_FAILURES(ValidateWritten(item.new_info.offset, content_size, item.expected));
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
    ASSERT_NO_FATAL_FAILURES(ValidateBuffer(vmo.start(), content_size, item.expected));
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

namespace {
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
}  // namespace

TEST_F(SyncClientTest, UpdateLayoutShrinkConfigDataExpandAbrAtEnd) {
  TestLayoutUpdate(std::nullopt, shrunken_configdata_abr_expand_at_end);
}

TEST_F(SyncClientTest, UpdateLayoutEmptyConfigDataExpandAbrAtEnd) {
  TestLayoutUpdate(std::nullopt, empty_configdata_abr_expand_at_end);
}

TEST_F(SyncClientTest, UpdateLayoutFromShrukenToEmptyConfigData) {
  TestLayoutUpdate(shrunken_configdata_abr_expand_at_end, empty_configdata_abr_expand_at_end);
}

}  // namespace
