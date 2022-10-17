// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_ITERATOR_EXTENT_ITERATOR_H_
#define SRC_STORAGE_BLOBFS_ITERATOR_EXTENT_ITERATOR_H_

#include <lib/zx/status.h>
#include <stdbool.h>
#include <stdint.h>
#include <zircon/types.h>

#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/node_finder.h"

namespace blobfs {

// Interface for a class which may be used to iterate over a collection of extents.
class ExtentIterator {
 public:
  virtual ~ExtentIterator() = default;

  // Returns true if there are no more extents to be consumed.
  virtual bool Done() const = 0;

  // On success, returns the next extent.
  virtual zx::result<const Extent*> Next() = 0;

  // Returns the number of blocks iterated past already. Updated on each call to |Next|.
  virtual uint64_t BlockIndex() const = 0;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_ITERATOR_EXTENT_ITERATOR_H_
