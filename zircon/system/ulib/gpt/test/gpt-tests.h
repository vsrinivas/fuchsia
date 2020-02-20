// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_GPT_TEST_GPT_TESTS_H_
#define ZIRCON_SYSTEM_ULIB_GPT_TEST_GPT_TESTS_H_
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>

#include <memory>
#include <utility>

#include <fbl/unique_fd.h>
#include <gpt/gpt.h>
#include <gpt/guid.h>
#include <mbr/mbr.h>

namespace gpt {
namespace {

constexpr uint32_t kBlockSize = 512;
constexpr uint64_t kBlockCount = 1 << 20;
constexpr uint64_t kAccptableMinimumSize = kBlockSize * kBlockCount;
constexpr uint64_t kGptMetadataSize = 1 << 18;  // 256KiB for now. See comment in LibGptTest::Init

static_assert(kGptMetadataSize <= kAccptableMinimumSize,
              "GPT size greater than kAccptableMinimumSize");

class LibGptTest {
 public:
  LibGptTest(bool use_ramdisk) : use_ramdisk_(use_ramdisk) {}
  ~LibGptTest() {}

  // Creates a ramdisk and initialize GPT on it.
  void Init();

  // Removes the backing ramdisk device.
  void Teardown();

  // Returns total size of the disk under test.
  uint64_t GetDiskSize() const { return blk_size_ * blk_count_; }

  // Return block size of the disk under test.
  uint32_t GetBlockSize() const { return blk_size_; }

  // Returns total number of block in the disk.
  uint64_t GetBlockCount() const { return blk_count_; }

  // Return total number of block free in disk after GPT is created.
  uint64_t GetUsableBlockCount() const { return usable_last_block_ - usable_start_block_; }

  // First block that is free to use after GPT is created.
  uint64_t GetUsableStartBlock() const { return usable_start_block_; }

  // Last block that is free to use after GPT is created.
  uint64_t GetUsableLastBlock() const { return usable_last_block_; }

  // Returns number of block GPT uses.
  uint64_t GptMetadataBlocksCount() const {
    // See comment in LibGptTest::Init
    return kGptMetadataSize / blk_size_;
  }

  // Returns the full device path.
  const char* GetDevicePath() const { return disk_path_; }

  // Remove all partition from GPT and keep device in GPT initialized state.
  void Reset();

  // Finalize uninitialized disk and verify.
  void Finalize();

  // Sync and verify.
  void Sync();

  // Get the Range from GPT.
  void ReadRange();

  // Read the MBR from the disk.
  zx_status_t ReadMbr(mbr::Mbr* mbr) const;

  // Prepare disk to run Add Partition tests.
  // 1. initialize GPT
  // 2. optionally sync
  // 3. get the usable range
  void PrepDisk(bool sync);

  // gpt_ changes across Reset(). So we do not expose pointer to GptDevice to
  // any of the test. Instead we expose following wrapper funtions for
  // a few GptDevice methods.
  bool IsGptValid() const { return gpt_->Valid(); }
  zx_status_t GetDiffs(uint32_t partition_index, uint32_t* diffs) const {
    return gpt_->GetDiffs(partition_index, diffs);
  }

  // Get's a partition at index pindex.
  gpt_partition_t* GetPartition(uint32_t pindex) const { return gpt_->GetPartition(pindex); }

  // Adds a partition
  zx_status_t AddPartition(const char* name, const uint8_t* type, const uint8_t* guid,
                           uint64_t offset, uint64_t blocks, uint64_t flags) {
    return gpt_->AddPartition(name, type, guid, offset, blocks, flags);
  }

  // removes a partition.
  zx_status_t RemovePartition(const uint8_t* guid) { return gpt_->RemovePartition(guid); }

  // removes all partitions.
  zx_status_t RemoveAllPartitions() { return gpt_->RemoveAllPartitions(); }

  // wrapper around GptDevice's SetPartitionType
  zx_status_t SetPartitionType(uint32_t partition_index, const uint8_t* type) {
    return gpt_->SetPartitionType(partition_index, type);
  }

  // wrapper around GptDevice's SetPartitionGuid
  zx_status_t SetPartitionGuid(uint32_t partition_index, const uint8_t* guid) {
    return gpt_->SetPartitionGuid(partition_index, guid);
  }

  // wrapper around GptDevice's SetPartitionRange
  zx_status_t SetPartitionRange(uint32_t partition_index, uint64_t start, uint64_t end) {
    return gpt_->SetPartitionRange(partition_index, start, end);
  }

  // wrapper around GptDevice's SetPartitionVisibility
  zx_status_t SetPartitionVisibility(uint32_t index, bool visible) {
    return gpt_->SetPartitionVisibility(index, visible);
  }

  // wrapper around GptDevice's GetPartitionFlags
  zx_status_t GetPartitionFlags(uint32_t index, uint64_t* flags) {
    return gpt_->GetPartitionFlags(index, flags);
  }

  // wrapper around GptDevice's SetPartitionFlags
  zx_status_t SetPartitionFlags(uint32_t index, uint64_t flags) {
    return gpt_->SetPartitionFlags(index, flags);
  }

 private:
  // Initialize a physical media.
  void InitDisk(const char* disk_path);

  // Create and initialize and ramdisk.
  void InitRamDisk();

  // Teardown the disk.
  void TearDownDisk();

  // Teardown and destroy ram disk.
  void TearDownRamDisk();

  // Block size of the device.
  uint32_t blk_size_ = kBlockSize;

  // Number of block in the disk.
  uint64_t blk_count_ = kBlockCount;

  // disk path
  char disk_path_[PATH_MAX] = {};

  // pointer to read GptDevice.
  std::unique_ptr<gpt::GptDevice> gpt_;

  // Open file descriptor to block device.
  fbl::unique_fd fd_;

  // Create and use ramdisk instead of a physical disk.
  bool use_ramdisk_;

  // An optional ramdisk structure, which is only non-nullptr if
  // |use_ramdisk_| is true.
  struct ramdisk_client* ramdisk_ = nullptr;

  // usable start block offset.
  uint64_t usable_start_block_ = UINT64_MAX;

  // usable last block offset.
  uint64_t usable_last_block_ = UINT64_MAX;
};

}  // namespace
}  // namespace gpt

#endif  // ZIRCON_SYSTEM_ULIB_GPT_TEST_GPT_TESTS_H_
