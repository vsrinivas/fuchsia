// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_NAMESPACE_LOCAL_VNODE_H_
#define LIB_FDIO_NAMESPACE_LOCAL_VNODE_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <lib/zxio/types.h>
#include <limits.h>
#include <zircon/types.h>

#include <utility>

#include <fbl/intrusive_wavl_tree.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>

namespace fdio_internal {

using EnumerateCallback = fit::function<zx_status_t(std::string_view path, zxio_t* entry)>;

// Represents a mapping from a string name to a remote connection.
//
// Each LocalVnode may have named children, which themselves may also
// optionally represent remote connections.
//
// This class is thread-compatible.
class LocalVnode : public fbl::RefCounted<LocalVnode> {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(LocalVnode);

  // Initializes a new vnode with a remote node_type, and attaches a reference to it
  // inside an (optional) parent.
  static zx::status<fbl::RefPtr<LocalVnode>> Create(fbl::RefPtr<LocalVnode> parent,
                                                    fidl::ClientEnd<fuchsia_io::Directory> remote,
                                                    fbl::String name);

  // Initializes a new vnode with a Intermediate node_type, and attaches a reference to it
  // inside an (optional) parent.
  static fbl::RefPtr<LocalVnode> Create(fbl::RefPtr<LocalVnode> parent, fbl::String name);

  // Recursively unlinks this Vnode's children, and detaches this node from
  // its parent.
  void Unlink();

  // Detaches this vnode from its parent. The Vnode's own children are not unlinked.
  void UnlinkFromParent();

  // Returns the next child vnode from the list of children, assuming that
  // |last_seen| is the ID of the last returned vnode. At the same time,
  // |last_seen| is updated to reflect the current ID.
  //
  // If the end of iteration is reached, |out_vnode| is set to nullptr.
  zx_status_t Readdir(uint64_t* last_seen, fbl::RefPtr<LocalVnode>* out_vnode) const;

  // Invoke |func| on the (path, channel) pairs for all remote nodes found in the
  // node hierarchy rooted at `this`.
  zx_status_t EnumerateRemotes(const EnumerateCallback& func) const;

  const fbl::String& Name() const { return name_; }

  struct IdTreeTag {};
  struct NameTreeTag {};

  class Entry : public fbl::ContainableBaseClasses<
                    fbl::TaggedWAVLTreeContainable<std::unique_ptr<Entry>, IdTreeTag,
                                                   fbl::NodeOptions::AllowMultiContainerUptr>,
                    fbl::TaggedWAVLTreeContainable<Entry*, NameTreeTag>> {
   public:
    Entry(uint64_t id, fbl::RefPtr<LocalVnode> node) : id_(id), node_(std::move(node)) {}
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

  class Intermediate {
   public:
    bool has_children() const { return !entries_by_id_.is_empty(); }
    void AddEntry(fbl::RefPtr<LocalVnode> vn);
    void RemoveEntry(LocalVnode* vn);
    void UnlinkEntries();

    const EntryByIdMap& GetEntriesById() const { return entries_by_id_; }

    // Returns a child if it has the name |name|.
    // Otherwise, returns nullptr.
    fbl::RefPtr<LocalVnode> Lookup(std::string_view name) const;

    // Invoke |Fn()| on all entries in this Intermediate node_type.
    // May be used as a const visitor-pattern for all entries.
    //
    // Any status other than ZX_OK returned from |Fn()| will halt iteration
    // immediately and return.
    template <typename Fn>
    zx_status_t ForAllEntries(Fn fn) const;

   private:
    uint64_t next_node_id_ = 1;
    EntryByNameMap entries_by_name_;
    EntryByIdMap entries_by_id_;
  };

  class Remote {
   public:
    explicit Remote(zxio_storage_t remote_storage) : remote_storage_(remote_storage) {}
    zxio_t* Connection() const { return const_cast<zxio_t*>(&remote_storage_.io); }

   private:
    const zxio_storage_t remote_storage_;
  };

  std::variant<Intermediate, Remote>& NodeType() { return node_type_; }

 private:
  friend class fbl::RefPtr<LocalVnode>;

  zx_status_t AddChild(fbl::RefPtr<LocalVnode> child);
  zx_status_t RemoveChild(LocalVnode* child);

  zx_status_t EnumerateInternal(fbl::StringBuffer<PATH_MAX>* path,
                                const EnumerateCallback& func) const;

  LocalVnode(fbl::RefPtr<LocalVnode> parent, std::variant<Intermediate, Remote> node_type,
             fbl::String name);
  ~LocalVnode();

  std::variant<Intermediate, Remote> node_type_;
  fbl::RefPtr<LocalVnode> parent_;
  const fbl::String name_;
};

}  // namespace fdio_internal

#endif  // LIB_FDIO_NAMESPACE_LOCAL_VNODE_H_
