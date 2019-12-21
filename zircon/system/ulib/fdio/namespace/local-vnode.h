// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>

#include <fbl/function.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/string_piece.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

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

  // Sets the remote connection of the current vnode.
  // This is only permitted if the current vnode has:
  // - No existing connection, and
  // - No children.
  zx_status_t SetRemote(zx::channel remote);

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

 private:
  void AddEntry(fbl::RefPtr<LocalVnode> vn);
  void RemoveEntry(LocalVnode* vn);
  void UnlinkChildren();
  void UnlinkFromParent();
  LocalVnode(fbl::RefPtr<LocalVnode> parent, zx::channel remote, fbl::String name);

  struct NameTreeTraits;
  struct IdTreeTraits;
  class Entry {
   public:
    Entry(uint64_t id, fbl::RefPtr<LocalVnode> node) : id_(id), node_(node) {}
    ~Entry() = default;

    uint64_t id() const { return id_; }
    const fbl::String& name() const { return node_->name_; }
    const fbl::RefPtr<LocalVnode>& node() const { return node_; }

   private:
    uint64_t const id_;
    fbl::RefPtr<LocalVnode> node_;

    // Node states.
    friend IdTreeTraits;
    friend NameTreeTraits;
    fbl::WAVLTreeNodeState<std::unique_ptr<Entry>> id_tree_state_;
    fbl::WAVLTreeNodeState<Entry*> name_tree_state_;
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

  struct IdTreeTraits {
    using PtrTraits = fbl::internal::ContainerPtrTraits<std::unique_ptr<Entry>>;
    static fbl::WAVLTreeNodeState<std::unique_ptr<Entry>>& node_state(Entry& entry) {
      return entry.id_tree_state_;
    }
  };

  struct NameTreeTraits {
    using PtrTraits = fbl::internal::ContainerPtrTraits<Entry*>;
    static fbl::WAVLTreeNodeState<Entry*>& node_state(Entry& entry) {
      return entry.name_tree_state_;
    }
  };

  using EntryByIdMap = fbl::WAVLTree<uint64_t, std::unique_ptr<Entry>, KeyByIdTraits, IdTreeTraits>;
  using EntryByNameMap = fbl::WAVLTree<fbl::String, Entry*, KeyByNameTraits, NameTreeTraits>;

  uint64_t next_node_id_ = 1;
  EntryByIdMap entries_by_id_;
  EntryByNameMap entries_by_name_;

  fbl::RefPtr<LocalVnode> parent_;
  zx::channel remote_;
  const fbl::String name_;
};

// Invoke |func| on the (path, channel) pairs for all remotes contained within |vn|.
//
// The path supplied to |func| is the full prefix from |vn|.
zx_status_t EnumerateRemotes(const LocalVnode& vn, const EnumerateCallback& func);

}  // namespace fdio_internal
