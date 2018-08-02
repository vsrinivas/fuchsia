// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "vnode.h"
#include <fbl/function.h>
#include <fbl/ref_counted.h>
#include <fbl/string.h>
#include <fbl/vector.h>

namespace fs {

// A |LazyDir| a base class for directories that dynamically update their
// contents on each operation.  Clients should derive from this class
// and implement GetContents and GetFile for their use case.  The base
// implementation of this class is thread-safe, but it is up to implementers
// to ensure their implementations are thread safe as well.
class LazyDir : public Vnode {
public:
    // Structure storing a single entry in the directory.
    struct LazyEntry {
        // Non-zero ID for this entry, must remain stable across calls.
        // IDs do not necessarily need to be unique, however, non-unique
        // IDs may cause duplication in directory listings.
        uint64_t id;
        fbl::String name;
        uint32_t type;
    };
    using LazyEntryVector = fbl::Vector<LazyEntry>;

    LazyDir();
    virtual ~LazyDir();

    // |Vnode| implementation.
    zx_status_t Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) final;
    zx_status_t Getattr(vnattr_t* out_attr) final;
    // Read the directory contents. Note that cookie->p is used to denote
    // if the "." entry has been returned. All IDs other than 0 are valid.
    zx_status_t Readdir(vdircookie_t* cookie, void* dirents, size_t len, size_t* out_actual) final;
    zx_status_t Lookup(fbl::RefPtr<fs::Vnode>* out_vnode, fbl::StringPiece name) final;

protected:
    // Get the contents of the directory in an output vector.
    virtual void GetContents(LazyEntryVector* out_vector) = 0;
    // Get the reference to a single file. The id and name of the entry as
    // returned from GetContents are passed in to assist locating the file.
    virtual zx_status_t GetFile(fbl::RefPtr<Vnode>* out_vnode, uint64_t id, fbl::String name) = 0;
};

} // namespace fs
