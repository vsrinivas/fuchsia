// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/vnode.h>

#ifdef __Fuchsia__
#include <fs/connection.h>

#include <utility>
#endif

namespace fs {

Vnode::Vnode() = default;

Vnode::~Vnode() = default;

#ifdef __Fuchsia__
zx_status_t Vnode::Serve(fs::Vfs* vfs, zx::channel channel, uint32_t flags) {
    return vfs->ServeConnection(fbl::make_unique<Connection>(
        vfs, fbl::WrapRefPtr(this), std::move(channel), flags));
}

zx_status_t Vnode::WatchDir(Vfs* vfs, uint32_t mask, uint32_t options, zx::channel watcher) {
    return ZX_ERR_NOT_SUPPORTED;
}
#endif

void Vnode::Notify(fbl::StringPiece name, unsigned event) {}

zx_status_t Vnode::ValidateFlags(uint32_t flags) {
    return ZX_OK;
}

zx_status_t Vnode::Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) {
    return ZX_OK;
}

zx_status_t Vnode::Close() {
    return ZX_OK;
}

zx_status_t Vnode::Read(void* data, size_t len, size_t off, size_t* out_actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Write(const void* data, size_t len, size_t offset, size_t* out_actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Lookup(fbl::RefPtr<Vnode>* out, fbl::StringPiece name) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Getattr(vnattr_t* a) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Setattr(const vnattr_t* a) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Readdir(vdircookie_t* cookie, void* dirents, size_t len, size_t* out_actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Create(fbl::RefPtr<Vnode>* out, fbl::StringPiece name, uint32_t mode) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Unlink(fbl::StringPiece name, bool must_be_dir) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Truncate(size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Rename(fbl::RefPtr<Vnode> newdir, fbl::StringPiece oldname,
                          fbl::StringPiece newname, bool src_must_be_dir,
                          bool dst_must_be_dir) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::Link(fbl::StringPiece name, fbl::RefPtr<Vnode> target) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::GetVmo(int flags, zx_handle_t* out_vmo, size_t* out_size) {
    return ZX_ERR_NOT_SUPPORTED;
}

void Vnode::Sync(SyncCallback closure) {
    closure(ZX_ERR_NOT_SUPPORTED);
}

#ifdef __Fuchsia__

zx_status_t Vnode::QueryFilesystem(fuchsia_io_FilesystemInfo* out) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Vnode::AttachRemote(MountChannel h) {
    return ZX_ERR_NOT_SUPPORTED;
}

bool Vnode::IsRemote() const {
    return false;
}

zx::channel Vnode::DetachRemote() {
    return zx::channel();
}

zx_handle_t Vnode::GetRemote() const {
    return ZX_HANDLE_INVALID;
}

void Vnode::SetRemote(zx::channel remote) {
    ZX_DEBUG_ASSERT(false);
}
#endif

DirentFiller::DirentFiller(void* ptr, size_t len)
    : ptr_(static_cast<char*>(ptr)), pos_(0), len_(len) {}

zx_status_t DirentFiller::Next(fbl::StringPiece name, uint8_t type, uint64_t ino) {
    vdirent_t* de = reinterpret_cast<vdirent_t*>(ptr_ + pos_);
    size_t sz = sizeof(vdirent_t) + name.length();

    if (sz > len_ - pos_ || name.length() > NAME_MAX) {
        return ZX_ERR_INVALID_ARGS;
    }
    de->ino = ino;
    de->size = static_cast<uint8_t>(name.length());
    de->type = type;
    memcpy(de->name, name.data(), name.length());
    pos_ += sz;
    return ZX_OK;
}

} // namespace fs
