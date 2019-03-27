// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <gpt/c/gpt.h>

namespace gpt {

constexpr uint32_t kPartitionCount = 128;

// Size of array need to store "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
// There are other places where we use different macros to get this
// string length. Assert that we are in sync with the rest.
constexpr uint64_t kGuidStrLength = (2 * GPT_GUID_LEN) + (4 * sizeof('-')) + sizeof('\0');
static_assert(kGuidStrLength == GPT_GUID_STRLEN, "Guid print format changed");

// Size of null terminated char array to store non-utf16 GUID partition name.
constexpr uint64_t kGuidCNameLength = (GPT_NAME_LEN / 2) + 1;

constexpr uint32_t kGptDiffType = 0x01;
constexpr uint32_t kGptDiffGuid = 0x02;
constexpr uint32_t kGptDiffFirst = 0x04;
constexpr uint32_t kGptDiffLast = 0x08;
constexpr uint32_t kGptDiffFlags = 0x10;
constexpr uint32_t kGptDiffName = 0x20;

constexpr uint64_t kFlagHidden = 0x2;

// Sets or clears partition visibility flag
void SetPartitionVisibility(gpt_partition_t* partition, bool visible);

// Returns true if partition's kHiddenFlag is not set i.e. partition
// is visible
bool IsPartitionVisible(const gpt_partition_t* partition);

class GptDevice {
public:
    static zx_status_t Create(int fd, uint32_t blocksize, uint64_t blocks,
                              fbl::unique_ptr<GptDevice>* out_dev);

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

    // Returns pointer to partition entry on finding a valid entry at given index. Else
    // returns nullptr.
    // TODO(auradkar): consider returning changing the prototype to
    // zx_status_t GetPartition(uint32_t partition_index, const gpt_partition_t** out)
    gpt_partition_t* GetPartition(uint32_t partition_index) const;

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

    zx_status_t BlockRrPart();

    // Return device's block size
    uint64_t BlockSize() const { return blocksize_; }

    // Return total number of blocks in the device
    uint64_t TotalBlockCount() const { return blocks_; }

private:
    GptDevice() { valid_ = false; }

    zx_status_t FinalizeAndSync(bool persist);

    // read the partition table from the device.
    zx_status_t Init(int fd, uint32_t blocksize, uint64_t blocks);

    // true if the partition table on the device is valid
    bool valid_;

    // pointer to a list of partitions
    gpt_partition_t* partitions_[kPartitionCount] = {};

    // device to use
    fbl::unique_fd fd_;

    // block size in bytes
    uint64_t blocksize_ = 0;

    // number of blocks
    uint64_t blocks_ = 0;

    // true if valid mbr exists on disk
    bool mbr_ = false;

    // header buffer, should be primary copy
    gpt_header_t header_ = {};

    // partition table buffer
    gpt_partition_t ptable_[kPartitionCount] = {};

    // copy of buffer from when last init'd or sync'd.
    gpt_partition_t ptable_backup_[kPartitionCount] = {};
};

} // namespace gpt
