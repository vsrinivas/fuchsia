// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <fbl/ref_ptr.h>
#include <fs/vnode.h>
#include <zx/event.h>

namespace fs {

// Connection represents an open connection to a Vnode (the server-side
// component of a file descriptor).
class Connection {
public:
    Connection(fbl::RefPtr<fs::Vnode> vn, fs::Vfs* vfs, uint32_t flags);

    ~Connection();

    fs::Vnode* vnode() const {
        return vn_.get();
    }
    fs::Vfs* vfs() const {
        return vfs_;
    }

    uint32_t flags() const {
        return flags_;
    }
    void set_flags(uint32_t flags) {
        flags_ = flags;
    }

    zx::event* token() {
        return &token_;
    }

    fs::vdircookie_t* dircookie() {
        return &dircookie_;
    }

    size_t offset() const {
        return offset_;
    }
    void set_offset(size_t offset) {
        offset_ = offset;
    }

private:
    const fbl::RefPtr<fs::Vnode> vn_;
    // The VFS state & dispatcher associated with this handle.
    fs::Vfs* vfs_;
    // Handle to event which allows client to refer to open vnodes in multi-patt
    // operations (see: link, rename). Defaults to ZX_HANDLE_INVALID.
    // Validated on the server side using cookies.
    zx::event token_{};
    fs::vdircookie_t dircookie_{};
    size_t offset_{};
    uint32_t flags_{};
};

} // namespace fs
