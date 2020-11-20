// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <limits>
#include <utility>

#include <safemath/checked_math.h>

#include "fvm-host/format.h"
#include "fvm/snapshot-metadata-format.h"
#include "zircon/errors.h"

namespace {

template <class T>
uint32_t ToU32(T in) {
  if (in > std::numeric_limits<uint32_t>::max()) {
    fprintf(stderr, "%s:%d out of range %" PRIuMAX "\n", __FILE__, __LINE__,
            safemath::checked_cast<uintmax_t>(in));
    exit(-1);
  }
  return safemath::checked_cast<uint32_t>(in);
}

}  // namespace

InternalSnapshotMetaFormat::InternalSnapshotMetaFormat(
    size_t slice_size, const std::vector<fvm::PartitionSnapshotState>& partitions,
    const std::vector<fvm::SnapshotExtentType>& extents)
    : Format(), slice_size_(slice_size) {
  meta_ = fvm::SnapshotMetadata::Synthesize(partitions.data(), partitions.size(), extents.data(),
                                            extents.size())
              .value();
  zero_buf_ = std::unique_ptr<uint8_t[]>(new uint8_t[meta_.Get()->size()]);
}

InternalSnapshotMetaFormat::~InternalSnapshotMetaFormat() = default;

zx_status_t InternalSnapshotMetaFormat::GetVsliceRange(unsigned extent_index,
                                                       vslice_info_t* vslice_info) const {
  if (extent_index == 0) {
    vslice_info->vslice_start = 0;
    vslice_info->slice_count = 1;
    vslice_info->block_offset = 0;
    vslice_info->block_count = 1;
    vslice_info->zero_fill = false;
    return ZX_OK;
  }
  return ZX_ERR_OUT_OF_RANGE;
}

zx_status_t InternalSnapshotMetaFormat::GetSliceCount(uint32_t* slices_out) const {
  *slices_out = 1;
  return ZX_OK;
}

zx_status_t InternalSnapshotMetaFormat::FillBlock(size_t block_offset) {
  if (block_offset == 0) {
    // We'll read directly from meta_ later.
    reading_from_meta_ = true;
    return ZX_OK;
  }
  reading_from_meta_ = false;
  return ZX_OK;
}

zx_status_t InternalSnapshotMetaFormat::EmptyBlock() {
  reading_from_meta_ = false;
  return ZX_OK;
}

void* InternalSnapshotMetaFormat::Data() {
  return reading_from_meta_ ? meta_.Get()->data() : zero_buf_.get();
}

const char* InternalSnapshotMetaFormat::Name() const { return "internal"; }

uint32_t InternalSnapshotMetaFormat::BlockSize() const { return meta_.Get()->size(); }

uint32_t InternalSnapshotMetaFormat::BlocksPerSlice() const {
  return ToU32(slice_size_ / BlockSize());
}
