// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/vector.h>

#include "ftl.h"

#include <stdio.h>

namespace internal {

// NOTE: This file is intended only for enabling unit testing, and does not
// contain all the information needed to understand FTL structures, not even
// all possible views into something as basic as the spare area. A more complete
// vision can be found directly in the implementation files.

// Basic structure of the spare area for an FTL page.
struct SpareArea {
  uint8_t unused;
  uint8_t page_num[4];
  uint8_t block_count[4];
  uint8_t wear_count[3];
  uint8_t msh_wc_lsh_validity;
  uint8_t validity[2];
  uint8_t ndm;  // 0 for NDM.
};
constexpr char kNdmSignature[] = "NDMTA01";  // Not null terminated.

// Functions to extract interesting parts from the SpareArea:
int DecodePageNum(const SpareArea& oob);
int DecodeBlockCount(const SpareArea& oob);
int DecodeWear(const SpareArea& oob);

// Functions to test specific properties of the SpareArea, independently of each
// other, so for example DataBlock doesn't imply FtlBlock:
bool IsNdmBlock(const SpareArea& oob);
bool IsFtlBlock(const SpareArea& oob);
bool IsDataBlock(const SpareArea& oob);
bool IsCopyBlock(const SpareArea& oob);
bool IsMapBlock(const SpareArea& oob);

// Header of an NDM control block version 1.
struct NdmHeaderV1 {
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

// Header of an NDM control block.
struct NdmHeader {
  uint16_t major_version;
  uint16_t minor_version;
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
  int32_t transfer_bad_block;
  int32_t transfer_bad_page;
};
static_assert(sizeof(NdmHeader) == sizeof(NdmHeaderV1) + sizeof(int32_t) * 3);
static_assert(offsetof(NdmHeader, current_location) == sizeof(uint32_t));
static_assert(offsetof(NdmHeaderV1, current_location) == 0);

// Populates a header structure from nand data. Defined here only for tests.
NdmHeader GetNdmHeader(const void* page);

// Encapsulates the NDM related functionality.
class NdmData {
 public:
  NdmData() {}

  // Initializes this object by looking for the latest control block on nand.
  bool FindHeader(const NandBroker& nand);

  // Returns the number of nand pages needed to get an NDM page.
  int page_multiplier() const { return page_multiplier_; }

  // Returns true if a given block is marked as bad by NMD.
  bool IsBadBlock(uint32_t block) const;

  // Returns the last block number that contains FTL data.
  uint32_t LastFtlBlock() const {
    if (header_.free_virt_block > 0) {
      return header_.free_virt_block - 1;
    }
    return last_ftl_block_;
  }

  // Prints out NDM control data.
  void DumpInfo() const;

  // Parses a given page for NDM control information. It assumes the page
  // contains NDM data.
  void ParseNdmData(const void* page, fbl::Vector<int32_t>* bad_blocks,
                    fbl::Vector<int32_t>* replacements) const;

 private:
  template <typename... T>
  void Log(T... args) const {
    if (logging_) {
      printf(args...);
    }
  }

  void DumpHeader(const NdmHeader& h) const;
  void DumpNdmData(const void* page, fbl::Vector<int32_t>* bad_blocks,
                   fbl::Vector<int32_t>* replacements) const;
  void DumpPartitions(const NdmHeader& header, const char* data, int num_partitions) const;

  NdmHeader header_ = {};
  int32_t header_block_ = 0;
  int32_t header_page_ = 0;
  int32_t last_ftl_block_ = 0;
  int page_multiplier_ = 0;
  mutable bool logging_ = false;
  fbl::Vector<int32_t> bad_blocks_;
  fbl::Vector<int32_t> replacements_;
};

}  // namespace internal
