// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-bad-block.h"

#include <lib/zx/vmar.h>
#include <zircon/types.h>

#include <memory>
#include <utility>

#include <ddk/protocol/nand.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

#include "bad-block.h"

namespace nand {
namespace {

// It is convenient for mapping for nand page size to be same as DRAM page size.
constexpr uint32_t kPageSize = ZX_PAGE_SIZE;
constexpr uint32_t kPagesPerBlock = 16;
constexpr uint32_t kNumBlocks = 100;
constexpr uint32_t kOobSize = 8;
constexpr fuchsia_hardware_nand_Info kNandInfo = {
    .page_size = kPageSize,
    .pages_per_block = kPagesPerBlock,
    .num_blocks = kNumBlocks,
    .ecc_bits = 2,
    .oob_size = kOobSize,
    .nand_class = fuchsia_hardware_nand_Class_BBS,
    .partition_guid = {},
};

using NandPage = uint32_t;

// Stores information about a specific bad block table entry. Generation is
// incremented based on object creation order.
//
// Not threadsafe.
class TableNode : public fbl::SinglyLinkedListable<std::unique_ptr<TableNode>> {
 public:
  uint32_t id_;
  bool valid_;
  uint16_t generation_;
  fbl::Vector<uint32_t> bad_blocks_;

  TableNode(NandPage id, bool valid = true, uint16_t generation = count_++)
      : id_(id), valid_(valid), generation_(generation) {}

  TableNode(NandPage id, fbl::Vector<uint32_t> bad_blocks, bool valid = true,
            uint16_t generation = count_++)
      : id_(id), valid_(valid), generation_(generation), bad_blocks_(std::move(bad_blocks)) {}

  TableNode(const TableNode&) = delete;
  TableNode& operator=(const TableNode&) = delete;

  static size_t GetHash(uint32_t key) { return key; }
  uint32_t GetKey() const { return id_; }

  static void ResetCount() { count_ = 0; }

 private:
  static uint16_t count_;
};

uint16_t TableNode::count_ = 0;

using TableEntries = fbl::HashTable<NandPage, std::unique_ptr<TableNode>>;

struct Context {
  TableEntries& table_entries;
};

void MockQuery(void* ctx, fuchsia_hardware_nand_Info* info_out, size_t* nand_op_size_out) {
  memcpy(info_out, &kNandInfo, sizeof(kNandInfo));
  *nand_op_size_out = sizeof(nand_operation_t);
}

void MockQueue(void* ctx, nand_operation_t* op, nand_queue_callback completion_cb, void* cookie) {
  auto* context = static_cast<Context*>(ctx);

  switch (op->command) {
    case NAND_OP_READ:
    case NAND_OP_WRITE:
      break;

    case NAND_OP_ERASE: {
      if (op->erase.first_block >= kNumBlocks ||
          op->erase.first_block + op->erase.num_blocks >= kNumBlocks) {
        fprintf(stderr, "Trying to write to a page that is out of range!\n");
        completion_cb(cookie, ZX_ERR_OUT_OF_RANGE, op);
        return;
      }
      const NandPage start_page = op->erase.first_block * kPagesPerBlock;
      const NandPage end_page = start_page + (op->erase.num_blocks * kPagesPerBlock);
      for (NandPage page = start_page; page < end_page; page++) {
        context->table_entries.erase(page);
      }
      completion_cb(cookie, ZX_OK, op);
      return;
    }
    default:
      completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, op);
      return;
  }

  // Read or Write operation.
  zx::vmo data_vmo(op->rw.data_vmo);
  uintptr_t data_buf;
  zx_status_t status =
      zx::vmar::root_self()->map(0, data_vmo, op->rw.offset_data_vmo, op->rw.length * kPageSize,
                                 ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &data_buf);
  __UNUSED auto unused = data_vmo.release();
  if (status != ZX_OK) {
    completion_cb(cookie, status, op);
    return;
  }
  auto data_unmapper = fbl::MakeAutoCall(
      [&]() { zx::vmar::root_self()->unmap(data_buf, op->rw.length * kPageSize); });
  uintptr_t oob_buf;
  zx::vmo oob_vmo(op->rw.oob_vmo);
  status = zx::vmar::root_self()->map(0, oob_vmo, op->rw.offset_oob_vmo, op->rw.length * kOobSize,
                                      ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &oob_buf);
  __UNUSED auto __ = oob_vmo.release();
  if (status != ZX_OK) {
    completion_cb(cookie, status, op);
    return;
  }
  auto oob_unmapper =
      fbl::MakeAutoCall([&]() { zx::vmar::root_self()->unmap(oob_buf, op->rw.length * kOobSize); });
  auto* data = reinterpret_cast<uint8_t*>(data_buf);
  auto* oob = reinterpret_cast<AmlBadBlock::OobMetadata*>(oob_buf);

  switch (op->command) {
    case NAND_OP_READ:
      for (uint16_t i = 0; i < op->rw.length; i++) {
        auto it = context->table_entries.find(op->rw.offset_nand + i);
        if (it != context->table_entries.end()) {
          if (!it->valid_) {
            completion_cb(cookie, ZX_ERR_IO, op);
            return;
          }
          memset(data + (i * kPageSize), 0, kPageSize);
          oob->magic = 0x7462626E;
          oob->generation = it->generation_;
          oob->program_erase_cycles = 0;
          for (const auto& block : it->bad_blocks_) {
            data[(i * kPageSize) + block] = 1;
          }
        } else {
          memset(data + (i * kPageSize), 0xFF, kPageSize);
          memset(oob + i, 0xFF, sizeof(*oob));
        }
      }
      completion_cb(cookie, ZX_OK, op);
      break;

    case NAND_OP_WRITE:
      for (uint16_t i = 0; i < op->rw.length; i++) {
        fbl::Vector<uint32_t> bad_blocks;
        for (uint32_t block = 0; block < kPageSize; block++) {
          if (data[block] != 0) {
            bad_blocks.push_back(block);
          }
        }
        auto node = std::make_unique<TableNode>(op->rw.offset_nand + i, std::move(bad_blocks), true,
                                                oob->generation);
        if (!context->table_entries.insert_or_find(std::move(node))) {
          fprintf(stderr, "Trying to write to a page that isn't erased!\n");
          status = ZX_ERR_INTERNAL;
          break;
        }
      }
      completion_cb(cookie, status, op);
      break;
    default:
      completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, op);
      break;
  }
}

nand_protocol_ops_t kNandProtocolOps = {
    .query = MockQuery,
    .queue = MockQueue,
    .get_factory_bad_block_list = nullptr,
};

BadBlock::Config MakeBadBlockConfig(Context* ctx) {
  return BadBlock::Config{
      .bad_block_config =
          {
              .type = kAmlogicUboot,
              .aml_uboot =
                  {
                      .table_start_block = 0,
                      .table_end_block = 3,
                  },
          },
      .nand_proto =
          {
              .ops = &kNandProtocolOps,
              .ctx = ctx,
          },
  };
}

TEST(AmlBadBlockTest, GetBadBlockListTest) {
  TableNode::ResetCount();
  TableEntries table_entries;
  table_entries.insert_or_replace(std::make_unique<TableNode>(0));
  table_entries.insert_or_replace(std::make_unique<TableNode>(1));
  Context context = {
      .table_entries = table_entries,
  };

  fbl::RefPtr<BadBlock> bad_block;
  zx_status_t status = BadBlock::Create(MakeBadBlockConfig(&context), &bad_block);
  ASSERT_OK(status);

  fbl::Array<uint32_t> bad_blocks;
  status = bad_block->GetBadBlockList(4, 10, &bad_blocks);
  ASSERT_OK(status);
  EXPECT_EQ(bad_blocks.size(), 0);
}

TEST(AmlBadBlockTest, GetBadBlockListWithEntriesTest) {
  TableNode::ResetCount();
  TableEntries table_entries;
  table_entries.insert_or_replace(std::make_unique<TableNode>(0));
  fbl::Vector<uint32_t> bad_blocks_v = {4, 8};
  table_entries.insert_or_replace(std::make_unique<TableNode>(1, std::move(bad_blocks_v)));
  Context context = {
      .table_entries = table_entries,
  };

  fbl::RefPtr<BadBlock> bad_block;
  zx_status_t status = BadBlock::Create(MakeBadBlockConfig(&context), &bad_block);
  ASSERT_OK(status);

  auto check_expected = [&bad_block](uint32_t start_block, uint32_t end_block,
                                     fbl::Vector<uint32_t> expected) {
    fbl::Array<uint32_t> bad_blocks;
    zx_status_t status = bad_block->GetBadBlockList(start_block, end_block, &bad_blocks);
    ASSERT_OK(status);
    ASSERT_EQ(bad_blocks.size(), expected.size());
    EXPECT_BYTES_EQ(reinterpret_cast<uint8_t*>(bad_blocks.data()),
                    reinterpret_cast<uint8_t*>(expected.data()), expected.size() * sizeof(uint32_t),
                    "");
  };
  ASSERT_NO_FATAL_FAILURES(check_expected(4, 10, {4, 8}));
  ASSERT_NO_FATAL_FAILURES(check_expected(5, 10, {8}));
  ASSERT_NO_FATAL_FAILURES(check_expected(4, 7, {4}));
  ASSERT_NO_FATAL_FAILURES(check_expected(9, 11, {}));
}

TEST(AmlBadBlockTest, FindBadBlockSecondBlockTest) {
  TableNode::ResetCount();
  TableEntries table_entries;
  fbl::Vector<uint32_t> bad_blocks_1 = {4, 6};
  table_entries.insert_or_replace(std::make_unique<TableNode>(0, std::move(bad_blocks_1)));
  fbl::Vector<uint32_t> bad_blocks_2 = {4, 6, 8};
  table_entries.insert_or_replace(
      std::make_unique<TableNode>(kPagesPerBlock * 3, std::move(bad_blocks_2)));
  fbl::Vector<uint32_t> bad_blocks_3 = {4, 6, 8, 9};
  table_entries.insert_or_replace(
      std::make_unique<TableNode>(kPagesPerBlock, std::move(bad_blocks_3)));
  Context context = {
      .table_entries = table_entries,
  };

  fbl::RefPtr<BadBlock> bad_block;
  zx_status_t status = BadBlock::Create(MakeBadBlockConfig(&context), &bad_block);
  ASSERT_OK(status);

  fbl::Array<uint32_t> bad_blocks;
  status = bad_block->GetBadBlockList(4, 10, &bad_blocks);
  ASSERT_OK(status);
  ASSERT_EQ(bad_blocks.size(), 4);
}

TEST(AmlBadBlockTest, FindBadBlockLastBlockTest) {
  TableNode::ResetCount();
  TableEntries table_entries;
  fbl::Vector<uint32_t> bad_blocks_1 = {4, 6};
  table_entries.insert_or_replace(
      std::make_unique<TableNode>(kPagesPerBlock * 2, std::move(bad_blocks_1)));
  fbl::Vector<uint32_t> bad_blocks_2 = {4, 6, 8};
  table_entries.insert_or_replace(
      std::make_unique<TableNode>(kPagesPerBlock, std::move(bad_blocks_2)));
  fbl::Vector<uint32_t> bad_blocks_3 = {4, 6, 8, 9};
  table_entries.insert_or_replace(
      std::make_unique<TableNode>(kPagesPerBlock * 3, std::move(bad_blocks_3)));
  Context context = {
      .table_entries = table_entries,
  };

  fbl::RefPtr<BadBlock> bad_block;
  zx_status_t status = BadBlock::Create(MakeBadBlockConfig(&context), &bad_block);
  ASSERT_OK(status);

  fbl::Array<uint32_t> bad_blocks;
  status = bad_block->GetBadBlockList(4, 10, &bad_blocks);
  ASSERT_OK(status);
  ASSERT_EQ(bad_blocks.size(), 4);
}

TEST(AmlBadBlockTest, MarkBlockBadTest) {
  TableNode::ResetCount();
  TableEntries table_entries;
  table_entries.insert_or_replace(std::make_unique<TableNode>(0));
  table_entries.insert_or_replace(std::make_unique<TableNode>(1));
  Context context = {
      .table_entries = table_entries,
  };

  fbl::RefPtr<BadBlock> bad_block;
  zx_status_t status = BadBlock::Create(MakeBadBlockConfig(&context), &bad_block);
  ASSERT_OK(status);

  status = bad_block->MarkBlockBad(8);

  fbl::Array<uint32_t> bad_blocks;
  status = bad_block->GetBadBlockList(4, 10, &bad_blocks);
  ASSERT_OK(status);
  EXPECT_EQ(bad_blocks.size(), 1);

  // Validate that a new table entry was inserted.
  const auto it = table_entries.find_if(
      [](const TableNode& node) { return node.generation_ == 2 && node.bad_blocks_.size() == 1; });
  ASSERT_TRUE(it != table_entries.end());
}

TEST(AmlBadBlockTest, FindBadBlockLastPageInvalidTest) {
  TableNode::ResetCount();
  TableEntries table_entries;
  fbl::Vector<uint32_t> bad_blocks_1 = {4, 6};
  table_entries.insert_or_replace(
      std::make_unique<TableNode>(kPagesPerBlock * 2, std::move(bad_blocks_1)));
  fbl::Vector<uint32_t> bad_blocks_2 = {4, 6, 8};
  table_entries.insert_or_replace(
      std::make_unique<TableNode>(kPagesPerBlock * 3, std::move(bad_blocks_2)));
  fbl::Vector<uint32_t> bad_blocks_3 = {4, 6, 8, 9};
  table_entries.insert_or_replace(
      std::make_unique<TableNode>(kPagesPerBlock * 3 + 1, std::move(bad_blocks_3), false));
  Context context = {
      .table_entries = table_entries,
  };

  fbl::RefPtr<BadBlock> bad_block;
  zx_status_t status = BadBlock::Create(MakeBadBlockConfig(&context), &bad_block);
  ASSERT_OK(status);

  fbl::Array<uint32_t> bad_blocks;
  status = bad_block->GetBadBlockList(4, 10, &bad_blocks);
  ASSERT_OK(status);
  ASSERT_EQ(bad_blocks.size(), 3);

  // Validate that a new table entry was inserted.
  const auto it = table_entries.find_if(
      [](const TableNode& node) { return node.generation_ == 2 && node.valid_ == true; });
  ASSERT_TRUE(it != table_entries.end());
}

TEST(AmlBadBlockTest, FindBadBlockNoValidTest) {
  TableNode::ResetCount();
  TableEntries table_entries;
  for (uint32_t block = 0; block < 4; block++) {
    for (uint32_t page = 0; page < 6; page++) {
      table_entries.insert_or_replace(
          std::make_unique<TableNode>(kPagesPerBlock * block + page, false));
    }
    table_entries.insert_or_replace(std::make_unique<TableNode>(kPagesPerBlock * block + 6));
  }
  Context context = {
      .table_entries = table_entries,
  };

  fbl::RefPtr<BadBlock> bad_block;
  zx_status_t status = BadBlock::Create(MakeBadBlockConfig(&context), &bad_block);
  ASSERT_OK(status);

  status = bad_block->MarkBlockBad(4);
  ASSERT_NE(status, ZX_OK);
}

TEST(AmlBadBlockTest, FindBadBlockBigHoleTest) {
  TableNode::ResetCount();
  TableEntries table_entries;
  table_entries.insert_or_replace(std::make_unique<TableNode>(kPagesPerBlock * 3));
  for (uint32_t i = 1; i < 9; i++) {
    table_entries.insert_or_replace(std::make_unique<TableNode>(kPagesPerBlock * 3 + i, false));
  }
  table_entries.insert_or_replace(
      std::make_unique<TableNode>(kPagesPerBlock * 3 + 9, fbl::Vector<uint32_t>({4})));

  Context context = {
      .table_entries = table_entries,
  };

  fbl::RefPtr<BadBlock> bad_block;
  zx_status_t status = BadBlock::Create(MakeBadBlockConfig(&context), &bad_block);
  ASSERT_OK(status);

  fbl::Array<uint32_t> bad_blocks;
  status = bad_block->GetBadBlockList(4, 10, &bad_blocks);
  ASSERT_OK(status);
  ASSERT_EQ(bad_blocks.size(), 1);
}

TEST(AmlBadBlockTest, MarkBlockBadFullBlockTest) {
  TableNode::ResetCount();
  TableEntries table_entries;
  for (uint32_t i = 0; i < kPagesPerBlock; i++) {
    table_entries.insert_or_replace(std::make_unique<TableNode>(i));
  }
  Context context = {
      .table_entries = table_entries,
  };

  fbl::RefPtr<BadBlock> bad_block;
  zx_status_t status = BadBlock::Create(MakeBadBlockConfig(&context), &bad_block);
  ASSERT_OK(status);

  status = bad_block->MarkBlockBad(8);

  fbl::Array<uint32_t> bad_blocks;
  status = bad_block->GetBadBlockList(4, 10, &bad_blocks);
  ASSERT_OK(status);
  EXPECT_EQ(bad_blocks.size(), 1);

  // Validate that a new table entry was inserted.
  const auto it = table_entries.find_if([](const TableNode& node) {
    return node.id_ >= kPagesPerBlock && node.generation_ == kPagesPerBlock &&
           node.bad_blocks_.size() == 1;
  });
  ASSERT_TRUE(it != table_entries.end());
}

TEST(AmlBadBlockTest, BootloaderQuirkTest) {
  TableNode::ResetCount();
  TableEntries table_entries;
  fbl::Vector<uint32_t> bad_blocks_1 = {8, 9, 10, 11, 12};
  table_entries.insert_or_replace(
      std::make_unique<TableNode>(kPagesPerBlock, std::move(bad_blocks_1)));
  Context context = {
      .table_entries = table_entries,
  };

  fbl::RefPtr<BadBlock> bad_block;
  zx_status_t status = BadBlock::Create(MakeBadBlockConfig(&context), &bad_block);
  ASSERT_OK(status);

  fbl::Array<uint32_t> bad_blocks;
  status = bad_block->GetBadBlockList(4, 10, &bad_blocks);
  ASSERT_OK(status);
  ASSERT_EQ(bad_blocks.size(), 3);
}

}  // namespace
}  // namespace nand
