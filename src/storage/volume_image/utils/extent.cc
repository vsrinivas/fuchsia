// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/extent.h"

#include <cstdio>
#include <tuple>

#include <fbl/algorithm.h>

namespace storage::volume_image {

std::tuple<Extent, Extent::Tail> Extent::Convert(uint64_t offset, uint64_t block_size) const {
  // Best case scenario block boundaries can be aligned between extents, but we should not assume
  // this.
  uint64_t total_size = this->block_size() * count();
  uint64_t extent_count = fbl::round_up(total_size, block_size) / block_size;
  uint64_t tail_offset = (total_size + block_size - 1) % block_size + 1;
  Tail tail = Tail(tail_offset, block_size - tail_offset);

  return std::make_tuple(Extent(offset, extent_count, block_size), tail);
}

}  // namespace storage::volume_image
