// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <sys/stat.h>

#include <utility>

#include <fbl/auto_lock.h>
#include <fs/pseudo_dir.h>
#include <fs/vfs_types.h>

namespace fio = ::llcpp::fuchsia::io;

namespace fs {

PseudoDir::PseudoDir() = default;

PseudoDir::~PseudoDir() {
  entries_by_name_.clear_unsafe();
  entries_by_id_.clear();
}

zx_status_t PseudoDir::Open(ValidatedOptions options, fbl::RefPtr<Vnode>* out_redirect) {
  return ZX_OK;
}

zx_status_t PseudoDir::GetAttributes(VnodeAttributes* attr) {
  *attr = VnodeAttributes();
  attr->mode = V_TYPE_DIR | V_IRUSR;
  attr->inode = fio::INO_UNKNOWN;
  attr->link_count = 1;
  return ZX_OK;
}

zx_status_t PseudoDir::Lookup(fbl::StringPiece name, fbl::RefPtr<fs::Vnode>* out) {
  fbl::AutoLock lock(&mutex_);

  auto it = entries_by_name_.find(name);
  if (it != entries_by_name_.end()) {
    *out = it->node();
    return ZX_OK;
  }

  return ZX_ERR_NOT_FOUND;
}

void PseudoDir::Notify(fbl::StringPiece name, unsigned event) { watcher_.Notify(name, event); }

zx_status_t PseudoDir::WatchDir(fs::Vfs* vfs, uint32_t mask, uint32_t options,
                                zx::channel watcher) {
  return watcher_.WatchDir(vfs, this, mask, options, std::move(watcher));
}

zx_status_t PseudoDir::Readdir(vdircookie_t* cookie, void* data, size_t len, size_t* out_actual) {
  fs::DirentFiller df(data, len);
  zx_status_t r = 0;
  if (cookie->n < kDotId) {
    uint64_t ino = fio::INO_UNKNOWN;
    if ((r = df.Next(".", VTYPE_TO_DTYPE(V_TYPE_DIR), ino)) != ZX_OK) {
      *out_actual = df.BytesFilled();
      return r;
    }
    cookie->n = kDotId;
  }

  fbl::AutoLock lock(&mutex_);

  for (auto it = entries_by_id_.lower_bound(cookie->n); it != entries_by_id_.end(); ++it) {
    if (cookie->n >= it->id()) {
      continue;
    }
    VnodeAttributes attr;
    if ((r = it->node()->GetAttributes(&attr)) != ZX_OK) {
      continue;
    }
    if (df.Next(it->name().ToStringPiece(), VTYPE_TO_DTYPE(attr.mode), attr.inode) != ZX_OK) {
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

  if (!vfs_valid_name(name.ToStringPiece())) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&mutex_);

  if (entries_by_name_.find(name) != entries_by_name_.end()) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  Notify(name.ToStringPiece(), fio::WATCH_EVENT_ADDED);
  auto entry = std::make_unique<Entry>(next_node_id_++, std::move(name), std::move(vn));
  entries_by_name_.insert(entry.get());
  entries_by_id_.insert(std::move(entry));
  return ZX_OK;
}

zx_status_t PseudoDir::RemoveEntry(fbl::StringPiece name) {
  fbl::AutoLock lock(&mutex_);

  auto it = entries_by_name_.find(name);
  if (it != entries_by_name_.end()) {
    entries_by_name_.erase(it);
    entries_by_id_.erase(it->id());
    Notify(name, fio::WATCH_EVENT_REMOVED);
    return ZX_OK;
  }

  return ZX_ERR_NOT_FOUND;
}

zx_status_t PseudoDir::RemoveEntry(fbl::StringPiece name, fs::Vnode* vn) {
  fbl::AutoLock lock(&mutex_);

  auto it = entries_by_name_.find(name);
  if (it != entries_by_name_.end() && it->node().get() == vn) {
    entries_by_name_.erase(it);
    entries_by_id_.erase(it->id());
    Notify(name, fio::WATCH_EVENT_REMOVED);
    return ZX_OK;
  }

  return ZX_ERR_NOT_FOUND;
}

void PseudoDir::RemoveAllEntries() {
  fbl::AutoLock lock(&mutex_);

  for (auto& entry : entries_by_name_) {
    Notify(entry.name().ToStringPiece(), fio::WATCH_EVENT_REMOVED);
  }
  entries_by_name_.clear();
  entries_by_id_.clear();
}

bool PseudoDir::IsEmpty() const {
  fbl::AutoLock lock(&mutex_);
  return entries_by_name_.is_empty();
}

PseudoDir::Entry::Entry(uint64_t id, fbl::String name, fbl::RefPtr<fs::Vnode> node)
    : id_(id), name_(std::move(name)), node_(std::move(node)) {}

PseudoDir::Entry::~Entry() = default;

}  // namespace fs
