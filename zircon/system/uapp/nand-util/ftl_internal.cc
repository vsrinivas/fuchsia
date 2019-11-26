// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ftl_internal.h"

#include <zircon/assert.h>

namespace internal {
namespace {

struct NdmSpareArea {
  uint8_t unused;
  char signature[7];
  uint8_t unused2[7];
  uint8_t ndm;  // 0 for NDM.
};

// Follows NdmHeaderV1 if transfer_to_block != FFs.
struct TransferInfo {
  int32_t transfer_bad_block;
  int32_t transfer_bad_page;
  uint8_t unused;
};

// A translated bad block.
struct RunningBadBlock {
  int32_t bad_block;
  int32_t replacement_block;
};

// Follows TransferInfo or NdmHeader.
struct BadBlockData {
  int32_t num_partitions;
  int32_t initial_bbt[1];  // Ends when value == NnmHeader.num_blocks.

  // Followed by a running bad table:
  // RunningBadBlock running_bbt[];  // Ends when values == FFs.
};

// Follows BadBlockData (one entry per partition).
struct NdmPartition {
  int32_t first_block;
  int32_t num_blocks;
  char name[15];
  uint8_t type;
};

// User data attached to a partition, for version >= 2.
struct NdmPartitionData {
  uint32_t data_size;  // Number of bytes on |data|.
  uint8_t data[];
};

uint32_t ReadBytes(const uint8_t* data, int bytes) {
  uint32_t value = 0;
  uint32_t scale = 0;
  for (; bytes > 0; bytes--, data++) {
    value += *data << scale;
    scale += 8;
  }
  return value;
}

uint32_t ReadBits(const uint8_t* data, int bits, int offset_bits = 0) {
  ZX_DEBUG_ASSERT((bits % 4) == 0);
  ZX_DEBUG_ASSERT((offset_bits % 4) == 0);
  ZX_DEBUG_ASSERT(offset_bits < 8);

  uint32_t value = 0;
  if (offset_bits) {
    value = *data & 0xF;
    bits -= 4;
    data++;
  }
  value += ReadBytes(data, bits / 8) >> offset_bits;
  data += bits / 8;

  if (bits % 8) {
    ZX_DEBUG_ASSERT(bits % 8 == 4);
    uint32_t ms_nibble = (*data >> 4) & 0xf;
    value += ms_nibble << (bits - 4);
  }

  return value;
}

}  // namespace

int DecodeWear(const SpareArea& oob) {
  uint32_t value = ReadBits(oob.wear_count, 28);
  return value == 0xfffffff ? -1 : value;
}

int DecodePageNum(const SpareArea& oob) { return ReadBytes(oob.page_num, 4); }

int DecodeBlockCount(const SpareArea& oob) { return ReadBytes(oob.block_count, 4); }

bool IsNdmBlock(const SpareArea& oob) {
  if (oob.ndm) {
    return false;
  }
  const NdmSpareArea& spare = *reinterpret_cast<const NdmSpareArea*>(&oob);
  return memcmp(spare.signature, kNdmSignature, sizeof(spare.signature)) == 0;
}

bool IsFtlBlock(const SpareArea& oob) { return oob.ndm == 0xFF; }

bool IsDataBlock(const SpareArea& oob) {
  int block_count = DecodeBlockCount(oob);
  return block_count == -1;
}

bool IsCopyBlock(const SpareArea& oob) {
  int block_count = DecodeBlockCount(oob);
  return block_count == -2;
}

bool IsMapBlock(const SpareArea& oob) {
  int block_count = DecodeBlockCount(oob);
  return block_count != -1 && block_count != -2;
}

NdmHeader GetNdmHeader(const void* page) {
  NdmHeader header = *reinterpret_cast<const NdmHeader*>(page);
  if (header.major_version < 2) {
    const NdmHeaderV1& v1 = *reinterpret_cast<const NdmHeaderV1*>(page);
    memcpy(&header.current_location, &v1, sizeof(v1));
    header.transfer_bad_block = -1;
    header.transfer_bad_page = -1;

    if (v1.transfer_to_block != -1) {
      const char* data = reinterpret_cast<const char*>(page);
      data += sizeof(v1);
      const TransferInfo& transfer = *reinterpret_cast<const TransferInfo*>(data);
      header.transfer_bad_block = transfer.transfer_bad_block;
      header.transfer_bad_page = transfer.transfer_bad_page;
    }
  }
  return header;
}

bool NdmData::FindHeader(const NandBroker& nand) {
  page_multiplier_ = nand.Info().oob_size < 16 ? 16 / nand.Info().oob_size : 1;

  int last = -1;
  for (int32_t block = nand.Info().num_blocks - 1; block > header_.free_virt_block; block--) {
    for (uint32_t page = 0; page < nand.Info().pages_per_block; page += page_multiplier_) {
      if (!nand.ReadPages(block * nand.Info().pages_per_block + page, page_multiplier_)) {
        printf("Read failed for block %d, page %d\n", block, page);
        break;
      }
      const SpareArea* oob = reinterpret_cast<const SpareArea*>(nand.oob());
      if (!IsNdmBlock(*oob)) {
        break;
      }

      fbl::Vector<int32_t> bad_blocks;
      fbl::Vector<int32_t> replacements;
      ParseNdmData(nand.data(), &bad_blocks, &replacements);

      const NdmHeader header = GetNdmHeader(nand.data());
      if (header.sequence_num >= last) {
        last = header.sequence_num;
        header_page_ = page;
        header_block_ = block;
        header_ = header;
        if (bad_blocks.size() > bad_blocks_.size()) {
          bad_blocks_.swap(bad_blocks);
          replacements_.swap(replacements);
        }
        if (header.free_virt_block > 0) {
          last_ftl_block_ = header.free_virt_block - 1;
        }
      }
    }
  }

  if (header_block_) {
    printf("Last NDM control block %d, at block %d, page %d\n", last, header_block_, header_page_);
    return true;
  }

  printf("NDM data not found\n");
  return false;
}

bool NdmData::IsBadBlock(uint32_t block) const {
  for (uint32_t bad : bad_blocks_) {
    if (block == bad) {
      return true;
    }
  }
  return false;
}

void NdmData::DumpInfo() const {
  logging_ = true;
  DumpHeader(header_);
  if (bad_blocks_.is_empty()) {
    return;
  }

  printf("%lu bad blocks:\n", bad_blocks_.size());
  for (auto block : bad_blocks_) {
    printf("%d ", block);
  }
  printf("\n");
}

void NdmData::ParseNdmData(const void* page, fbl::Vector<int32_t>* bad_blocks,
                           fbl::Vector<int32_t>* replacements) const {
  const NdmHeader h = GetNdmHeader(page);
  if (h.current_location == 0xFFFF) {
    return;
  }

  const char* data = reinterpret_cast<const char*>(page);
  data += (h.major_version < 2) ? sizeof(NdmHeaderV1) : sizeof(h);
  DumpHeader(h);

  if (h.transfer_to_block != -1 && h.major_version < 2) {
    const TransferInfo& transfer = *reinterpret_cast<const TransferInfo*>(data);
    data += sizeof(transfer.transfer_bad_block);
    data += sizeof(transfer.transfer_bad_page);
    data += sizeof(transfer.unused);

#if defined(__arm__) || defined(__aarch64__)
    return;
#endif
  }

  const BadBlockData& bad_data = *reinterpret_cast<const BadBlockData*>(data);
  data += sizeof(bad_data);

  for (int i = 0; bad_data.initial_bbt[i] != h.num_blocks; i++) {
    Log("Bad block at %d\n", bad_data.initial_bbt[i]);
    bad_blocks->push_back(bad_data.initial_bbt[i]);
    data += sizeof(bad_data.initial_bbt[i]);
    if (i == 100) {
      printf("Unreasonable number of bad blocks. Out of sync\n");
      return;
    }
  }

  const RunningBadBlock* running = reinterpret_cast<const RunningBadBlock*>(data);
  data += sizeof(*running);

  for (int i = 0; running->bad_block != -1; i++, running++) {
    Log("Bad block at %d, translated to %d\n", running->bad_block, running->replacement_block);
    bad_blocks->push_back(running->bad_block);
    replacements->push_back(running->replacement_block);
    data += sizeof(*running);
    if (i == 100) {
      printf("Unreasonable number of bad blocks. Out of sync\n");
      return;
    }
  }

  DumpPartitions(h, data, bad_data.num_partitions);
  Log("Total bad blocks %lu\n\n", bad_blocks->size());
}

void NdmData::DumpHeader(const NdmHeader& h) const {
  Log("NDM control block %d:\n", h.sequence_num);
  Log("version %u.%u\n", h.major_version, h.minor_version);
  Log("current_location %d, last_location %d\n", h.current_location, h.last_location);
  Log("num_blocks %d, block_size %d\n", h.num_blocks, h.block_size);
  Log("control_block_0 %d, control_block_1 %d\n", h.control_block0, h.control_block1);
  Log("free_virt_block %d, free_control_block %d, transfer_to_block %d\n", h.free_virt_block,
      h.free_control_block, h.transfer_to_block);
  Log("transfer_bad_block %d, transfer_bad_page %d\n", h.transfer_bad_block, h.transfer_bad_page);
}

void NdmData::DumpNdmData(const void* page, fbl::Vector<int32_t>* bad_blocks,
                          fbl::Vector<int32_t>* replacements) const {
  bool old = logging_;
  logging_ = true;
  ParseNdmData(page, bad_blocks, replacements);
  logging_ = old;
}

void NdmData::DumpPartitions(const NdmHeader& header, const char* data, int num_partitions) const {
  for (int32_t i = 0; i < num_partitions; i++) {
    auto partition = reinterpret_cast<const NdmPartition*>(data);
    data += sizeof(*partition);
    char name[sizeof(partition->name) + 1];
    memcpy(name, partition->name, sizeof(partition->name));
    name[sizeof(partition->name)] = '\0';
    Log("Partition %d:\n", i);
    Log("first_block %d, num_blocks %d, name %s, type %d\n", partition->first_block,
        partition->num_blocks, name, partition->type);

    if (header.major_version >= 2) {
      auto partition_data = reinterpret_cast<const NdmPartitionData*>(data);
      data += sizeof(*partition_data);

      // TODO(40208): Dump the partition parameters.
      data += partition_data->data_size;
    }
  }
}

}  // namespace internal
