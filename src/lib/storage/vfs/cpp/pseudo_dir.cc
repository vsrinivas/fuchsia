// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <sys/stat.h>

#include <string_view>
#include <utility>

#include <fbl/auto_lock.h>

#include "src/lib/storage/vfs/cpp/vfs_types.h"

namespace fio = fuchsia_io;

namespace fs {

PseudoDir::PseudoDir(bool has_dot_entry) : has_dot_entry_(has_dot_entry) {}

PseudoDir::~PseudoDir() {
  entries_by_name_.clear_unsafe();
  entries_by_id_.clear();
}

zx_status_t PseudoDir::GetAttributes(VnodeAttributes* attr) {
  *attr = VnodeAttributes();
  attr->mode = V_TYPE_DIR | V_IRUSR;
  attr->inode = fio::wire::kInoUnknown;
  attr->link_count = 1;
  return ZX_OK;
}

zx_status_t PseudoDir::Lookup(std::string_view name, fbl::RefPtr<fs::Vnode>* out) {
  std::lock_guard lock(mutex_);

  auto it = entries_by_name_.find(name);
  if (it != entries_by_name_.end()) {
    *out = it->node();
    return ZX_OK;
  }

  return ZX_ERR_NOT_FOUND;
}

void PseudoDir::Notify(std::string_view name, fio::wire::WatchEvent event) {
  watcher_.Notify(name, event);
}

zx_status_t PseudoDir::WatchDir(fs::Vfs* vfs, fio::wire::WatchMask mask, uint32_t options,
                                fidl::ServerEnd<fuchsia_io::DirectoryWatcher> watcher) {
  return watcher_.WatchDir(vfs, this, mask, options, std::move(watcher));
}

zx_status_t PseudoDir::Readdir(VdirCookie* cookie, void* data, size_t len, size_t* out_actual) {
  fs::DirentFiller df(data, len);
  zx_status_t r = 0;
  if (has_dot_entry_ && cookie->n < kDotId) {
    uint64_t ino = fio::wire::kInoUnknown;
    if ((r = df.Next(".", VTYPE_TO_DTYPE(V_TYPE_DIR), ino)) != ZX_OK) {
      *out_actual = df.BytesFilled();
      return r;
    }
    cookie->n = kDotId;
  }

  std::lock_guard lock(mutex_);

  for (auto it = entries_by_id_.lower_bound(cookie->n); it != entries_by_id_.end(); ++it) {
    if (cookie->n >= it->id()) {
      continue;
    }
    VnodeAttributes attr;
    if ((r = it->node()->GetAttributes(&attr)) != ZX_OK) {
      continue;
    }
    if (df.Next(it->name(), VTYPE_TO_DTYPE(attr.mode), attr.inode) != ZX_OK) {
      *out_actual = df.BytesFilled();
      return ZX_OK;
    }
    cookie->n = it->id();
  }

  *out_actual = df.BytesFilled();
  return ZX_OK;
}

VnodeProtocolSet PseudoDir::GetProtocols() const { return VnodeProtocol::kDirectory; }

zx_status_t PseudoDir::GetNodeInfoForProtocol([[maybe_unused]] VnodeProtocol protocol,
                                              [[maybe_unused]] Rights rights,
                                              VnodeRepresentation* info) {
  *info = VnodeRepresentation::Directory();
  return ZX_OK;
}

zx_status_t PseudoDir::AddEntry(fbl::String name, fbl::RefPtr<fs::Vnode> vn) {
  ZX_DEBUG_ASSERT(vn);

  if (!IsValidName(name)) {
    return ZX_ERR_INVALID_ARGS;
  }

  std::lock_guard lock(mutex_);

  if (entries_by_name_.find(name) != entries_by_name_.end()) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  Notify(name, fio::wire::WatchEvent::kAdded);
  auto entry = std::make_unique<Entry>(next_node_id_++, std::move(name), std::move(vn));
  entries_by_name_.insert(entry.get());
  entries_by_id_.insert(std::move(entry));
  return ZX_OK;
}

zx_status_t PseudoDir::RemoveEntry(std::string_view name) {
  std::lock_guard lock(mutex_);

  auto it = entries_by_name_.find(name);
  if (it != entries_by_name_.end()) {
    entries_by_name_.erase(it);
    entries_by_id_.erase(it->id());
    Notify(name, fio::wire::WatchEvent::kRemoved);
    return ZX_OK;
  }

  return ZX_ERR_NOT_FOUND;
}

zx_status_t PseudoDir::RemoveEntry(std::string_view name, fs::Vnode* vn) {
  std::lock_guard lock(mutex_);

  auto it = entries_by_name_.find(name);
  if (it != entries_by_name_.end() && it->node().get() == vn) {
    entries_by_name_.erase(it);
    entries_by_id_.erase(it->id());
    Notify(name, fio::wire::WatchEvent::kRemoved);
    return ZX_OK;
  }

  return ZX_ERR_NOT_FOUND;
}

void PseudoDir::RemoveAllEntries() {
  std::lock_guard lock(mutex_);

  for (auto& entry : entries_by_name_) {
    Notify(entry.name(), fio::wire::WatchEvent::kRemoved);
  }
  entries_by_name_.clear();
  entries_by_id_.clear();
}

bool PseudoDir::IsEmpty() const {
  SharedLock lock(mutex_);
  return entries_by_name_.is_empty();
}

PseudoDir::Entry::Entry(uint64_t id, fbl::String name, fbl::RefPtr<fs::Vnode> node)
    : id_(id), name_(std::move(name)), node_(std::move(node)) {}

PseudoDir::Entry::~Entry() = default;

}  // namespace fs
