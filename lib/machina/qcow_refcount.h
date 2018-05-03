// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_QCOW_REFCOUNT_H_
#define GARNET_LIB_MACHINA_QCOW_REFCOUNT_H_

#include <fbl/array.h>
#include <stdint.h>
#include <zircon/types.h>

#include "garnet/lib/machina/bits.h"
#include "lib/fxl/macros.h"

namespace machina {

struct QcowHeader;

// The QCOW refcount table manages the number of references to each physical
// cluster on the disk.
//
// In the simple case, a refcount of 0 indicates the block is currently unused
// and is free to be allocated whenever more disk clusters are required.
//
// A refcount of 1 indicates the cluster is in use by exactly one purpose. Reads
// and writes can touch the block directly.
//
// A refcount > 1 indicates that the block is shared. Reads may complete
// normally but writes will require the cluster to be copied to a new cluster
// for the write before completing.
class QcowRefcount {
 public:
  QcowRefcount();

  QcowRefcount(QcowRefcount&& other);
  QcowRefcount& operator=(QcowRefcount&& other);

  // Read in the top level refcount table.
  zx_status_t Load(int fd, const QcowHeader& header);

  // Reads the refcount for the physical cluster with the provided |index|. The
  // index is the cluster number, or in other words:
  //
  //   cluster_offset = cluster_number << cluster_bits
  //
  // On success |ZX_OK| is returned and the clusters refcount is written to
  // |count|.
  zx_status_t ReadRefcount(size_t index, uint64_t* count);

  // Writes the refcount for the physical cluster with the provided |index|. The
  // index is the cluster number, or in other words:
  //
  //   cluster_offset = cluster_number << cluster_bits
  //
  // On success |ZX_OK| is returned and the clusters refcount is written to
  // |count|. If |count| would overflow refcount field |ZX_ERR_INVALID_ARGS| is
  // returned.
  zx_status_t WriteRefcount(size_t index, uint64_t count);

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(QcowRefcount);

  // The number of bits used for each clusters refcount field.
  uint32_t refcount_bits() const { return 1u << refcount_order_; }

  // A bit mask that matches the width of the culsters refcount field.
  uint64_t refcount_mask() const { return bit_mask<uint64_t>(refcount_bits()); }

  zx_status_t ReadRefcountBlock(uint32_t block_index, uint8_t** block);

  int fd_ = -1;
  uint32_t refcount_order_;
  uint32_t cluster_size_;

  // Retain the entire top level refcount table in memory.
  using RefcountTableEntry = uint64_t;
  fbl::Array<RefcountTableEntry> refcount_table_;

  // Only cache the most recently accessed refcount block.
  int64_t loaded_block_index_ = -1;
  fbl::Array<uint8_t> loaded_block_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_QCOW_REFCOUNT_H_
