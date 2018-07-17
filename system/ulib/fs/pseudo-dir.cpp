// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/pseudo-dir.h>

#include <sys/stat.h>

#include <fbl/auto_lock.h>

namespace fs {

PseudoDir::PseudoDir() = default;

PseudoDir::~PseudoDir() {
  entries_by_name_.clear_unsafe();
  entries_by_id_.clear();
}

zx_status_t PseudoDir::Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) {
    return ZX_OK;
}

zx_status_t PseudoDir::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_DIR | V_IRUSR;
    attr->nlink = 1;
    return ZX_OK;
}

zx_status_t PseudoDir::Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) {
    fbl::AutoLock lock(&mutex_);

    auto it = entries_by_name_.find(name);
    if (it != entries_by_name_.end()) {
        *out = it->node();
        return ZX_OK;
    }

    return ZX_ERR_NOT_FOUND;
}

void PseudoDir::Notify(fbl::StringPiece name, unsigned event) {
    watcher_.Notify(name, event);
}

zx_status_t PseudoDir::WatchDir(Vfs* vfs, const vfs_watch_dir_t* cmd) {
    return watcher_.WatchDir(vfs, this, cmd);
}

zx_status_t PseudoDir::Readdir(vdircookie_t* cookie, void* data, size_t len, size_t* out_actual) {
    fs::DirentFiller df(data, len);
    zx_status_t r = 0;
    if (cookie->n < kDotId) {
        if ((r = df.Next(".", VTYPE_TO_DTYPE(V_TYPE_DIR))) != ZX_OK) {
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
        vnattr_t attr;
        if ((r = it->node()->Getattr(&attr)) != ZX_OK) {
            continue;
        }
        if ((r = df.Next(it->name().ToStringPiece(),
                         VTYPE_TO_DTYPE(attr.mode))) != ZX_OK) {
            *out_actual = df.BytesFilled();
            return r;
        }
        cookie->n = it->id();
    }

    *out_actual = df.BytesFilled();
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

    Notify(name.ToStringPiece(), VFS_WATCH_EVT_ADDED);
    auto entry = fbl::make_unique<Entry>(next_node_id_++,
                                                fbl::move(name), fbl::move(vn));
    entries_by_name_.insert(entry.get());
    entries_by_id_.insert(fbl::move(entry));
    return ZX_OK;
}

zx_status_t PseudoDir::RemoveEntry(fbl::StringPiece name) {
    fbl::AutoLock lock(&mutex_);

    auto it = entries_by_name_.find(name);
    if (it != entries_by_name_.end()) {
        entries_by_name_.erase(it);
        entries_by_id_.erase(it->id());
        Notify(name, VFS_WATCH_EVT_REMOVED);
        return ZX_OK;
    }

    return ZX_ERR_NOT_FOUND;
}

void PseudoDir::RemoveAllEntries() {
    fbl::AutoLock lock(&mutex_);

    for (auto& entry : entries_by_name_) {
        Notify(entry.name().ToStringPiece(), VFS_WATCH_EVT_REMOVED);
    }
    entries_by_name_.clear();
    entries_by_id_.clear();
}

bool PseudoDir::IsEmpty() const {
    fbl::AutoLock lock(&mutex_);
    return entries_by_name_.is_empty();
}

PseudoDir::Entry::Entry(uint64_t id, fbl::String name, fbl::RefPtr<fs::Vnode> node)
    : id_(id), name_(fbl::move(name)), node_(fbl::move(node)) {
}

PseudoDir::Entry::~Entry() = default;

} // namespace fs
