// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/array.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_piece.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <zircon/types.h>

namespace vmofs {

class Vnode : public fs::Vnode {
public:
    zx_status_t Close() final;

    Vnode();
    ~Vnode() override;

    virtual uint32_t GetVType() = 0;
};

class VnodeFile : public Vnode {
public:
    // The creator retains ownership of |vmo|.
    VnodeFile(zx_handle_t vmo,
              zx_off_t offset,
              zx_off_t length);
    ~VnodeFile() override;

    zx_status_t ValidateFlags(uint32_t flags) final;
    zx_status_t Serve(fs::Vfs* vfs, zx::channel channel, uint32_t flags) final;
    zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final;
    zx_status_t Getattr(vnattr_t* a) final;
    zx_status_t GetHandles(uint32_t flags, zx_handle_t* hnd, uint32_t* type,
                           zxrio_object_info_t* extra) final;

    uint32_t GetVType() final;

private:
    zx_handle_t vmo_;
    zx_off_t offset_;
    zx_off_t length_;
    bool have_local_clone_;
};

class VnodeDir : public Vnode {
public:
    // |names| must be sorted in ascending order and must have the same length
    // as |children|.
    VnodeDir(fbl::Array<fbl::StringPiece> names,
             fbl::Array<fbl::RefPtr<Vnode>> children);
    ~VnodeDir() override;

    zx_status_t ValidateFlags(uint32_t flags) final;
    zx_status_t Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) final;
    zx_status_t Getattr(vnattr_t* a) final;
    zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len,
                        size_t* out_actual) final;

    uint32_t GetVType() final;

private:
    fbl::Array<fbl::StringPiece> names_;
    fbl::Array<fbl::RefPtr<Vnode>> children_;
};

} // namespace vmofs
