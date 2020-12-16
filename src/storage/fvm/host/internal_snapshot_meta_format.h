// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FVM_HOST_INTERNAL_SNAPSHOT_META_FORMAT_H_
#define SRC_STORAGE_FVM_HOST_INTERNAL_SNAPSHOT_META_FORMAT_H_

#include "src/storage/fvm/host/format.h"
#include "src/storage/fvm/snapshot_metadata.h"

class InternalSnapshotMetaFormat final : public Format {
 public:
  InternalSnapshotMetaFormat(size_t reserved_slices, size_t slice_size,
                             const std::vector<fvm::PartitionSnapshotState>& partitions,
                             const std::vector<fvm::SnapshotExtentType>& extents);
  ~InternalSnapshotMetaFormat() override;

  zx_status_t MakeFvmReady(size_t slice_size, uint32_t vpart_index, FvmReservation* reserve) final {
    return ZX_OK;
  }
  zx::status<ExtentInfo> GetExtent(unsigned index) const final;
  zx_status_t GetSliceCount(uint32_t* slices_out) const final;
  zx_status_t FillBlock(size_t block_offset) final;
  zx_status_t EmptyBlock() final;
  void* Data() final;
  uint32_t BlockSize() const final;
  uint32_t BlocksPerSlice() const final;

 private:
  const char* Name() const final;

  size_t reserved_slices_ = 0;
  size_t slice_size_ = 0;
  // When FillBlock(0) is called, we read from meta_. Otherwise we read from zero_buf_.
  bool reading_from_meta_ = false;
  std::unique_ptr<uint8_t[]> zero_buf_;
  fvm::SnapshotMetadata meta_;
};

#endif  // SRC_STORAGE_FVM_HOST_INTERNAL_SNAPSHOT_META_FORMAT_H_
