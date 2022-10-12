// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "local-vnode.h"

#include <lib/zx/channel.h>
#include <lib/zxio/cpp/create_with_type.h>
#include <lib/zxio/zxio.h>
#include <zircon/types.h>

#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>

#include "sdk/lib/fdio/internal.h"

namespace fdio_internal {

zx::status<fbl::RefPtr<LocalVnode>> LocalVnode::Create(
    fbl::RefPtr<LocalVnode> parent, fidl::ClientEnd<fuchsia_io::Directory> remote,
    fbl::String name) {
  if (!remote.is_valid()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  zxio_storage_t remote_storage;
  zxio::CreateDirectory(const_cast<zxio_storage_t*>(&remote_storage), std::move(remote));

  Remote storage(remote_storage);

  fbl::RefPtr vn =
      fbl::AdoptRef(new LocalVnode(std::move(parent), std::move(storage), std::move(name)));

  if (vn->parent_ != nullptr) {
    vn->parent_->AddChild(vn);
  }
  return zx::ok(vn);
}

fbl::RefPtr<LocalVnode> LocalVnode::Create(fbl::RefPtr<LocalVnode> parent, fbl::String name) {
  Intermediate children;

  fbl::RefPtr vn =
      fbl::AdoptRef(new LocalVnode(std::move(parent), std::move(children), std::move(name)));

  if (vn->parent_ != nullptr) {
    vn->parent_->AddChild(vn);
  }

  return vn;
}

zx_status_t LocalVnode::AddChild(fbl::RefPtr<LocalVnode> child) {
  return std::visit(fdio::overloaded{
                        [&child](LocalVnode::Intermediate& c) {
                          c.AddEntry(child);
                          return ZX_OK;
                        },
                        [](LocalVnode::Remote& s) {
                          // Calling AddChild on a Storage node is invalid, and implies
                          // a poorly formed path.
                          return ZX_ERR_BAD_PATH;
                        },
                    },
                    node_type_);
}

void LocalVnode::Intermediate::AddEntry(fbl::RefPtr<LocalVnode> vn) {
  // |fdio_namespace| already checked that the entry does not exist.
  ZX_DEBUG_ASSERT(entries_by_name_.find(vn->Name()) == entries_by_name_.end());

  auto entry = std::make_unique<Entry>(next_node_id_, std::move(vn));
  entries_by_name_.insert(entry.get());
  entries_by_id_.insert(std::move(entry));
  next_node_id_++;
}

zx_status_t LocalVnode::RemoveChild(LocalVnode* child) {
  return std::visit(fdio::overloaded{
                        [&child](LocalVnode::Intermediate& c) {
                          c.RemoveEntry(child);
                          return ZX_OK;
                        },
                        [](LocalVnode::Remote& s) {
                          // Calling RemoveChild on a Storage node is invalid, and implies
                          // a poorly formed path.
                          return ZX_ERR_BAD_PATH;
                        },
                    },
                    node_type_);
}

void LocalVnode::Intermediate::RemoveEntry(LocalVnode* vn) {
  auto it = entries_by_name_.find(vn->Name());
  if (it != entries_by_name_.end() && it->node().get() == vn) {
    auto id = it->id();
    entries_by_name_.erase(it);
    entries_by_id_.erase(id);
  }
}

void LocalVnode::Unlink() {
  std::visit(fdio::overloaded{
                 [](LocalVnode::Intermediate& c) { c.UnlinkEntries(); },
                 [](LocalVnode::Remote& s) {},
             },
             node_type_);
  UnlinkFromParent();
}

fbl::RefPtr<LocalVnode> LocalVnode::Intermediate::Lookup(std::string_view name) const {
  auto it = entries_by_name_.find(fbl::String{name});
  if (it != entries_by_name_.end()) {
    return it->node();
  }
  return nullptr;
}

LocalVnode::LocalVnode(fbl::RefPtr<LocalVnode> parent, std::variant<Intermediate, Remote> node_type,
                       fbl::String name)
    : node_type_(std::move(node_type)), parent_(std::move(parent)), name_(std::move(name)) {}

LocalVnode::~LocalVnode() {
  std::visit(fdio::overloaded{
                 [](LocalVnode::Intermediate& c) {},
                 [](LocalVnode::Remote& s) {
                   // Close the channel underlying the remote connection without making a Close
                   // call to preserve previous behavior.
                   zx::channel remote_channel;
                   zxio_release(s.Connection(), remote_channel.reset_and_get_address());
                 },
             },
             node_type_);
}

void LocalVnode::Intermediate::UnlinkEntries() {
  for (auto& entry : entries_by_name_) {
    std::visit(fdio::overloaded{
                   [](LocalVnode::Intermediate& c) { c.UnlinkEntries(); },
                   [](LocalVnode::Remote& s) {},
               },
               entry.node()->node_type_);
    entry.node()->parent_ = nullptr;
  }
  entries_by_name_.clear();
  entries_by_id_.clear();
}

void LocalVnode::UnlinkFromParent() {
  if (parent_) {
    parent_->RemoveChild(this);
  }
  parent_ = nullptr;
}

zx_status_t LocalVnode::EnumerateInternal(fbl::StringBuffer<PATH_MAX>* path,
                                          const EnumerateCallback& func) const {
  const size_t original_length = path->length();

  // Add this current node to the path, and enumerate it if it has a remote
  // object.
  path->Append(Name().data(), Name().length());

  std::visit(fdio::overloaded{
                 [&path, &func, this](const LocalVnode::Intermediate& c) {
                   // If we added a node with children, add a separator and enumerate all the
                   // children.
                   if (Name().length() > 0) {
                     path->Append('/');
                   }

                   c.ForAllEntries([&path, &func](const LocalVnode& child) {
                     return child.EnumerateInternal(path, func);
                   });
                 },
                 [&path, &func](const LocalVnode::Remote& s) {
                   // If we added a remote node, call the enumeration function on the remote node.
                   func(std::string_view(path->data(), path->length()), s.Connection());
                 },
             },
             node_type_);

  // To re-use the same prefix buffer, restore the original buffer length
  // after enumeration has completed.
  path->Resize(original_length);
  return ZX_OK;
}

zx_status_t LocalVnode::EnumerateRemotes(const EnumerateCallback& func) const {
  fbl::StringBuffer<PATH_MAX> path;
  path.Append('/');
  return EnumerateInternal(&path, func);
}

zx_status_t LocalVnode::Readdir(uint64_t* last_seen, fbl::RefPtr<LocalVnode>* out_vnode) const {
  return std::visit(fdio::overloaded{
                        [&](const LocalVnode::Intermediate& c) {
                          for (auto it = c.GetEntriesById().lower_bound(*last_seen);
                               it != c.GetEntriesById().end(); ++it) {
                            if (it->id() <= *last_seen) {
                              continue;
                            }
                            *last_seen = it->id();
                            *out_vnode = it->node();
                            return ZX_OK;
                          }
                          *out_vnode = nullptr;
                          return ZX_OK;
                        },
                        [](const LocalVnode::Remote& s) {
                          // If we've called Readdir on a Remote node, the path
                          // was misconfigured.
                          return ZX_ERR_BAD_PATH;
                        },
                    },
                    node_type_);
}

template <typename Fn>
zx_status_t LocalVnode::Intermediate::ForAllEntries(Fn fn) const {
  for (const Entry& entry : entries_by_id_) {
    zx_status_t status = fn(*entry.node());
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

}  // namespace fdio_internal
