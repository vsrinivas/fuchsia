// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <blobfs/format.h>
#include <zircon/types.h>

namespace blobfs {

// Interface for a class which may be used to iterate over a collection of extents.
class ExtentIterator {
 public:
  virtual ~ExtentIterator() = default;

  // Returns true if there are no more extents to be consumed.
  virtual bool Done() const = 0;

  // On success, returns ZX_OK and the next extent in |out|.
  virtual zx_status_t Next(const Extent** out) = 0;

  // Returns the number of blocks iterated past already. Updated on each
  // call to |Next|.
  virtual uint64_t BlockIndex() const = 0;
};

// Interface to look up nodes.
class NodeFinder {
 public:
  virtual ~NodeFinder() = default;

  // Returns a pointer to the requested node.
  //
  // TODO(smklein): Return a zx_status_t to allow for invalid |ino| values.
  virtual Inode* GetNode(uint32_t node_index) = 0;
};

}  // namespace blobfs
