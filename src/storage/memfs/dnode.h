// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MEMFS_DNODE_H_
#define SRC_STORAGE_MEMFS_DNODE_H_

#include <lib/fdio/vfs.h>
#include <limits.h>
#include <string.h>

#include <memory>
#include <string_view>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace memfs {

class VnodeMemfs;

constexpr size_t kDnodeNameMax = NAME_MAX;
static_assert(NAME_MAX == 255, "NAME_MAX must be 255");

// Assert that kDnodeNameMax can be used as a bitmask
static_assert(((kDnodeNameMax + 1) & kDnodeNameMax) == 0,
              "Expected kDnodeNameMax to be one less than a power of two");

// The named portion of a node, representing the named hierarchy.
//
// Dnodes always have one corresponding Vnode (a name represents one vnode).
// Vnodes may be represented by multiple Dnodes (a vnode may have many names).
//
// Dnodes are owned by their parents.
class Dnode : public fbl::DoublyLinkedListable<std::unique_ptr<Dnode>> {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Dnode);

  // Allocates a dnode, attached to a vnode
  static std::unique_ptr<Dnode> Create(std::string_view name, fbl::RefPtr<VnodeMemfs> vn);

  // Takes a parent-less node and makes it a child of the parent node.
  //
  // Increments child link count by one.
  // If the child is a directory, increments the parent link count by one.
  static void AddChild(Dnode* parent, std::unique_ptr<Dnode> child);

  ~Dnode();

  // Removes a dnode from its parent (if dnode has a parent)
  // Decrements parent link count by one.
  std::unique_ptr<Dnode> RemoveFromParent();

  // Detaches a dnode from its parent / vnode.
  // Decrements dn->vnode link count by one (if it exists).
  //
  // Precondition: Dnode has no children.
  // Postcondition: "this" may be destroyed.
  void Detach();

  bool HasChildren() const { return !children_.is_empty(); }

  // Look up the child dnode (within a parent directory) by name.
  // Returns ZX_OK if the child is found.
  //
  // If the looked up child is the current node, "out" is nullptr, and
  // ZX_OK is still returned.
  // If "out" is provided as "nullptr", the returned status appears the
  // same, but the "out" argument is not touched.
  zx_status_t Lookup(std::string_view name, Dnode** out);

  // Acquire a pointer to the vnode underneath this dnode.
  // Acquires a reference to the underlying vnode.
  fbl::RefPtr<VnodeMemfs> AcquireVnode() const;

  // Get a pointer to the parent Dnode. If current Dnode is root, return nullptr.
  Dnode* GetParent() const;

  // Returns ZX_OK if the dnode may be unlinked
  zx_status_t CanUnlink() const;

  // Read dirents (up to len bytes worth) into data.
  // ReaddirStart reads the canned "." and ".." entries that should appear
  // at the beginning of a directory.
  // On success, return the number of bytes read.
  static zx_status_t ReaddirStart(fs::DirentFiller* df, void* cookie);
  void Readdir(fs::DirentFiller* df, void* cookie) const;

  // Answers the question: "Is dn a subdirectory of this?"
  bool IsSubdirectory(const Dnode* dn) const;

  // Functions to take / steal the allocated dnode name.
  std::unique_ptr<char[]> TakeName();
  void PutName(std::unique_ptr<char[]> name, size_t len);

  bool IsDirectory() const;

 private:
  friend struct TypeChildTraits;

  Dnode(fbl::RefPtr<VnodeMemfs> vn, std::unique_ptr<char[]> name, uint32_t flags);

  size_t NameLen() const;
  bool NameMatch(std::string_view name) const;

  fbl::RefPtr<VnodeMemfs> vnode_;
  // Refers to the parent named node in the directory hierarchy.
  // A weak reference is used here to avoid a circular dependency, where
  // parents own children, but children point to their parents.
  Dnode* parent_;
  // Used to impose an absolute order on dnodes within a directory.
  size_t ordering_token_;
  fbl::DoublyLinkedList<std::unique_ptr<Dnode>> children_;
  uint32_t flags_;
  std::unique_ptr<char[]> name_;
};

}  // namespace memfs

#endif  // SRC_STORAGE_MEMFS_DNODE_H_
