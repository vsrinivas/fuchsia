// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/qcow_refcount.h"

#include "garnet/lib/machina/qcow.h"
#include "lib/fxl/logging.h"

namespace machina {

// Per https://github.com/qemu/qemu/blob/master/docs/interop/qcow2.txt
// This value may not exceed 6 (i.e. refcount_bits = 64)
static constexpr uint32_t kQcowMaxRefcountOrder = 6;

QcowRefcount::QcowRefcount() = default;

QcowRefcount::QcowRefcount(QcowRefcount&& o)
    : fd_(o.fd_),
      refcount_order_(o.refcount_order_),
      cluster_size_(o.cluster_size_),
      refcount_table_(std::move(o.refcount_table_)),
      loaded_block_index_(o.loaded_block_index_),
      loaded_block_(std::move(o.loaded_block_)) {
  o.fd_ = -1;
}

QcowRefcount& QcowRefcount::operator=(QcowRefcount&& o) {
  refcount_order_ = o.refcount_order_;
  cluster_size_ = o.cluster_size_;
  loaded_block_index_ = o.loaded_block_index_;
  refcount_table_ = std::move(o.refcount_table_);
  loaded_block_ = std::move(o.loaded_block_);
  o.fd_ = -1;
  return *this;
}

zx_status_t QcowRefcount::Load(int fd, const QcowHeader& header) {
  if (header.refcount_order > kQcowMaxRefcountOrder) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Allocate space to store a single refcount block (second level table).
  if (loaded_block_.size() != header.cluster_size()) {
    loaded_block_.reset(new uint8_t[header.cluster_size()],
                        header.cluster_size());
  }

  // Allocate the top-level refcount table.
  size_t refcount_table_size = header.cluster_size() *
                               header.refcount_table_clusters /
                               sizeof(RefcountTableEntry);
  if (refcount_table_.size() != refcount_table_size) {
    refcount_table_.reset(new RefcountTableEntry[refcount_table_size],
                          refcount_table_size);
  }

  // Read in the top-level table.
  off_t ret = lseek(fd, header.refcount_table_offset, SEEK_SET);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to seek to refcount table: " << strerror(errno);
    return ZX_ERR_IO;
  }
  ssize_t result = read(fd, refcount_table_.get(), refcount_table_.size());
  if (result != static_cast<ssize_t>(refcount_table_.size())) {
    FXL_LOG(ERROR) << "Failed to read refcount table: " << strerror(errno);
    return ZX_ERR_IO;
  }

  fd_ = fd;
  refcount_order_ = header.refcount_order;
  cluster_size_ = 1u << header.cluster_bits;
  return ZX_OK;
}

zx_status_t QcowRefcount::ReadRefcount(size_t cluster_index, uint64_t* count) {
  uint32_t entries_per_block = (cluster_size_ * CHAR_BIT) / refcount_bits();
  uint32_t block_index = cluster_index / entries_per_block;
  uint32_t block_offset = cluster_index % entries_per_block;

  uint8_t* cluster;
  zx_status_t status = ReadRefcountBlock(block_index, &cluster);
  if (status != ZX_OK) {
    return status;
  }

  switch (refcount_order_) {
    case 0: /* 1 bit */
    case 1: /* 2 bit */
    case 2: /* 4 bit */ {
      uint8_t entries_per_byte = CHAR_BIT / refcount_bits();
      size_t cluster_byte_offset = block_offset / entries_per_byte;
      uint8_t shift = refcount_bits() * (block_offset % entries_per_byte);
      *count = cluster[cluster_byte_offset] >> shift & refcount_mask();
      return ZX_OK;
    }
    case 3: /* 8 bit */
      *count = cluster[block_offset];
      return ZX_OK;
    case 4: /* 16 bit */
      *count = BigToHostEndianTraits::Convert(
          reinterpret_cast<uint16_t*>(cluster)[block_offset]);
      return ZX_OK;
    case 5: /* 32 bit */
      *count = BigToHostEndianTraits::Convert(
          reinterpret_cast<uint32_t*>(cluster)[block_offset]);
      return ZX_OK;
    case 6: /* 64 bit */
      *count = BigToHostEndianTraits::Convert(
          reinterpret_cast<uint64_t*>(cluster)[block_offset]);
      return ZX_OK;
    default:
      return ZX_ERR_BAD_STATE;
  }
}

zx_status_t QcowRefcount::WriteRefcount(size_t cluster_index, uint64_t count) {
  if (count & ~refcount_mask()) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t entries_per_block = (cluster_size_ * CHAR_BIT) / refcount_bits();
  uint32_t block_index = cluster_index / entries_per_block;
  uint32_t block_offset = cluster_index % entries_per_block;

  uint8_t* cluster;
  zx_status_t status = ReadRefcountBlock(block_index, &cluster);
  if (status != ZX_OK) {
    return status;
  }

  switch (refcount_order_) {
    case 0: /* 1 bit */
    case 1: /* 2 bit */
    case 2: /* 4 bit */ {
      uint8_t entries_per_byte = CHAR_BIT / refcount_bits();
      size_t cluster_byte_offset = block_offset / entries_per_byte;
      uint8_t shift = refcount_bits() * (block_offset % entries_per_byte);
      cluster[cluster_byte_offset] &= ~(refcount_mask() << shift);
      cluster[cluster_byte_offset] |= count << shift;
      return ZX_OK;
    }
    case 3: /* 8 bit */
      cluster[block_offset] = count;
      return ZX_OK;
    case 4: /* 16 bit */
      reinterpret_cast<uint16_t*>(cluster)[block_offset] =
          HostToBigEndianTraits::Convert(static_cast<uint16_t>(count));
      return ZX_OK;
    case 5: /* 32 bit */
      reinterpret_cast<uint32_t*>(cluster)[block_offset] =
          HostToBigEndianTraits::Convert(static_cast<uint32_t>(count));
      return ZX_OK;
    case 6: /* 64 bit */
      reinterpret_cast<uint64_t*>(cluster)[block_offset] =
          HostToBigEndianTraits::Convert(count);
      return ZX_OK;
    default:
      return ZX_ERR_BAD_STATE;
  }
}

zx_status_t QcowRefcount::ReadRefcountBlock(uint32_t block_index,
                                            uint8_t** block) {
  if (block_index == loaded_block_index_) {
    *block = loaded_block_.get();
    return ZX_OK;
  }
  if (!refcount_table_ || !loaded_block_) {
    return ZX_ERR_BAD_STATE;
  }

  RefcountTableEntry refcount_block_offset =
      HostToBigEndianTraits::Convert(refcount_table_[block_index]);
  off_t ret = lseek(fd_, refcount_block_offset, SEEK_SET);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to seek to refcnt table: " << ret;
    return ZX_ERR_IO;
  }
  ssize_t result = read(fd_, loaded_block_.get(), loaded_block_.size());
  if (result != static_cast<ssize_t>(loaded_block_.size())) {
    FXL_LOG(ERROR) << "Failed to read refcnt table: " << ret;
    return ZX_ERR_IO;
  }
  loaded_block_index_ = block_index;
  *block = loaded_block_.get();
  return ZX_OK;
}

}  //  namespace machina
