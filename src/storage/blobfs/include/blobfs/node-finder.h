// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_NODE_FINDER_H_
#define SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_NODE_FINDER_H_

#include <blobfs/format.h>

namespace blobfs {

class NodeFinder;

// NodeFinder subclasses return pointers to Inodes. When they go out of scope, DropInodePtr is
// called. This can be used to maintain any locks that might need to be in place.
struct InodePtrDeleter {
  InodePtrDeleter() = default;
  explicit InodePtrDeleter(NodeFinder* finder) : finder_(finder) {}
  void operator()(Inode* inode);
  NodeFinder* finder_ = nullptr;
};

// Don't assume that memory is freed when the pointer goes out of scope. Subclasses are free to
// implement a cache of nodes and hand out pointers to the cache instead.
using InodePtr = std::unique_ptr<Inode, InodePtrDeleter>;

// Interface to look up nodes.
class NodeFinder {
 public:
  virtual ~NodeFinder() = default;

  // Returns a pointer to the requested node.
  //
  // TODO(smklein): Return a zx_status_t to allow for invalid |ino| values.
  virtual InodePtr GetNode(uint32_t node_index) = 0;

  // Called when an InodePtr goes out of scope.
  virtual void DropInodePtr() {}
};

inline void InodePtrDeleter::operator()(Inode* inode) {
  if (finder_) {
    finder_->DropInodePtr();
  }
}

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_NODE_FINDER_H_
