// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_NAMESPACE_LOCAL_VNODE_H_
#define LIB_FDIO_NAMESPACE_LOCAL_VNODE_H_

#include <lib/zx/channel.h>
#include <limits.h>
#include <zircon/types.h>

#include <fbl/function.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/string_piece.h>

namespace fdio_internal {

using EnumerateCallback =
    fbl::Function<zx_status_t(const fbl::StringPiece& path, const zx::channel& channel)>;

// Represents a mapping from a string name to a remote connection.
//
// Each LocalVnode may have named children, which themselves may also
// optionally represent remote connections.
//
// This class is thread-compatible.
class LocalVnode : public fbl::RefCounted<LocalVnode> {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(LocalVnode);

  // Initializes a new vnode, and attaches a reference to it inside an
  // (optional) parent.
  static fbl::RefPtr<LocalVnode> Create(fbl::RefPtr<LocalVnode> parent, zx::channel remote,
                                        fbl::String name);

  // Recursively unlinks this Vnode's children, and detaches this node from
  // its parent.
  void Unlink();

  // Detaches this vnode from its parent. The Vnode's own children are not unlinked.
  void UnlinkFromParent();

  // Invoke |Fn()| on all children of this LocalVnode.
  // May be used as a const visitor-pattern for all children.
  //
  // Any status other than ZX_OK returned from |Fn()| will halt iteration
  // immediately and return.
  template <typename Fn>
  zx_status_t ForAllChildren(Fn fn) const {
    for (const Entry& entry : entries_by_id_) {
      zx_status_t status = fn(*entry.node());
      if (status != ZX_OK) {
        return status;
      }
    }
    return ZX_OK;
  }

  // Returns a child if it has the name |name|.
  // Otherwise, returns nullptr.
  fbl::RefPtr<LocalVnode> Lookup(const fbl::StringPiece& name) const;

  // Returns the next child vnode from the list of children, assuming that
  // |last_seen| is the ID of the last returned vnode. At the same time,
  // |last_seen| is updated to reflect the current ID.
  //
  // If the end of iteration is reached, |out_vnode| is set to nullptr.
  void Readdir(uint64_t* last_seen, fbl::RefPtr<LocalVnode>* out_vnode) const;

  // Remote is "set-once". If it is valid, this class guarantees that
  // the value of |Remote()| will not change for the lifetime of |LocalVnode|.
  const zx::channel& Remote() const { return remote_; }
  const fbl::String& Name() const { return name_; }

  bool has_children() const { return !entries_by_id_.is_empty(); }

 private:
  void AddEntry(fbl::RefPtr<LocalVnode> vn);
  void RemoveEntry(LocalVnode* vn);
  void UnlinkChildren();
  LocalVnode(fbl::RefPtr<LocalVnode> parent, zx::channel remote, fbl::String name);

  struct IdTreeTag {};
  struct NameTreeTag {};

  class Entry : public fbl::ContainableBaseClasses<
                    fbl::TaggedWAVLTreeContainable<std::unique_ptr<Entry>, IdTreeTag,
                                                   fbl::NodeOptions::AllowMultiContainerUptr>,
                    fbl::TaggedWAVLTreeContainable<Entry*, NameTreeTag>> {
   public:
    Entry(uint64_t id, fbl::RefPtr<LocalVnode> node) : id_(id), node_(node) {}
    ~Entry() = default;

    uint64_t id() const { return id_; }
    const fbl::String& name() const { return node_->name_; }
    const fbl::RefPtr<LocalVnode>& node() const { return node_; }

   private:
    uint64_t const id_;
    fbl::RefPtr<LocalVnode> node_;
  };

  struct KeyByIdTraits {
    static uint64_t GetKey(const Entry& entry) { return entry.id(); }
    static bool LessThan(uint64_t key1, uint64_t key2) { return key1 < key2; }
    static bool EqualTo(uint64_t key1, uint64_t key2) { return key1 == key2; }
  };

  struct KeyByNameTraits {
    static fbl::String GetKey(const Entry& entry) { return entry.name(); }
    static bool LessThan(const fbl::String& key1, const fbl::String& key2) { return key1 < key2; }
    static bool EqualTo(const fbl::String& key1, const fbl::String& key2) { return key1 == key2; }
  };

  using EntryByIdMap =
      fbl::TaggedWAVLTree<uint64_t, std::unique_ptr<Entry>, IdTreeTag, KeyByIdTraits>;
  using EntryByNameMap = fbl::TaggedWAVLTree<fbl::String, Entry*, NameTreeTag, KeyByNameTraits>;

  uint64_t next_node_id_ = 1;
  EntryByIdMap entries_by_id_;
  EntryByNameMap entries_by_name_;

  fbl::RefPtr<LocalVnode> parent_;
  const zx::channel remote_;
  const fbl::String name_;
};

// Invoke |func| on the (path, channel) pairs for all remotes contained within |vn|.
//
// The path supplied to |func| is the full prefix from |vn|.
zx_status_t EnumerateRemotes(const LocalVnode& vn, const EnumerateCallback& func);

}  // namespace fdio_internal

#endif  // LIB_FDIO_NAMESPACE_LOCAL_VNODE_H_
