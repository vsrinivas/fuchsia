// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLOBFS_NODE_FINDER_H_
#define BLOBFS_NODE_FINDER_H_

#include <blobfs/format.h>

namespace blobfs {

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

#endif  // BLOBFS_NODE_FINDER_H_
