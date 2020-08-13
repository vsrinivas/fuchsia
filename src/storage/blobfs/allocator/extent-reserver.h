// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_ALLOCATOR_EXTENT_RESERVER_H_
#define SRC_STORAGE_BLOBFS_ALLOCATOR_EXTENT_RESERVER_H_

#include <stdbool.h>
#include <stdint.h>
#include <zircon/types.h>

#include <bitmap/rle-bitmap.h>
#include <blobfs/format.h>

namespace blobfs {

// Allows extents to be reserved and unreserved. The purpose of reservation is to allow allocation
// of extents to occur without yet allocating structures which could be written out to durable
// storage.
//
// These extents may be observed by derived classes of ExtentReserver
class ExtentReserver {
 public:
  // Reserves space for blocks in memory. Does not update disk.
  //
  // |extent.Length()| must be > 0.
  void Reserve(const Extent& extent);

  // Unreserves space for blocks in memory. Does not update disk.
  void Unreserve(const Extent& extent);

  // Returns the total number of reserved blocks.
  uint64_t ReservedBlockCount() const;

 protected:
  // Returns an iterator to the underlying reserved blocks.
  //
  // This iterator becomes invalid on the next call to either |ReserveExtent| or
  // |UnreserveExtent|.
  bitmap::RleBitmap::const_iterator ReservedBlocksCbegin() const {
    return reserved_blocks_.cbegin();
  }

  bitmap::RleBitmap::const_iterator ReservedBlocksCend() const { return reserved_blocks_.end(); }

 private:
  bitmap::RleBitmap reserved_blocks_ = {};
};

// Wraps an extent reservation in RAII to hold the reservation active, and release it when it goes
// out of scope.
class ReservedExtent {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ReservedExtent);

  // Creates a reserved extent.
  //
  // |extent.Length()| must be > 0.
  ReservedExtent(ExtentReserver* reserver, Extent extent);
  ReservedExtent(ReservedExtent&& o);
  ReservedExtent& operator=(ReservedExtent&& o);
  ~ReservedExtent();

  // Access the underlying extent which has been reserved.
  //
  // Unsafe to call if this extent has not actually been reserved.
  const Extent& extent() const;

  // Split a reserved extent from [start, start + length) such that:
  // This retains [start, start + block_split),
  //  and returns [start + block_split, start + length)
  //
  // This function requires that |block_split| < |extent.block_count|.
  ReservedExtent SplitAt(BlockCountType block_split);

  // Releases the underlying reservation, unreserving the extent and preventing continued access
  // to |extent()|.
  void Reset();

 private:
  // Update internal state such that future calls to |Reserved| return false.
  void Release();

  // Identify if the underlying extent is reserved, and able to be accessed.
  bool Reserved() const;

  ExtentReserver* reserver_;
  Extent extent_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_ALLOCATOR_EXTENT_RESERVER_H_
