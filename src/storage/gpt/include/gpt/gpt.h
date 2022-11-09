// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_GPT_INCLUDE_GPT_GPT_H_
#define SRC_STORAGE_GPT_INCLUDE_GPT_GPT_H_

#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <lib/fpromise/result.h>
#include <lib/zx/result.h>

#include <memory>

#include <gpt/c/gpt.h>

namespace gpt {

// GPT magic number.
constexpr uint64_t kMagicNumber = GPT_MAGIC;

// GPT version 1.0
constexpr uint32_t kRevision = 0x00010000;

// GPT expect fixed size header. Verify that size of gpt_header_t meets
// the standards.
constexpr uint32_t kHeaderSize = GPT_HEADER_SIZE;
static_assert(kHeaderSize == sizeof(gpt_header_t), "invalid gpt header size");

// A copy of GPT is always at block 1. Location of backup copy is pointed by
// field within gpt_header_t.
constexpr uint64_t kPrimaryHeaderStartBlock = 1;

// Block size is expected to be large enough to hold gpt_header_t. GPT
// entries array start in the next block i.e. 2.
constexpr uint64_t kPrimaryEntriesStartBlock = kPrimaryHeaderStartBlock + 1;

// Last block should contain the header for GPT backup copy.
constexpr uint64_t BackupHeaderStartBlock(uint64_t block_count) { return block_count - 1; }

// Maximum number of partitions supported.
constexpr uint32_t kPartitionCount = 128;

// Number of blocks required to hold gpt_header_t. This should be always 1.
constexpr uint32_t kHeaderBlocks = 1;

// Minimum supperted block size.
constexpr uint32_t kMinimumBlockSize = 512;

// Maximum supported blocks size. 1MiB.
constexpr uint32_t kMaximumBlockSize = 1 << 20;

// GPT expect fixed size entry structure. Verify that size of gpt_entry_t meets
// the standards.
constexpr uint32_t kEntrySize = GPT_ENTRY_SIZE;
static_assert(kEntrySize == sizeof(gpt_entry_t), "invalid gpt entry size");

// Maximum size of the partition entry table.
constexpr size_t kMaxPartitionTableSize = static_cast<size_t>(kPartitionCount) * kEntrySize;

// Size of array need to store "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
// There are other places where we use different macros to get this
// string length. Assert that we are in sync with the rest.
constexpr size_t kGuidStrLength = (2UL * GPT_GUID_LEN) + (4UL * sizeof('-')) + sizeof('\0');
static_assert(kGuidStrLength == GPT_GUID_STRLEN, "Guid print format changed");

// Size of null terminated char array to store non-utf16 GUID partition name.
constexpr uint64_t kGuidCNameLength = (GPT_NAME_LEN / 2) + 1;
// Maximum size, including null terminator, of a partition's name in UTF-8.
// It's at most 3 UTF-8 code units for every UTF-16 code unit.  Code points > 0x10000 (which
// require 4 UTF-8 code units) get encoded as surrogate pairs in UTF-16.
constexpr size_t kMaxUtf8NameLen = ((GPT_NAME_LEN / sizeof(char16_t)) * 3) + 1;

constexpr uint32_t kGptDiffType = 0x01;
constexpr uint32_t kGptDiffGuid = 0x02;
constexpr uint32_t kGptDiffFirst = 0x04;
constexpr uint32_t kGptDiffLast = 0x08;
constexpr uint32_t kGptDiffFlags = 0x10;
constexpr uint32_t kGptDiffName = 0x20;

constexpr uint64_t kFlagHidden = 0x2;

// Returns the maximum size in bytes to hold header block and partition table.
constexpr fpromise::result<size_t, zx_status_t> MinimumBytesPerCopy(uint64_t block_size) {
  if (block_size < kHeaderSize) {
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }
  return fpromise::ok(block_size + kMaxPartitionTableSize);
}

// Returns the maximum blocks needed to hold header block and partition table.
constexpr fpromise::result<uint64_t, zx_status_t> MinimumBlocksPerCopy(uint64_t block_size) {
  if (block_size < kHeaderSize) {
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }
  return fpromise::ok((MinimumBytesPerCopy(block_size).value() + block_size - 1) / block_size);
}

// Returns the minimum blocks needed to hold two copies of GPT at appropriate
// offset (considering mbr block).
constexpr fpromise::result<uint64_t, zx_status_t> MinimumBlockDeviceSize(uint64_t block_size) {
  if (block_size < kHeaderSize) {
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }
  // There are two copies of GPT and a block for MBR(or such use).
  return fpromise::ok(kPrimaryHeaderStartBlock + (2 * MinimumBlocksPerCopy(block_size).value()));
}

// Returns number of addressable blocks. On finding entry
//  - nullptr, returns ZX_ERR_INVALID_ARGS
//  - invalid, returns ZX_ERR_BAD_STATE
//  - uninitialized, returns ZX_ERR_NOT_FOUND
fpromise::result<uint64_t, zx_status_t> EntryBlockCount(const gpt_entry_t* entry);

// Sets or clears partition visibility flag
void SetPartitionVisibility(gpt_partition_t* partition, bool visible);

// Returns true if partition's kHiddenFlag is not set i.e. partition
// is visible
bool IsPartitionVisible(const gpt_partition_t* partition);

// Returns a null terminated UTF-8 representation of the partition name.
zx::result<> GetPartitionName(const gpt_entry_t& entry, char* name, size_t capacity);

class GptDevice {
 public:
  static zx_status_t Create(fidl::ClientEnd<fuchsia_hardware_block::Block> device,
                            uint32_t blocksize, uint64_t blocks,
                            std::unique_ptr<GptDevice>* out_dev);

  // Loads gpt header and gpt entries array from |buffer| of length |size|
  // belonging to "block device" with |blocks| number of blocks and each
  // block of size |blocksize|. On finding valid header and entries, returns
  // pointer to GptDevice in |oput_dev|.
  static zx_status_t Load(const uint8_t* buffer, uint64_t size, uint32_t blocksize, uint64_t blocks,
                          std::unique_ptr<GptDevice>* out_dev);

  // returns true if the partition table on the device is valid
  bool Valid() const { return valid_; }

  // Returns the range of usable blocks within the GPT, from [block_start, block_end] (inclusive)
  zx_status_t Range(uint64_t* block_start, uint64_t* block_end) const;

  // Writes changes to partition table to the device. If the device does not contain valid
  // GPT, a gpt header gets created. Sync doesn't nudge block device driver to rescan the
  // partitions. So it is the caller's responsibility to rescan partitions for the changes
  // if needed.
  zx_status_t Sync();

  // perform all checks and computations on the in-memory representation, but DOES
  // NOT write it out to disk. To perform checks AND write to disk, use Sync
  zx_status_t Finalize();

  // Adds a partition to in-memory instance of GptDevice. The changes stay visible
  // only to this instace. Needs a Sync to write the changes to the device.
  zx_status_t AddPartition(const char* name, const uint8_t* type, const uint8_t* guid,
                           uint64_t offset, uint64_t blocks, uint64_t flags);

  // Writes zeroed blocks at an arbitrary offset (in blocks) within the device.
  //
  // Can be used alongside gpt_partition_add to ensure a newly created partition
  // will not read stale superblock data.
  zx_status_t ClearPartition(uint64_t offset, uint64_t blocks);

  // Removes a partition from in-memory instance of GptDevice. The changes stay visible
  // only to this instace. Needs a Sync to write the changes to the device.
  zx_status_t RemovePartition(const uint8_t* guid);

  // Removes all partitions from in-memory instance of GptDevice. The changes stay visible
  // only to this instace. Needs a Sync to write the changes to the device.
  zx_status_t RemoveAllPartitions();

  // given a gpt device, get the GUID for the disk
  void GetHeaderGuid(uint8_t (*disk_guid_out)[GPT_GUID_LEN]) const;

  // return true if partition# idx has been locally modified
  zx_status_t GetDiffs(uint32_t idx, uint32_t* diffs) const;

  // Returns mutable pointer to partition entry at given index, otherwise ZX_ERR_NOT_FOUND
  // if the partition entry is nullptr, or ZX_ERR_OUT_OF_RANGE if the index is out of range.
  zx::result<gpt_partition_t*> GetPartition(uint32_t partition_index);

  // Returns const pointer to partition entry at given index, otherwise ZX_ERR_NOT_FOUND
  // if the partition entry is nullptr, or ZX_ERR_OUT_OF_RANGE if the index is out of range.
  zx::result<const gpt_partition_t*> GetPartition(uint32_t partition_index) const;

  // Updates the type of partition at index partition_index
  zx_status_t SetPartitionType(uint32_t partition_index, const uint8_t* type);

  // Updates the guid(id) of partition at index partition_index
  zx_status_t SetPartitionGuid(uint32_t partition_index, const uint8_t* guid);

  // Makes partition visible if 'visible' is true
  zx_status_t SetPartitionVisibility(uint32_t partition_index, bool visible);

  // Changes partition's partitions start and end blocks. If there is conflict with
  // either other partitions or the device, then returns non-ZX_OK value
  zx_status_t SetPartitionRange(uint32_t partition_index, uint64_t start, uint64_t end);

  // Returns current flags for partition at index partition_index
  zx_status_t GetPartitionFlags(uint32_t partition_index, uint64_t* flags) const;

  // Sets flags for partition at index partition_index
  zx_status_t SetPartitionFlags(uint32_t partition_index, uint64_t flags);

  // print out the GPT
  void PrintTable() const;

  // Return device's block size
  uint64_t BlockSize() const { return blocksize_; }

  uint32_t EntryCount() const {
    if (!valid_) {
      return kPartitionCount;
    }
    return header_.entries_count;
  }

  // Return number of bytes entries array occupies.
  uint64_t EntryArraySize() const {
    if (!valid_) {
      return kMaxPartitionTableSize;
    }

    return (static_cast<uint64_t>(header_.entries_count) * kEntrySize);
  }

  // Return number of blocks that entries array occupies.
  uint64_t EntryArrayBlockCount() const {
    return ((EntryArraySize() + blocksize_ - 1) / blocksize_);
  }

  // Return total number of blocks in the device
  uint64_t TotalBlockCount() const { return blocks_; }

  const fidl::ClientEnd<fuchsia_hardware_block::Block>& device() const;

 private:
  GptDevice() { valid_ = false; }

  zx_status_t FinalizeAndSync(bool persist);

  // read the partition table from the device.
  static zx_status_t Init(fidl::ClientEnd<fuchsia_hardware_block::Block> device, uint32_t blocksize,
                          uint64_t block_count, std::unique_ptr<GptDevice>* out_dev);

  zx_status_t LoadEntries(const uint8_t* buffer, uint64_t buffer_size, uint64_t block_count);

  // Walks entries array and returns error if crc doesn't match or ValidateEntry returns error.
  zx_status_t ValidateEntries(const uint8_t* buffer, uint64_t block_count) const;

  zx::result<gpt_partition_t*> GetPartitionPtr(uint32_t partition_index) const;

  // true if the partition table on the device is valid
  bool valid_;

  // pointer to a list of partitions
  gpt_partition_t* partitions_[kPartitionCount] = {};

#ifdef __Fuchsia__
  fidl::ClientEnd<fuchsia_hardware_block::Block> device_;
#endif

  // block size in bytes
  uint64_t blocksize_ = 0;

  // number of blocks
  uint64_t blocks_ = 0;

  // header buffer, should be primary copy
  gpt_header_t header_ = {};

  // partition table buffer
  gpt_partition_t ptable_[kPartitionCount] = {};

  // copy of buffer from when last init'd or sync'd.
  gpt_partition_t ptable_backup_[kPartitionCount] = {};
};

// On success returns initialized gpt header. On finding either |block_size| or
// |block_count| is not large enough, returns error.
fpromise::result<gpt_header_t, zx_status_t> InitializePrimaryHeader(uint64_t block_size,
                                                                    uint64_t block_count);

// Validates gpt header. Each type of inconsistency leads to unique status code.
// The status can be used to print user friendly error messages.
zx_status_t ValidateHeader(const gpt_header_t* header, uint64_t block_count);

// A gpt entry can exists in three states
//  - unused; where all fields are zeroed.
//  - valid; field have sensible values;
//  - error; one or more fields are in inconsistent state.
// Returns
//  - true if the entry is valid
//  - false if entry is unused
//  - error if entry fields are inconsistent
fpromise::result<bool, zx_status_t> ValidateEntry(const gpt_entry_t* entry);

// Converts status returned by ValidateHeader to a human readable error message.
const char* HeaderStatusToCString(zx_status_t status);

}  // namespace gpt

#endif  // SRC_STORAGE_GPT_INCLUDE_GPT_GPT_H_
