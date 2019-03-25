// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <fs/tracked-remote-dir.h>
#include <fuchsia/fshost/c/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <lib/memfs/cpp/vnode.h>

#include "vnode.h"

namespace devmgr {
namespace fshost {
namespace {

// A connection bespoke to the fshost Vnode, capable of serving fshost FIDL
// requests.
class Connection final : public fs::Connection {
public:
    Connection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode, zx::channel channel,
                     uint32_t flags);
private:
    static const fuchsia_fshost_Registry_ops* Ops() {
        using Binder = fidl::Binder<Connection>;
        static const fuchsia_fshost_Registry_ops kFshostOps = {
            .RegisterFilesystem = Binder::BindMember<&Connection::RegisterFilesystem>,
        };
        return &kFshostOps;
    }

    Vnode& GetVnode() const;
    zx_status_t RegisterFilesystem(zx_handle_t channel, fidl_txn_t* txn);

    zx_status_t HandleFsSpecificMessage(fidl_msg_t* msg, fidl_txn_t* txn) final;
};

} // namespace

Vnode::Vnode(async_dispatcher_t* dispatcher, fbl::RefPtr<fs::PseudoDir> filesystems)
    : filesystems_(std::move(filesystems)),
      filesystem_counter_(0),
      dispatcher_(dispatcher) {
}

zx_status_t Vnode::AddFilesystem(zx::channel directory) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%" PRIu64 "", filesystem_counter_++);

    auto directory_vnode = fbl::AdoptRef<fs::TrackedRemoteDir>(
            new fs::TrackedRemoteDir(std::move(directory)));

    return directory_vnode->AddAsTrackedEntry(dispatcher_, filesystems_.get(), fbl::String(buf));
}

zx_status_t Vnode::ValidateFlags(uint32_t flags) {
    if (flags & ZX_FS_FLAG_DIRECTORY) {
        return ZX_ERR_NOT_DIR;
    }
    return ZX_OK;
}

zx_status_t Vnode::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_FILE;
    attr->nlink = 1;
    return ZX_OK;
}

zx_status_t Vnode::Serve(fs::Vfs* vfs, zx::channel channel, uint32_t flags) {
    return vfs->ServeConnection(std::make_unique<Connection>(
        vfs, fbl::WrapRefPtr(this), std::move(channel), flags));
}

zx_status_t Vnode::GetNodeInfo(uint32_t flags, fuchsia_io_NodeInfo* info) {
    info->tag = fuchsia_io_NodeInfoTag_service;
    return ZX_OK;
}

Connection::Connection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode,
                       zx::channel channel, uint32_t flags)
    : fs::Connection(vfs, std::move(vnode), std::move(channel), flags) {}

Vnode& Connection::GetVnode() const {
    return reinterpret_cast<Vnode&>(fs::Connection::GetVnode());
}

zx_status_t Connection::RegisterFilesystem(zx_handle_t channel, fidl_txn_t* txn) {
    zx::channel public_export(channel);
    zx_status_t status = GetVnode().AddFilesystem(std::move(public_export));
    return fuchsia_fshost_RegistryRegisterFilesystem_reply(txn, status);
}

zx_status_t Connection::HandleFsSpecificMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    fidl_message_header_t* header = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
    if (header->ordinal == fuchsia_fshost_RegistryRegisterFilesystemOrdinal) {
        return fuchsia_fshost_Registry_dispatch(this, txn, msg, Ops());
    }
    zx_handle_close_many(msg->handles, msg->num_handles);
    return ZX_ERR_NOT_SUPPORTED;
}

} // namespace fshost
} // namespace devmgr
