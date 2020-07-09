// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_GEOMETRY_H_
#define SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_GEOMETRY_H_

#include <stdint.h>

namespace block_verity {

// A block_verity partition is composed of three sections:
// * superblock
// * integrity section
// * data section
//
// The amount of space allocated to each section varies by block size (in
// bytes), hash function output length (in bytes), and block device size (in
// blocks).

struct IntegrityShape {
  uint64_t integrity_block_count;
  uint32_t tree_depth;  // number of levels of indirect blocks
};

// Given a number of data blocks, a block size, and a hash output size, compute
// how many integrity blocks will be needed to provide integrity data for them,
// and how deep the hash tree would need to be to cover it.
IntegrityShape IntegrityShapeFor(uint32_t block_size, uint32_t hash_size,
                                 uint64_t data_block_count);

struct BlockAllocation {
  // Number of blocks allocated for metadata/superblock.  This is 1.
  uint64_t superblock_count;

  // Number of blocks allocated to the "integrity section", including unusable
  // blocks.
  uint64_t padded_integrity_block_count;

  // Number of blocks allocated to the "data section"
  uint64_t data_block_count;

  IntegrityShape integrity_shape;
};

BlockAllocation BestSplitFor(uint32_t block_size, uint32_t hash_size, uint64_t total_blocks);

typedef uint64_t IntegrityBlockIndex;
typedef uint64_t DataBlockIndex;
typedef uint32_t HashIndex;

// A representation of where, within the integrity section, the hash of a
// particular block can be found.
struct HashLocation {
  // The index into the integrity section of the block we are consulting
  IntegrityBlockIndex integrity_block;

  // The index of the hash within that block.  To get a byte offset, multiply hash_in_block by
  // hash_size.
  HashIndex hash_in_block;
};

class Geometry {
 public:
  Geometry(uint32_t block_size, uint32_t hash_size, uint64_t total_blocks);

  // Given a data block index, return the location in the integrity section that
  // contains the hash of the literal data in that block.  This is used in the
  // verified read logic.
  HashLocation IntegrityDataLocationForDataBlock(DataBlockIndex data_block_index);

  // Given a block index into the integrity data, return the integrity data
  // block index and intra-block hash offset that covers that indirect block.
  // This is used on the verified read path -- after authenticating a data
  // block by checking the hash value at the location specified by
  // `IntegrityDataLocationForDataBlock`, we need to chain hash verification up
  // the merkle tree to the root.  This function tells us where to find the next
  // block up in the merkle tree.
  HashLocation NextIntegrityBlockUp(uint32_t distance_from_leaf,
                                    IntegrityBlockIndex integrity_block_index);

  uint32_t hashes_per_block_;
  BlockAllocation allocation_;
};

}  // namespace block_verity

#endif  // SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_GEOMETRY_H_
