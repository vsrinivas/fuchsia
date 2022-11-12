// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_TEST_COMPATIBILITY_V2_FILE_BACKED_BLOCK_DEVICE_H_
#define SRC_STORAGE_F2FS_TEST_COMPATIBILITY_V2_FILE_BACKED_BLOCK_DEVICE_H_

namespace f2fs {

// This class augments an image file to a block device so that a filesystem can operate on the image
// file. During each I/O process, POSIX interface is used instead of FIFO transaction.
class FileBackedBlockDevice : public block_client::BlockDevice {
 public:
  // Not copyable or movable
  FileBackedBlockDevice(const FileBackedBlockDevice&) = delete;
  FileBackedBlockDevice& operator=(const FileBackedBlockDevice&) = delete;
  FileBackedBlockDevice(FileBackedBlockDevice&&) = delete;
  FileBackedBlockDevice& operator=(FileBackedBlockDevice&&) = delete;

  FileBackedBlockDevice(fbl::unique_fd fd, const uint64_t block_count, const uint32_t block_size);

  zx::result<std::string> GetDevicePath() const final { return zx::error(ZX_ERR_NOT_SUPPORTED); }

  zx_status_t VolumeGetInfo(
      fuchsia_hardware_block_volume::wire::VolumeManagerInfo* out_manager_info,
      fuchsia_hardware_block_volume::wire::VolumeInfo* out_volume_info) const final {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t VolumeQuerySlices(const uint64_t* slices, size_t slices_count,
                                fuchsia_hardware_block_volume::wire::VsliceRange* out_ranges,
                                size_t* out_ranges_count) const final {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t VolumeExtend(uint64_t offset, uint64_t length) final { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t VolumeShrink(uint64_t offset, uint64_t length) final { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t FifoTransaction(block_fifo_request_t* requests, size_t count) final;
  zx_status_t BlockGetInfo(fuchsia_hardware_block::wire::BlockInfo* out_info) const final;
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out_vmoid) final;

 private:
  fbl::unique_fd fd_;

  std::mutex mutex_;
  const uint64_t block_count_;
  const uint32_t block_size_;
  const fuchsia_hardware_block::wire::Flag block_info_flags_ = {};
  const uint32_t max_transfer_size_ = fuchsia_hardware_block::wire::kMaxTransferUnbounded;
  std::map<vmoid_t, zx::vmo> vmos_ __TA_GUARDED(mutex_);
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_TEST_COMPATIBILITY_V2_FILE_BACKED_BLOCK_DEVICE_H_
