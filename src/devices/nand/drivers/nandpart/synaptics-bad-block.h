// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_NAND_DRIVERS_NANDPART_SYNAPTICS_BAD_BLOCK_H_
#define SRC_STORAGE_NAND_DRIVERS_NANDPART_SYNAPTICS_BAD_BLOCK_H_

#include "bad-block.h"

namespace nand {

class SynapticsBadBlock : public BadBlock {
 public:
  static zx_status_t Create(Config config, fbl::RefPtr<BadBlock>* out);

  zx_status_t GetBadBlockList(uint32_t first_block, uint32_t last_block,
                              fbl::Array<uint32_t>* bad_blocks) override;
  zx_status_t MarkBlockBad(uint32_t block) override;

 protected:
  friend class fbl::internal::MakeRefCountedHelper<SynapticsBadBlock>;

  static constexpr uint8_t kTablePattern[] = {'B', 'b', 't', '0'};
  static constexpr uint8_t kMirrorPattern[] = {'1', 't', 'b', 'B'};
  static_assert(sizeof(kTablePattern) == sizeof(kMirrorPattern));

  static constexpr size_t kPatternSize = sizeof(kTablePattern);
  static constexpr size_t kTablePatternOffset = 8;
  static constexpr size_t kTableVersionOffset = kTablePatternOffset + sizeof(kTablePattern);

  static constexpr size_t kOobSize = kTableVersionOffset + 1;

  SynapticsBadBlock(ddk::NandProtocolClient nand, const bad_block_config_t& config,
                    const nand_info_t& nand_info, zx::vmo data_vmo, zx::vmo oob_vmo,
                    fbl::Array<uint8_t> nand_op)
      : BadBlock(std::move(data_vmo), std::move(oob_vmo), std::move(nand_op)),
        nand_(nand),
        config_(config),
        nand_info_(nand_info),
        bbt_block_(config.synaptics.table_end_block + 1),
        bbt_mirror_block_(config.synaptics.table_end_block + 1) {}

  // Returns a block number that is outside the range of the bad block table.
  uint32_t InvalidBlock() const { return config_.synaptics.table_end_block + 1; }

  // Checks whether or not the block is outside the range of the bad block table.
  bool IsBlockValid(uint32_t block) const {
    return block >= config_.synaptics.table_start_block &&
           block <= config_.synaptics.table_end_block;
  }

  // Finds the next good bad block table block starting with start_block + 1 and excluding
  // except_block. If start_block is invalid the search starts at the beginning of the table region.
  // Once the end of the region is reached the search wraps around to the beginning. The next good
  // block is returned, or an invalid block is returned if there are no more good blocks.
  uint32_t FindNextGoodTableBlock(uint32_t start_block, uint32_t except_block) TA_REQ(lock_);

  zx_status_t ReadBadBlockTablePattern(uint32_t block, uint8_t out_pattern[kPatternSize],
                                       uint8_t* out_version) TA_REQ(lock_);
  uint32_t FindBadBlockTable() TA_REQ(lock_);
  zx_status_t ReadBadBlockTable() TA_REQ(lock_);

  // Writes the RAM bad block table and the version number to the data and OOB VMOs.
  zx_status_t WriteBadBlockTableToVmo() TA_REQ(lock_);

  // Attempts to write the bad block table to the specified block. If block is invalid or bad a good
  // block will be chosen. Sets out_block to the block that was written.
  zx_status_t WriteBadBlockTable(uint32_t block, uint32_t except_block,
                                 const uint8_t pattern[kPatternSize], uint32_t* out_block)
      TA_REQ(lock_);

  zx_status_t ReadFirstPage(uint32_t block) TA_REQ(lock_);
  zx_status_t WriteFirstPage(uint32_t block) TA_REQ(lock_);

  const ddk::NandProtocolClient nand_;
  const bad_block_config_t config_;
  const nand_info_t nand_info_;

  fbl::Array<uint8_t> bbt_contents_ TA_GUARDED(lock_);
  uint32_t bbt_block_ TA_GUARDED(lock_);
  uint32_t bbt_mirror_block_ TA_GUARDED(lock_);
  uint8_t bbt_version_ TA_GUARDED(lock_) = 0;
};

}  // namespace nand

#endif  // SRC_STORAGE_NAND_DRIVERS_NANDPART_SYNAPTICS_BAD_BLOCK_H_
