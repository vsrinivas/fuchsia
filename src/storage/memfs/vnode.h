// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MEMFS_VNODE_H_
#define SRC_STORAGE_MEMFS_VNODE_H_

#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace memfs {

class Dnode;

class Vnode : public fs::Vnode {
 public:
  virtual zx_status_t SetAttributes(fs::VnodeAttributesUpdate a) final;
  virtual void Sync(SyncCallback closure) final;

  // To be more specific: Is this vnode connected into the directory hierarchy?
  // VnodeDirs can be unlinked, and this method will subsequently return false.
  bool IsDirectory() const { return dnode_ != nullptr; }
  void UpdateModified();

  ~Vnode() override;

  uint64_t ino() const { return ino_; }

  static uint64_t GetInoCounter() { return ino_ctr_.load(std::memory_order_relaxed); }
  static uint64_t GetDeletedInoCounter() {
    return deleted_ino_ctr_.load(std::memory_order_relaxed);
  }

  // TODO(smklein): Move member into the VnodeDir subclass.
  // Directories contain a raw reference to their location in the filesystem hierarchy.
  // Although this would have safer memory semantics with an actual weak pointer, it is
  // currently raw to avoid circular dependencies from Vnode -> Dnode -> Vnode.
  //
  // Caution must be taken when detaching Dnodes from their parents to avoid leaving
  // this reference dangling.
  Dnode* dnode_ = nullptr;
  // The Dnode to parent pointer is always set for both directory and file.
  Dnode* dnode_parent_ = nullptr;
  uint32_t link_count_ = 0;

 protected:
  explicit Vnode(PlatformVfs* vfs);

  uint64_t ino_ = 0;
  uint64_t create_time_ = 0;
  uint64_t modify_time_ = 0;

 private:
  static std::atomic<uint64_t> ino_ctr_;
  static std::atomic<uint64_t> deleted_ino_ctr_;
};

}  // namespace memfs

#endif  // SRC_STORAGE_MEMFS_VNODE_H_
