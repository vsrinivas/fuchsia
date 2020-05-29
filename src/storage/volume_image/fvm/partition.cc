// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/partition.h"

namespace storage::volume_image {

bool Partition::LessThan::operator()(const Partition& lhs, const Partition& rhs) const {
  return std::tie(lhs.volume().name, lhs.volume().instance) <
         std::tie(rhs.volume().name, rhs.volume().instance);
}

}  // namespace storage::volume_image
