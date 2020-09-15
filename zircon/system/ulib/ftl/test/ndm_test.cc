// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ftl/ndm-driver.h>

#include <optional>
#include <vector>

#include <zxtest/zxtest.h>

#include "ftl.h"
#include "ndm/ndmp.h"

namespace {

constexpr uint32_t kNumBlocks = 30;
constexpr uint32_t kPagesPerBlock = 16;
constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kOobSize = 16;
constexpr uint32_t kBlockSize = kPageSize * kPagesPerBlock;

constexpr uint32_t kControlPage0 = (kNumBlocks - 1) * kPagesPerBlock;
constexpr uint32_t kControlPage1 = (kNumBlocks - 2) * kPagesPerBlock;

constexpr ftl::VolumeOptions kDefaultOptions = {kNumBlocks, 2, kBlockSize, kPageSize, kOobSize, 0};

class NdmRamDriver final : public ftl::NdmBaseDriver {
 public:
  NdmRamDriver(const ftl::VolumeOptions options = kDefaultOptions) : options_(options) {}
  ~NdmRamDriver() final {}

  const uint8_t* data(uint32_t page_num) const { return &volume_[page_num * kPageSize]; }
  NDM ndm() { return GetNdmForTest(); }
  void format_using_v2(bool value) { format_using_v2_ = value; }

  // Goes through the normal logic to create a volume with user data info.
  const char* CreateVolume(std::optional<ftl::LoggerProxy> logger = std::nullopt) {
    return CreateNdmVolumeWithLogger(nullptr, options_, true, logger);
  }

  // NdmDriver interface:
  const char* Init() final;
  const char* Attach(const ftl::Volume* ftl_volume) final;
  bool Detach() final;
  int NandRead(uint32_t start_page, uint32_t page_count, void* page_buffer, void* oob_buffer) final;
  int NandWrite(uint32_t start_page, uint32_t page_count, const void* page_buffer,
                const void* oob_buffer) final;
  int NandErase(uint32_t page_num) final;
  int IsBadBlock(uint32_t page_num) final { return ftl::kFalse; }
  bool IsEmptyPage(uint32_t page_num, const uint8_t* data, const uint8_t* spare) final {
    return IsEmptyPageImpl(data, kPageSize, spare, kOobSize);
  }

 private:
  std::vector<uint8_t> volume_;
  ftl::VolumeOptions options_;
  bool format_using_v2_ = true;
};

const char* NdmRamDriver::Init() {
  size_t volume_size = (kPageSize + kOobSize) * kPagesPerBlock * kNumBlocks;
  volume_.resize(volume_size);
  memset(volume_.data(), 0xff, volume_size);
  return nullptr;
}

const char* NdmRamDriver::Attach(const ftl::Volume* ftl_volume) {
  if (!GetNdmForTest()) {
    IsNdmDataPresent(options_, format_using_v2_);
  }
  return GetNdmForTest() ? nullptr : "Failed to add device";
}

bool NdmRamDriver::Detach() { return RemoveNdmVolume(); }

int NdmRamDriver::NandRead(uint32_t start_page, uint32_t page_count, void* page_buffer,
                           void* oob_buffer) {
  uint8_t* data = reinterpret_cast<uint8_t*>(page_buffer);
  uint8_t* spare = reinterpret_cast<uint8_t*>(oob_buffer);

  if (data) {
    memcpy(data, &volume_[start_page * kPageSize], page_count * kPageSize);
  }

  if (spare) {
    uint32_t oob_offset = kPagesPerBlock * kNumBlocks * kPageSize;
    memcpy(spare, &volume_[oob_offset + start_page * kOobSize], page_count * kOobSize);
  }
  return ftl::kNdmOk;
}

int NdmRamDriver::NandWrite(uint32_t start_page, uint32_t page_count, const void* page_buffer,
                            const void* oob_buffer) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(page_buffer);
  const uint8_t* spare = reinterpret_cast<const uint8_t*>(oob_buffer);
  ZX_ASSERT(data);
  ZX_ASSERT(spare);

  uint32_t oob_offset = kPagesPerBlock * kNumBlocks * kPageSize;
  memcpy(&volume_[start_page * kPageSize], data, page_count * kPageSize);
  memcpy(&volume_[oob_offset + start_page * kOobSize], spare, page_count * kOobSize);
  return ftl::kNdmOk;
}

int NdmRamDriver::NandErase(uint32_t page_num) {
  ZX_ASSERT(page_num % kPagesPerBlock == 0);

  uint32_t oob_offset = kPagesPerBlock * kNumBlocks * kPageSize;
  memset(&volume_[page_num * kPageSize], 0xFF, kBlockSize);
  memset(&volume_[oob_offset + page_num * kOobSize], 0xFF, kPagesPerBlock * kOobSize);

  return ftl::kNdmOk;
}

class NdmTest : public zxtest::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(ftl::InitModules());
    ASSERT_NULL(ndm_driver_.Init());
    ASSERT_NULL(ndm_driver_.Attach(nullptr));
  }

 protected:
  NdmRamDriver ndm_driver_;
};

struct HeaderV1 {
  uint16_t current_location;
  uint16_t last_location;
  int32_t sequence_num;
  uint32_t crc;
  int32_t num_blocks;
  int32_t block_size;
  int32_t control_block0;
  int32_t control_block1;
  int32_t free_virt_block;
  int32_t free_control_block;
  int32_t transfer_to_block;
};

struct HeaderV2 {
  uint16_t major_version;
  uint16_t minor_version;
  HeaderV1 v1;
};

class NdmTestOldFormat : public NdmTest {
 public:
  void SetUp() override {
    ASSERT_TRUE(ftl::InitModules());
    ASSERT_NULL(ndm_driver_.Init());
    ndm_driver_.format_using_v2(false);
    ASSERT_NULL(ndm_driver_.Attach(nullptr));
  }
};

TEST_F(NdmTestOldFormat, WritesVersion1) {
  auto header = reinterpret_cast<const HeaderV1*>(ndm_driver_.data(kControlPage0));
  EXPECT_EQ(1, header->current_location);
  EXPECT_EQ(1, header->last_location);
  EXPECT_EQ(0, header->sequence_num);
  EXPECT_EQ(kNumBlocks, header->num_blocks);
  EXPECT_EQ(kPageSize * kPagesPerBlock, header->block_size);
  EXPECT_EQ(kNumBlocks - 1, header->control_block0);
  EXPECT_EQ(kNumBlocks - 2, header->control_block1);
  EXPECT_EQ(kNumBlocks - 4, header->free_virt_block);
  EXPECT_EQ(kNumBlocks - 3, header->free_control_block);
  EXPECT_EQ(-1, header->transfer_to_block);
}

TEST_F(NdmTest, WritesVersion2) {
  auto header = reinterpret_cast<const HeaderV2*>(ndm_driver_.data(kControlPage0));
  EXPECT_EQ(2, header->major_version);
  EXPECT_EQ(0, header->minor_version);
  EXPECT_EQ(1, header->v1.current_location);
  EXPECT_EQ(1, header->v1.last_location);
  EXPECT_EQ(0, header->v1.sequence_num);
  EXPECT_EQ(kNumBlocks, header->v1.num_blocks);
  EXPECT_EQ(kPageSize * kPagesPerBlock, header->v1.block_size);
  EXPECT_EQ(kNumBlocks - 1, header->v1.control_block0);
  EXPECT_EQ(kNumBlocks - 2, header->v1.control_block1);
  EXPECT_EQ(kNumBlocks - 4, header->v1.free_virt_block);
  EXPECT_EQ(kNumBlocks - 3, header->v1.free_control_block);
  EXPECT_EQ(-1, header->v1.transfer_to_block);
}

TEST_F(NdmTest, OnlyOneControlBlock) {
  auto header = reinterpret_cast<const HeaderV1*>(ndm_driver_.data(kControlPage0 + 1));
  EXPECT_EQ(0xffff, header->current_location);

  header = reinterpret_cast<const HeaderV1*>(ndm_driver_.data(kControlPage1));
  EXPECT_EQ(0xffff, header->current_location);
}

TEST_F(NdmTestOldFormat, NoVersion2) {
  NDMPartition partition = {};
  partition.num_blocks = ndmGetNumVBlocks(ndm_driver_.ndm());
  ASSERT_EQ(0, ndmWritePartition(ndm_driver_.ndm(), &partition, 0, "foo"));

  EXPECT_NOT_NULL(ndmGetPartition(ndm_driver_.ndm(), 0));
  EXPECT_NULL(ndmGetPartitionInfo(ndm_driver_.ndm()));
}

TEST_F(NdmTest, UsesVersion2) {
  NDMPartitionInfo partition = {};
  uint32_t partition_size = ndmGetNumVBlocks(ndm_driver_.ndm());
  partition.basic_data.num_blocks = partition_size;
  strcpy(partition.basic_data.name, "foo");
  ASSERT_EQ(0, ndmWritePartitionInfo(ndm_driver_.ndm(), &partition));

  EXPECT_NOT_NULL(ndmGetPartition(ndm_driver_.ndm(), 0));

  const NDMPartitionInfo* info = ndmGetPartitionInfo(ndm_driver_.ndm());
  ASSERT_NOT_NULL(info);
  EXPECT_EQ(0, info->basic_data.first_block);
  EXPECT_EQ(partition_size, info->basic_data.num_blocks);
  EXPECT_EQ(0, info->user_data.data_size);
  EXPECT_STR_EQ("foo", info->basic_data.name);
}

TEST_F(NdmTest, SavesVersion2) {
  NDMPartitionInfo partition = {};
  partition.basic_data.num_blocks = ndmGetNumVBlocks(ndm_driver_.ndm());
  ASSERT_EQ(0, ndmWritePartitionInfo(ndm_driver_.ndm(), &partition));
  ASSERT_EQ(0, ndmSavePartitionTable(ndm_driver_.ndm()));

  auto header = reinterpret_cast<const HeaderV2*>(ndm_driver_.data(kControlPage0 + 1));
  EXPECT_EQ(2, header->major_version);
  EXPECT_EQ(0, header->minor_version);
  EXPECT_EQ(1, header->v1.current_location);
  EXPECT_EQ(1, header->v1.last_location);
  EXPECT_EQ(1, header->v1.sequence_num);
  EXPECT_EQ(kNumBlocks, header->v1.num_blocks);
  EXPECT_EQ(kPageSize * kPagesPerBlock, header->v1.block_size);
  EXPECT_EQ(kNumBlocks - 1, header->v1.control_block0);
  EXPECT_EQ(kNumBlocks - 2, header->v1.control_block1);
  EXPECT_EQ(kNumBlocks - 4, header->v1.free_virt_block);
  EXPECT_EQ(kNumBlocks - 3, header->v1.free_control_block);
  EXPECT_EQ(-1, header->v1.transfer_to_block);
}

TEST_F(NdmTest, OnlyOneV2ControlBlock) {
  NDMPartitionInfo partition = {};
  partition.basic_data.num_blocks = ndmGetNumVBlocks(ndm_driver_.ndm());
  ASSERT_EQ(0, ndmWritePartitionInfo(ndm_driver_.ndm(), &partition));
  ASSERT_EQ(0, ndmSavePartitionTable(ndm_driver_.ndm()));

  auto header = reinterpret_cast<const HeaderV2*>(ndm_driver_.data(kControlPage0 + 2));
  EXPECT_EQ(0xffff, header->major_version);

  header = reinterpret_cast<const HeaderV2*>(ndm_driver_.data(kControlPage1));
  EXPECT_EQ(0xffff, header->major_version);
}

TEST_F(NdmTest, SavesUpdatedPartitionData) {
  NDMPartitionInfo partition = {};
  partition.basic_data.num_blocks = ndmGetNumVBlocks(ndm_driver_.ndm());
  ASSERT_EQ(0, ndmWritePartitionInfo(ndm_driver_.ndm(), &partition));

  // Write three new control blocks.
  ASSERT_EQ(0, ndmSavePartitionTable(ndm_driver_.ndm()));
  ASSERT_EQ(0, ndmSavePartitionTable(ndm_driver_.ndm()));
  ASSERT_EQ(0, ndmSavePartitionTable(ndm_driver_.ndm()));

  auto header = reinterpret_cast<const HeaderV2*>(ndm_driver_.data(kControlPage0 + 1));
  EXPECT_EQ(2, header->major_version);
  EXPECT_EQ(1, header->v1.sequence_num);

  header = reinterpret_cast<const HeaderV2*>(ndm_driver_.data(kControlPage0 + 2));
  EXPECT_EQ(2, header->major_version);
  EXPECT_EQ(2, header->v1.sequence_num);

  header = reinterpret_cast<const HeaderV2*>(ndm_driver_.data(kControlPage0 + 3));
  EXPECT_EQ(2, header->major_version);
  EXPECT_EQ(3, header->v1.sequence_num);

  header = reinterpret_cast<const HeaderV2*>(ndm_driver_.data(kControlPage0 + 4));
  EXPECT_EQ(0xffff, header->major_version);

  header = reinterpret_cast<const HeaderV2*>(ndm_driver_.data(kControlPage1));
  EXPECT_EQ(0xffff, header->major_version);
}

union PartitionInfo {
  NDMPartitionInfo ndm;
  struct {
    NDMPartition basic_data;
    uint32_t data_size;
    uint32_t data;
  } exploded;
};
static_assert(sizeof(NDMPartition) + sizeof(uint32_t) == sizeof(NDMPartitionInfo));
static_assert(sizeof(NDMPartitionInfo) + sizeof(uint32_t) == sizeof(PartitionInfo));

// Tests that the user portion of the partition info can grow.
TEST_F(NdmTest, UpdatesUserData) {
  NDMPartitionInfo partition = {};
  partition.basic_data.num_blocks = ndmGetNumVBlocks(ndm_driver_.ndm());
  ASSERT_EQ(0, ndmWritePartitionInfo(ndm_driver_.ndm(), &partition));
  ASSERT_EQ(0, ndmSavePartitionTable(ndm_driver_.ndm()));

  // Reinitialize NDM.
  EXPECT_TRUE(ndm_driver_.Detach());
  ASSERT_NULL(ndm_driver_.Attach(nullptr));

  // Redefine the partition.
  PartitionInfo new_info = {};
  new_info.exploded.basic_data = partition.basic_data;
  new_info.exploded.data_size = sizeof(new_info.exploded.data);
  new_info.exploded.data = 42;
  ASSERT_EQ(0, ndmWritePartitionInfo(ndm_driver_.ndm(), &new_info.ndm));
  ASSERT_EQ(0, ndmSavePartitionTable(ndm_driver_.ndm()));

  // Read the latest version from disk.
  EXPECT_TRUE(ndm_driver_.Detach());
  ASSERT_NULL(ndm_driver_.Attach(nullptr));

  const NDMPartitionInfo* info = ndmGetPartitionInfo(ndm_driver_.ndm());
  ASSERT_NOT_NULL(info);
  ASSERT_EQ(sizeof(new_info.exploded.data), info->user_data.data_size);

  auto actual_info = reinterpret_cast<const PartitionInfo*>(info);
  EXPECT_EQ(42, actual_info->exploded.data);

  // Verify the expected disk layout.
  auto header = reinterpret_cast<const HeaderV2*>(ndm_driver_.data(kControlPage0 + 1));
  EXPECT_EQ(2, header->major_version);
  EXPECT_EQ(1, header->v1.sequence_num);

  header = reinterpret_cast<const HeaderV2*>(ndm_driver_.data(kControlPage1));
  EXPECT_EQ(2, header->major_version);
  EXPECT_EQ(2, header->v1.sequence_num);

  header = reinterpret_cast<const HeaderV2*>(ndm_driver_.data(kControlPage0 + 2));
  EXPECT_EQ(0xffff, header->major_version);

  header = reinterpret_cast<const HeaderV2*>(ndm_driver_.data(kControlPage1 + 1));
  EXPECT_EQ(0xffff, header->major_version);
}

TEST_F(NdmTest, BaseDriverSavesConfig) {
  ASSERT_NULL(ndm_driver_.CreateVolume());

  const NDMPartitionInfo* info = ndmGetPartitionInfo(ndm_driver_.ndm());
  ASSERT_NOT_NULL(info);
  ASSERT_GE(info->user_data.data_size, 96);  // Size of the first version of the data.

  const ftl::VolumeOptions* options = ndm_driver_.GetSavedOptions();
  ASSERT_NOT_NULL(options);
  ASSERT_BYTES_EQ(&kDefaultOptions, options, sizeof(*options));
}

class NdmReadOnlyTest : public zxtest::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(ftl::InitModules());
    ftl::VolumeOptions options = kDefaultOptions;
    options.flags |= ftl::kReadOnlyInit;
    ndm_driver_.reset(new NdmRamDriver(options));
    ASSERT_NULL(ndm_driver_->Init());
    buffer_ = std::vector<uint8_t>(kPageSize, 0xff);
  }

 protected:
  std::unique_ptr<NdmRamDriver> ndm_driver_;
  std::vector<uint8_t> buffer_;
};

// An NDM control block version 1, stored on page 29.
constexpr uint32_t kControl29V1[] = {
    0x00010001, 0x00000000, 0x4efa26dd, 0x0000001e, 0x00010000, 0x0000001d, 0x0000001c, 0x0000001a,
    0x0000001b, 0xffffffff, 0x00000000, 0x0000001e, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};

constexpr uint32_t kControlOob[] = {0x4d444eff, 0x31304154, 0xffffffff, 0x00ffffff};

TEST_F(NdmReadOnlyTest, Version1Only) {
  memcpy(buffer_.data(), kControl29V1, sizeof(kControl29V1));
  ASSERT_EQ(ftl::kNdmOk, ndm_driver_->NandWrite(kControlPage0, 1, buffer_.data(), kControlOob));

  ASSERT_NULL(ndm_driver_->CreateVolume());
}

// An NDM control block version 2.0, stored on page 29.
constexpr uint32_t kControl29V2[] = {
    0x00000002, 0x00010001, 0x00000000, 0x061cc64a, 0x0000001e, 0x00010000, 0x0000001d, 0x0000001c,
    0x0000001a, 0x0000001b, 0xffffffff, 0xffffffff, 0xffffffff, 0x00000000, 0x0000001e, 0xffffffff};

TEST_F(NdmReadOnlyTest, Version2Only) {
  memcpy(buffer_.data(), kControl29V2, sizeof(kControl29V2));
  ASSERT_EQ(ftl::kNdmOk, ndm_driver_->NandWrite(kControlPage0, 1, buffer_.data(), kControlOob));

  ASSERT_NULL(ndm_driver_->CreateVolume());
}

// An NDM control block version 2.0, stored on page 28, with partition data.
constexpr uint32_t kControl28V2[] = {
    0x00000002, 0x00010001, 0x00000001, 0x41220f07, 0x0000001e, 0x00010000, 0x0000001d, 0x0000001c,
    0x0000001a, 0x0000001b, 0xffffffff, 0xffffffff, 0xffffffff, 0x00000001, 0x0000001e, 0xffffffff,
    0xffffffff, 0x00000000, 0x0000001a, 0x006c7466, 0x00000000, 0x00000000, 0x00000000, 0x00000060,
    0x00000001, 0x00000004, 0x00000006, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x0000001e, 0x00000002, 0x00010000, 0x00001000, 0x00000010, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000};

TEST_F(NdmReadOnlyTest, UpgradedVersion2) {
  memcpy(buffer_.data(), kControl29V1, sizeof(kControl29V1));
  ASSERT_EQ(ftl::kNdmOk, ndm_driver_->NandWrite(kControlPage0, 1, buffer_.data(), kControlOob));

  memcpy(buffer_.data(), kControl28V2, sizeof(kControl28V2));
  ASSERT_EQ(ftl::kNdmOk, ndm_driver_->NandWrite(kControlPage1, 1, buffer_.data(), kControlOob));
  ASSERT_NULL(ndm_driver_->CreateVolume());
}

// An NDM control block version 1, stored on page 29, with one factory bad block and
// a second bad block in the process of being relocated.
constexpr uint32_t kControlBlockTransferV1[] = {
    0x00010001, 0x00000001, 0xcd0deda6, 0x0000001e, 0x00010000, 0x0000001d, 0x0000001c, 0xffffffff,
    0xffffffff, 0x0000001b, 0x00000003, 0x0000000d, 0x00000102, 0x00000000, 0x00001e00, 0x00000300,
    0x00001b00, 0xffffff00, 0xffffffff, 0x000000ff, 0x00001a00, 0x6c746600, 0x00000000, 0x00000000,
    0x00000000, 0xffffff00, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};

TEST_F(NdmReadOnlyTest, InTransferV1) {
  memcpy(buffer_.data(), kControlBlockTransferV1, sizeof(kControlBlockTransferV1));
  ASSERT_EQ(ftl::kNdmOk, ndm_driver_->NandWrite(kControlPage0, 1, buffer_.data(), kControlOob));

  ASSERT_NOT_NULL(ndm_driver_->CreateVolume());
  ASSERT_EQ(NDM_BAD_BLK_RECOV, GetFsErrCode());
}

// An NDM control block version 1, stored on page 29, with one factory bad block and
// one translated bad block.
constexpr uint32_t kControlBlockBadBlocksV1[] = {
    0x00010001, 0x00000002, 0x64342dc5, 0x0000001e, 0x00010000, 0x0000001d, 0x0000001c, 0xffffffff,
    0xffffffff, 0xffffffff, 0x00000001, 0x00000000, 0x0000001e, 0x00000003, 0x0000001b, 0xffffffff,
    0xffffffff, 0x00000000, 0x0000001a, 0x006c7466, 0x00000000, 0x00000000, 0x00000000, 0xffffffff};

TEST_F(NdmReadOnlyTest, BadBlocksV1) {
  memcpy(buffer_.data(), kControlBlockBadBlocksV1, sizeof(kControlBlockBadBlocksV1));
  ASSERT_EQ(ftl::kNdmOk, ndm_driver_->NandWrite(kControlPage0, 1, buffer_.data(), kControlOob));

  ASSERT_NULL(ndm_driver_->CreateVolume());
  EXPECT_EQ(2, ndm_driver_->ndm()->num_bad_blks);
}

// An NDM control block version 2.0, stored on page 29, with one factory bad block and
// a second bad block in the process of being relocated.
constexpr uint32_t kControlBlockTransferV2[] = {
    0x00000002, 0x00010001, 0x00000001, 0xdc1fd63c, 0x0000001e, 0x00010000, 0x0000001d, 0x0000001c,
    0xffffffff, 0xffffffff, 0x0000001b, 0x00000003, 0x0000000d, 0x00000001, 0x00000000, 0x0000001e,
    0x00000003, 0x0000001b, 0xffffffff, 0xffffffff, 0x00000000, 0x0000001a, 0x006c7466, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};

TEST_F(NdmReadOnlyTest, InTransferV2) {
  memcpy(buffer_.data(), kControlBlockTransferV2, sizeof(kControlBlockTransferV2));
  ASSERT_EQ(ftl::kNdmOk, ndm_driver_->NandWrite(kControlPage0, 1, buffer_.data(), kControlOob));

  ASSERT_NOT_NULL(ndm_driver_->CreateVolume());
  ASSERT_EQ(NDM_BAD_BLK_RECOV, GetFsErrCode());
}

// An NDM control block version 2.0, stored on page 29, with one factory bad block and
// one translated bad block.
constexpr uint32_t kControlBlockBadBlocksV2[] = {
    0x00000002, 0x00010001, 0x00000002, 0x01148752, 0x0000001e, 0x00010000, 0x0000001d, 0x0000001c,
    0xffffffff, 0xffffffff, 0xffffffff, 0x00000003, 0x0000000d, 0x00000001, 0x00000000, 0x0000001e,
    0x00000003, 0x0000001b, 0xffffffff, 0xffffffff, 0x00000000, 0x0000001a, 0x006c7466, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};

TEST_F(NdmReadOnlyTest, BadBlocksV2) {
  static bool logger_called = false;
  logger_called = false;

  class LoggerHelper {
   public:
    static void Log(const char* _, ...) __PRINTFLIKE(1, 2) { logger_called = true; }
  };

  ftl::LoggerProxy logger;
  logger.trace = &LoggerHelper::Log;
  logger.debug = &LoggerHelper::Log;
  logger.info = &LoggerHelper::Log;
  logger.warn = &LoggerHelper::Log;
  logger.error = &LoggerHelper::Log;

  memcpy(buffer_.data(), kControlBlockBadBlocksV2, sizeof(kControlBlockBadBlocksV2));
  ASSERT_EQ(ftl::kNdmOk, ndm_driver_->NandWrite(kControlPage0, 1, buffer_.data(), kControlOob));

  ASSERT_NULL(ndm_driver_->CreateVolume(logger));
  EXPECT_EQ(2, ndm_driver_->ndm()->num_bad_blks);
  EXPECT_TRUE(logger_called);
}

}  // namespace
