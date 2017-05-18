// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fs/dispatcher.h>
#include <fs/vfs.h>
#include <magenta/types.h>
#include <mxtl/array.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/string_piece.h>

namespace vmofs {

class Vnode : public fs::Vnode {
public:
    mx_status_t Close() final;
    fs::Dispatcher* GetDispatcher() final;

    ~Vnode() override;

    virtual uint32_t GetVType() = 0;

protected:
    explicit Vnode(fs::Dispatcher* dispatcher);

    fs::Dispatcher* dispatcher_;
};

class VnodeFile : public Vnode {
public:
    // The creator retains ownership of |vmo|.
    VnodeFile(fs::Dispatcher* dispatcher,
              mx_handle_t vmo,
              mx_off_t offset,
              mx_off_t length);
    ~VnodeFile() override;

    mx_status_t Open(uint32_t flags) final;
    mx_status_t Serve(mx_handle_t h, uint32_t flags) final;
    ssize_t Read(void* data, size_t len, size_t off) final;
    mx_status_t Getattr(vnattr_t* a) final;
    mx_status_t GetHandles(uint32_t flags, mx_handle_t* hnds,
                           uint32_t* type, void* extra, uint32_t* esize) final;

    uint32_t GetVType() final;

private:
    mx_handle_t vmo_;
    mx_off_t offset_;
    mx_off_t length_;
};

class VnodeDir : public Vnode {
public:
    // |names| must be sorted in ascending order and must have the same length
    // as |children|.
    VnodeDir(fs::Dispatcher* dispatcher,
             mxtl::Array<mxtl::StringPiece> names,
             mxtl::Array<mxtl::RefPtr<Vnode>> children);
    ~VnodeDir() override;

    mx_status_t Open(uint32_t flags) final;
    mx_status_t Lookup(mxtl::RefPtr<fs::Vnode>* out, const char* name, size_t len) final;
    mx_status_t Getattr(vnattr_t* a) final;
    mx_status_t Readdir(void* cookie, void* dirents, size_t len) final;

    uint32_t GetVType() final;

private:
    mxtl::Array<mxtl::StringPiece> names_;
    mxtl::Array<mxtl::RefPtr<Vnode>> children_;
};

} // namespace vmofs
