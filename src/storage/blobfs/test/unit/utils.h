// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_TEST_UNIT_UTILS_H_
#define SRC_STORAGE_BLOBFS_TEST_UNIT_UTILS_H_

#include <memory>
#include <optional>
#include <vector>

#include <fbl/auto_lock.h>
#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/block_device.h"
#include "src/storage/blobfs/allocator/allocator.h"
#include "src/storage/blobfs/transaction_manager.h"

namespace blobfs {

constexpr uint32_t kBlockSize = kBlobfsBlockSize;
constexpr groupid_t kGroupID = 2;
constexpr size_t kWritebackCapacity = 8;
constexpr uint32_t kDeviceBlockSize = 1024;
constexpr uint32_t kDiskBlockRatio = kBlockSize / kDeviceBlockSize;

// Callback for MockTransactionManager to invoke on calls to Transaction(). |request| is performed
// on the provided |vmo|.
using TransactionCallback =
    fit::function<zx_status_t(const block_fifo_request_t& request, const zx::vmo& vmo)>;

using block_client::BlockDevice;

// A simplified TransactionManager to be used when unit testing structures which require one (e.g.
// WritebackQueue, Journal). Allows vmos to be attached/detached and a customized callback to be
// invoked on transaction completion.
// This class is thread-safe.
class MockTransactionManager : public TransactionManager, public block_client::BlockDevice {
 public:
  MockTransactionManager() = default;
  ~MockTransactionManager() = default;

  // Sets the |callback| to be invoked for each request on calls to Transaction().
  void SetTransactionCallback(TransactionCallback callback) {
    fbl::AutoLock lock(&lock_);
    transaction_callback_ = std::move(callback);
  }

  uint64_t BlockNumberToDevice(uint64_t block_num) const final { return block_num; }

  block_client::BlockDevice* GetDevice() final { return this; }
  zx_status_t RunOperation(const storage::Operation& operation,
                           storage::BlockBuffer* buffer) final {
    return ZX_OK;
  }

  const Superblock& Info() const final { return superblock_; }

  Superblock& MutableInfo() { return superblock_; }

  zx_status_t AddInodes(Allocator*) final { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t AddBlocks(size_t nblocks, RawBitmap* map) final { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) final;

  zx_status_t BlockDetachVmo(storage::Vmoid vmoid) final;

  fs::Journal* GetJournal() final {
    ZX_ASSERT(false);
    return nullptr;
  }

  // FIFO protocol.
  zx_status_t FifoTransaction(block_fifo_request_t* requests, size_t count) final;

  zx::result<std::string> GetDevicePath() const final { return zx::error(ZX_ERR_NOT_SUPPORTED); }

  zx_status_t BlockGetInfo(fuchsia_hardware_block_BlockInfo* out_info) const final {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t VolumeGetInfo(fuchsia_hardware_block_volume_VolumeManagerInfo* out_manager_info,
                            fuchsia_hardware_block_volume_VolumeInfo* out_volume_info) const final {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t VolumeQuerySlices(const uint64_t* slices, size_t slices_count,
                                fuchsia_hardware_block_volume_VsliceRange* out_ranges,
                                size_t* out_ranges_count) const final {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t VolumeExtend(uint64_t offset, uint64_t length) final { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t VolumeShrink(uint64_t offset, uint64_t length) final { return ZX_ERR_NOT_SUPPORTED; }

 private:
  std::shared_ptr<BlobfsMetrics> metrics_ = std::make_shared<BlobfsMetrics>(false);
  Superblock superblock_{};
  std::vector<std::optional<zx::vmo>> attached_vmos_ __TA_GUARDED(lock_);
  TransactionCallback transaction_callback_ __TA_GUARDED(lock_);
  fbl::Mutex lock_;
};

// A trivial space manager, incapable of resizing.
class MockSpaceManager : public SpaceManager {
 public:
  Superblock& MutableInfo() { return superblock_; }

  const Superblock& Info() const final { return superblock_; }
  zx_status_t AddInodes(Allocator*) final { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t AddBlocks(size_t nblocks, RawBitmap* map) final { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) final {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t BlockDetachVmo(storage::Vmoid vmoid) final { return ZX_ERR_NOT_SUPPORTED; }

 private:
  Superblock superblock_{};
};

// Create a block and node map of the requested size, update the superblock of
// the |space_manager|, and create an allocator from this provided info.
void InitializeAllocator(size_t blocks, size_t nodes, MockSpaceManager* space_manager,
                         std::unique_ptr<Allocator>* out);

// Force the allocator to become maximally fragmented by allocating
// every-other block within up to |blocks|.
void ForceFragmentation(Allocator* allocator, size_t blocks);

// Save the extents within |in| in a non-reserved vector |out|.
void CopyExtents(const std::vector<ReservedExtent>& in, std::vector<Extent>* out);

// Save the nodes within |in| in a non-reserved vector |out|.
void CopyNodes(const std::vector<ReservedNode>& in, std::vector<uint32_t>* out);

// Reads |size| bytes from the |device| at byte offset |dev_offset| into |buf|.
// Expects |size| and |dev_offset| to be multiple of |device| block size.
void DeviceBlockRead(BlockDevice* device, void* buf, size_t size, uint64_t dev_offset);

// Writes |size| bytes from the |buf| to the |device| at offset |dev_offset|.
// Expects |size| and |dev_offset| to be multiple of |device| block size.
void DeviceBlockWrite(BlockDevice* device, const void* buf, size_t size, uint64_t dev_offset);

std::string GetCompressionAlgorithmName(CompressionAlgorithm compression_algorithm);

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_TEST_UNIT_UTILS_H_
