// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "synaptics-bad-block.h"

#include <lib/fzl/vmo-mapper.h>

#include <array>
#include <map>
#include <vector>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <zxtest/zxtest.h>

namespace nand {

class FakeNand : public ddk::NandProtocol<FakeNand> {
 public:
  explicit FakeNand(const nand_info_t& nand_info)
      : ddk::NandProtocol<FakeNand>(), nand_proto_{&nand_protocol_ops_, this}, info_(nand_info) {}

  const nand_protocol_t* GetProto() const { return &nand_proto_; }

  void NandQuery(nand_info_t* out_info, size_t* out_nand_op_size) {
    *out_info = info_;
    *out_nand_op_size = sizeof(nand_operation_t);
  }

  void NandQueue(nand_operation_t* op, nand_queue_callback callback, void* cookie) {
    if (op->command == NAND_OP_READ) {
      callback(cookie, NandOpRead(op->rw), op);
    } else if (op->command == NAND_OP_WRITE) {
      callback(cookie, NandOpWrite(op->rw), op);
    } else if (op->command == NAND_OP_ERASE) {
      callback(cookie, NandOpErase(op->erase), op);
    } else {
      callback(cookie, ZX_ERR_INVALID_ARGS, op);
    }
  }

  zx_status_t NandGetFactoryBadBlockList(uint32_t* out_bad_blocks_list, size_t bad_blocks_count,
                                         size_t* out_bad_blocks_actual) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  std::vector<uint8_t> ReadPageData(uint32_t page, size_t size, size_t offset = 0) {
    if (pages_.find(page) == pages_.end()) {
      return std::vector<uint8_t>(size, 0xff);
    }
    return std::vector<uint8_t>(pages_[page].get() + offset, pages_[page].get() + offset + size);
  }

  std::vector<uint8_t> ReadPageOob(uint32_t page, size_t size, size_t offset = 0) {
    return ReadPageData(page, size, offset + info_.page_size);
  }

  void WritePageData(uint32_t page, const std::vector<uint8_t>& data, size_t offset = 0) {
    if (pages_.find(page) == pages_.end()) {
      pages_[page].reset(new uint8_t[info_.page_size + info_.oob_size]);
      memset(pages_[page].get(), 0xff, info_.page_size + info_.oob_size);
    }

    memcpy(pages_[page].get() + offset, data.data(), data.size());
  }

  void WritePageOob(uint32_t page, const std::vector<uint8_t>& oob, size_t offset = 0) {
    WritePageData(page, oob, offset + info_.page_size);
  }

  void EraseBlock(uint32_t block) {
    const uint32_t start_page = block * info_.pages_per_block;
    for (uint32_t i = 0; i < info_.pages_per_block; i++) {
      pages_.erase(i + start_page);
    }
  }

  void SetBlockBad(uint32_t block) { bad_blocks_.push_back(block); }

  void Reset() {
    pages_.clear();
    bad_blocks_.clear();
  }

 private:
  zx_status_t NandOpRead(const nand_read_write_t& rw);
  zx_status_t NandOpWrite(const nand_read_write_t& rw);
  zx_status_t NandOpErase(const nand_erase_t& erase);

  bool IsPageInBadBlock(uint32_t page) {
    for (const uint32_t block : bad_blocks_) {
      if (page >= (block * info_.pages_per_block) && page < ((block + 1) * info_.pages_per_block)) {
        return true;
      }
    }

    return false;
  }

  const nand_protocol_t nand_proto_;
  const nand_info_t info_;

  std::map<uint32_t, std::unique_ptr<uint8_t[]>> pages_;
  std::vector<uint32_t> bad_blocks_;
};

zx_status_t FakeNand::NandOpRead(const nand_read_write_t& rw) {
  if (rw.length != 1) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (rw.offset_nand > info_.num_blocks * info_.pages_per_block) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (IsPageInBadBlock(rw.offset_nand)) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  fzl::VmoMapper data_mapper;
  zx_status_t status = data_mapper.Map(
      *zx::unowned_vmo(rw.data_vmo), rw.offset_data_vmo * info_.page_size,
      fbl::round_up(info_.page_size, ZX_PAGE_SIZE), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (status != ZX_OK) {
    return status;
  }

  fzl::VmoMapper oob_mapper;
  status = oob_mapper.Map(*zx::unowned_vmo(rw.oob_vmo), rw.offset_oob_vmo * info_.page_size,
                          fbl::round_up(info_.oob_size, ZX_PAGE_SIZE),
                          ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (status != ZX_OK) {
    return status;
  }

  if (pages_.find(rw.offset_nand) == pages_.end()) {
    // The page has been erased or was never written, fill both VMOs with 0xff bytes.
    memset(data_mapper.start(), 0xff, info_.page_size);
    memset(oob_mapper.start(), 0xff, info_.oob_size);
  } else {
    memcpy(data_mapper.start(), pages_[rw.offset_nand].get(), info_.page_size);
    memcpy(oob_mapper.start(), pages_[rw.offset_nand].get() + info_.page_size, info_.oob_size);
  }

  return ZX_OK;
}

zx_status_t FakeNand::NandOpWrite(const nand_read_write_t& rw) {
  if (rw.length != 1) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (rw.offset_nand > info_.num_blocks * info_.pages_per_block) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (pages_.find(rw.offset_nand) != pages_.end()) {
    return ZX_ERR_BAD_STATE;
  }
  if (IsPageInBadBlock(rw.offset_nand)) {
    return ZX_ERR_IO;
  }

  const size_t data_offset = rw.offset_data_vmo * info_.page_size;
  const size_t oob_offset = rw.offset_oob_vmo * info_.page_size;

  pages_[rw.offset_nand].reset(new uint8_t[info_.page_size + info_.oob_size]);
  uint8_t* const buffer = pages_[rw.offset_nand].get();

  zx_status_t status = zx_vmo_read(rw.data_vmo, buffer, data_offset, info_.page_size);
  if (status != ZX_OK) {
    return status;
  }

  return zx_vmo_read(rw.oob_vmo, buffer + info_.page_size, oob_offset, info_.oob_size);
}

zx_status_t FakeNand::NandOpErase(const nand_erase_t& erase) {
  if (erase.num_blocks != 1) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (erase.first_block > info_.num_blocks) {
    return ZX_ERR_INVALID_ARGS;
  }

  for (const uint32_t block : bad_blocks_) {
    if (block == erase.first_block) {
      return ZX_ERR_IO;
    }
  }

  EraseBlock(erase.first_block);
  return ZX_OK;
}

class SynapticsBadBlockTest : public zxtest::Test {
 private:
  static constexpr nand_info_t kNandInfo = {
      .page_size = 32,
      .pages_per_block = 4,
      .num_blocks = 16,
      .ecc_bits = 0,
      .oob_size = 16,
      .nand_class = 0,
      .partition_guid = {},
  };

 public:
  SynapticsBadBlockTest() : nand_(kNandInfo) {}

  void SetUp() override {
    nand_.Reset();
    ASSERT_OK(SynapticsBadBlock::Create({kBadBlockConfig, *nand_.GetProto()}, &dut_));
  }

 protected:
  uint32_t FirstPage(uint32_t block) { return block * kNandInfo.pages_per_block; }

  FakeNand nand_;
  fbl::RefPtr<BadBlock> dut_;

 private:
  static constexpr bad_block_config_t kBadBlockConfig = {
      .type = kSynaptics,
      .synaptics =
          {
              .table_start_block = 8,
              .table_end_block = 15,
          },
  };
};

TEST_F(SynapticsBadBlockTest, GetBadBlockList) {
  nand_.WritePageData(FirstPage(8), {0xf6, 0x24, 0xff, 0xaf});
  nand_.WritePageOob(FirstPage(8), {'1', 't', 'b', 'B', 30}, 8);

  nand_.WritePageData(FirstPage(13), {0xef, 0xf0, 0x7f, 0xff});
  nand_.WritePageOob(FirstPage(13), {'B', 'b', 't', '0', 31}, 8);

  fbl::Array<uint32_t> bad_blocks;
  EXPECT_OK(dut_->GetBadBlockList(0, 15, &bad_blocks));
  ASSERT_EQ(bad_blocks.size(), 4);

  constexpr uint32_t expected_bad_blocks[] = {2, 4, 5, 11};
  EXPECT_BYTES_EQ(bad_blocks.data(), expected_bad_blocks, sizeof(expected_bad_blocks));
}

TEST_F(SynapticsBadBlockTest, MarkBlockBad) {
  nand_.WritePageData(FirstPage(10), {0xef, 0xf0, 0x7f, 0xff});
  nand_.WritePageOob(FirstPage(10), {'1', 't', 'b', 'B', 31}, 8);

  EXPECT_OK(dut_->MarkBlockBad(0));   // Write table version 32 to blocks 8 and 12
  EXPECT_OK(dut_->MarkBlockBad(2));   // Already marked
  EXPECT_OK(dut_->MarkBlockBad(4));   // Already marked
  EXPECT_OK(dut_->MarkBlockBad(11));  // Already marked
  EXPECT_OK(dut_->MarkBlockBad(14));  // Write table version 33 to blocks 9 and 13
  EXPECT_OK(dut_->MarkBlockBad(15));  // Write table version 34 to blocks 10 and 8

  constexpr uint8_t expected_bbt[] = {0xed, 0xf0, 0x7f, 0x5f};
  EXPECT_BYTES_EQ(nand_.ReadPageData(FirstPage(10), 4).data(), expected_bbt, 4);
  EXPECT_BYTES_EQ(nand_.ReadPageData(FirstPage(8), 4).data(), expected_bbt, 4);

  constexpr uint8_t expected_bbt_oob[] = {'B', 'b', 't', '0', 34};
  constexpr uint8_t expected_mirror_oob[] = {'1', 't', 'b', 'B', 34};

  EXPECT_BYTES_EQ(nand_.ReadPageOob(FirstPage(10), 5, 8).data(), expected_bbt_oob, 5);
  EXPECT_BYTES_EQ(nand_.ReadPageOob(FirstPage(8), 5, 8).data(), expected_mirror_oob, 5);
}

TEST_F(SynapticsBadBlockTest, NoBadBlockTable) {
  fbl::Array<uint32_t> bad_blocks;
  EXPECT_NOT_OK(dut_->GetBadBlockList(0, 15, &bad_blocks));
  EXPECT_NOT_OK(dut_->MarkBlockBad(0));
}

TEST_F(SynapticsBadBlockTest, RotateTableBlocks) {
  nand_.WritePageData(FirstPage(14), {0xff, 0xff, 0xff, 0xff});
  nand_.WritePageOob(FirstPage(14), {'B', 'b', 't', '0', 0}, 8);

  nand_.WritePageData(FirstPage(15), {0xff, 0xff, 0xff, 0xff});
  nand_.WritePageOob(FirstPage(15), {'t', 't', 'b', 'B', 0}, 8);

  EXPECT_OK(dut_->MarkBlockBad(0));

  constexpr uint8_t expected_bbt[] = {0xfd, 0xff, 0xff, 0xff};
  EXPECT_BYTES_EQ(nand_.ReadPageData(FirstPage(15), 4).data(), expected_bbt, 4);
  EXPECT_BYTES_EQ(nand_.ReadPageData(FirstPage(8), 4).data(), expected_bbt, 4);

  constexpr uint8_t expected_bbt_oob[] = {'B', 'b', 't', '0', 1};
  constexpr uint8_t expected_mirror_oob[] = {'1', 't', 'b', 'B', 1};

  EXPECT_BYTES_EQ(nand_.ReadPageOob(FirstPage(15), 5, 8).data(), expected_bbt_oob, 5);
  EXPECT_BYTES_EQ(nand_.ReadPageOob(FirstPage(8), 5, 8).data(), expected_mirror_oob, 5);
}

TEST_F(SynapticsBadBlockTest, SkipTableBadBlocks) {
  nand_.WritePageData(FirstPage(14), {0xff, 0xff, 0xf5, 0x7f});
  nand_.WritePageOob(FirstPage(14), {'B', 'b', 't', '0', 0}, 8);

  EXPECT_OK(dut_->MarkBlockBad(0));

  constexpr uint8_t expected_bbt_oob[] = {'B', 'b', 't', '0', 1};
  constexpr uint8_t expected_mirror_oob[] = {'1', 't', 'b', 'B', 1};

  EXPECT_BYTES_EQ(nand_.ReadPageOob(FirstPage(10), 5, 8).data(), expected_bbt_oob, 5);
  EXPECT_BYTES_EQ(nand_.ReadPageOob(FirstPage(11), 5, 8).data(), expected_mirror_oob, 5);
}

TEST_F(SynapticsBadBlockTest, UpdateTableBadBlocks) {
  nand_.WritePageData(FirstPage(14), {0xff, 0xff, 0xf5, 0x7f});
  nand_.WritePageOob(FirstPage(14), {'B', 'b', 't', '0', 0}, 8);

  fbl::Array<uint32_t> bad_blocks;
  dut_->GetBadBlockList(0, 15, &bad_blocks);

  nand_.SetBlockBad(10);
  nand_.SetBlockBad(12);
  nand_.SetBlockBad(14);

  EXPECT_OK(dut_->MarkBlockBad(0));

  constexpr uint8_t expected_bbt[] = {0xfd, 0xff, 0xd5, 0x5d};

  EXPECT_BYTES_EQ(nand_.ReadPageData(FirstPage(11), 4).data(), expected_bbt, 4);
  EXPECT_BYTES_EQ(nand_.ReadPageData(FirstPage(13), 4).data(), expected_bbt, 4);

  constexpr uint8_t expected_bbt_oob[] = {'B', 'b', 't', '0', 3};
  constexpr uint8_t expected_mirror_oob[] = {'1', 't', 'b', 'B', 3};

  EXPECT_BYTES_EQ(nand_.ReadPageOob(FirstPage(11), 5, 8).data(), expected_bbt_oob, 5);
  EXPECT_BYTES_EQ(nand_.ReadPageOob(FirstPage(13), 5, 8).data(), expected_mirror_oob, 5);
}

TEST_F(SynapticsBadBlockTest, OneGoodTableBlocks) {
  nand_.WritePageData(FirstPage(12), {0xff, 0xff, 0x00, 0x03});
  nand_.WritePageOob(FirstPage(12), {'1', 't', 'b', 'B', 0}, 8);

  fbl::Array<uint32_t> bad_blocks;
  dut_->GetBadBlockList(0, 15, &bad_blocks);

  EXPECT_OK(dut_->MarkBlockBad(0));

  constexpr uint8_t expected_bbt[] = {0xfd, 0xff, 0x00, 0x03};
  EXPECT_BYTES_EQ(nand_.ReadPageData(FirstPage(12), 4).data(), expected_bbt, 4);

  constexpr uint8_t expected_bbt_oob[] = {'B', 'b', 't', '0', 1};
  EXPECT_BYTES_EQ(nand_.ReadPageOob(FirstPage(12), 5, 8).data(), expected_bbt_oob, 5);
}

TEST_F(SynapticsBadBlockTest, NoGoodTableBlocks) {
  nand_.WritePageData(FirstPage(12), {0xff, 0xff, 0x00, 0x03});
  nand_.WritePageOob(FirstPage(12), {'B', 'b', 't', '0', 0}, 8);

  fbl::Array<uint32_t> bad_blocks;
  dut_->GetBadBlockList(0, 15, &bad_blocks);

  nand_.SetBlockBad(12);

  EXPECT_NOT_OK(dut_->MarkBlockBad(0));
}

}  // namespace nand
