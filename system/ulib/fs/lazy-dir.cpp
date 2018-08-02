// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/lazy-dir.h>

namespace fs {

namespace {
int CompareLazyDirPtrs(const void* a, const void* b) {
    auto a_id = static_cast<const LazyDir::LazyEntry*>(a)->id;
    auto b_id = static_cast<const LazyDir::LazyEntry*>(b)->id;
    if (a_id == b_id) {
        return 0;
    }
    return a_id < b_id ? -1 : 1;
}

bool DoDot(vdircookie_t* cookie) {
    if (cookie->p == 0) {
        cookie->p = (void*)1;
        return true;
    }
    return false;
}
} // namespace

LazyDir::LazyDir() {}
LazyDir::~LazyDir() = default;

zx_status_t LazyDir::Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) {
    return ZX_OK;
}

zx_status_t LazyDir::Getattr(vnattr_t* out_attr) {
    memset(out_attr, 0, sizeof(vnattr_t));
    out_attr->mode = V_TYPE_DIR | V_IRUSR;
    out_attr->nlink = 1;
    return ZX_OK;
}

zx_status_t LazyDir::Lookup(fbl::RefPtr<fs::Vnode>* out_vnode, fbl::StringPiece name) {
    LazyEntryVector entries;
    GetContents(&entries);
    for (const auto& entry : entries) {
        if (name.compare(entry.name) == 0) {
            return GetFile(out_vnode, entry.id, entry.name);
        }
    }
    return ZX_ERR_NOT_FOUND;
}

zx_status_t LazyDir::Readdir(vdircookie_t* cookie, void* dirents, size_t len, size_t* out_actual) {
    LazyEntryVector entries;
    GetContents(&entries);
    qsort(entries.get(), entries.size(), sizeof(LazyEntry), CompareLazyDirPtrs);

    fs::DirentFiller df(dirents, len);
    zx_status_t r = 0;

    if (DoDot(cookie)) {
        if ((r = df.Next(".", VTYPE_TO_DTYPE(V_TYPE_DIR))) != ZX_OK) {
            *out_actual = df.BytesFilled();
            return r;
        }
    }

    for (auto it = fbl::lower_bound(entries.begin(), entries.end(), cookie->n,
                                    [](const LazyEntry&a, uint64_t b_id) { return a.id < b_id; });
         it < entries.end();
         ++it) {
        if (cookie->n >= it->id) {
            continue;
        }
        if ((r = df.Next(it->name, VTYPE_TO_DTYPE(it->type))) != ZX_OK) {
            *out_actual = df.BytesFilled();
            return r;
        }
        cookie->n = it->id;
    }
    *out_actual = df.BytesFilled();
    return ZX_OK;
}

} // namespace fs
