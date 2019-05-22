// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <blobfs/blobfs.h>
#include <blobfs/latency-event.h>
#include <cobalt-client/cpp/timer.h>
#include <digest/digest.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_piece.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/vfs.h>
#include <lib/fidl-utils/bind.h>
#include <lib/sync/completion.h>
#include <zircon/device/vfs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <utility>

namespace blobfs {

Directory::Directory(Blobfs* bs)
    : blobfs_(bs) {}

BlobCache& Directory::Cache() {
    return blobfs_->Cache();
}

Directory::~Directory() = default;

zx_status_t Directory::GetNodeInfo(uint32_t flags, fuchsia_io_NodeInfo* info) {
    info->tag = fuchsia_io_NodeInfoTag_directory;
    return ZX_OK;
}

zx_status_t Directory::ValidateFlags(uint32_t flags) {
    if (flags & ZX_FS_FLAG_NOT_DIRECTORY) {
        return ZX_ERR_NOT_FILE;
    }
    return ZX_OK;
}

zx_status_t Directory::Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len,
                               size_t* out_actual) {
    return blobfs_->Readdir(cookie, dirents, len, out_actual);
}

zx_status_t Directory::Read(void* data, size_t len, size_t off, size_t* out_actual) {
    return ZX_ERR_NOT_FILE;
}

zx_status_t Directory::Write(const void* data, size_t len, size_t offset, size_t* out_actual) {
    return ZX_ERR_NOT_FILE;
}

zx_status_t Directory::Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) {
    return ZX_ERR_NOT_FILE;
}

zx_status_t Directory::Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) {
    TRACE_DURATION("blobfs", "Directory::Lookup", "name", name);
    LatencyEvent event(&blobfs_->GetMutableVnodeMetrics()->look_up, blobfs_->CollectingMetrics());
    assert(memchr(name.data(), '/', name.length()) == nullptr);
    if (name == ".") {
        // Special case: Accessing root directory via '.'
        *out = fbl::RefPtr<Directory>(this);
        return ZX_OK;
    }

    zx_status_t status;
    Digest digest;
    if ((status = digest.Parse(name.data(), name.length())) != ZX_OK) {
        return status;
    }
    fbl::RefPtr<CacheNode> cache_node;
    if ((status = Cache().Lookup(digest, &cache_node)) != ZX_OK) {
        return status;
    }
    auto vnode = fbl::RefPtr<Blob>::Downcast(std::move(cache_node));
    blobfs_->LocalMetrics().UpdateLookup(vnode->SizeData());
    *out = std::move(vnode);
    return ZX_OK;
}

zx_status_t Directory::Getattr(vnattr_t* a) {
    memset(a, 0, sizeof(vnattr_t));
    a->mode = V_TYPE_DIR | V_IRUSR;
    a->inode = fuchsia_io_INO_UNKNOWN;
    a->size = 0;
    a->blksize = kBlobfsBlockSize;
    a->blkcount = 0;
    a->nlink = 1;
    a->create_time = 0;
    a->modify_time = 0;
    return ZX_OK;
}

zx_status_t Directory::Create(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name, uint32_t mode) {
    TRACE_DURATION("blobfs", "Directory::Create", "name", name, "mode", mode);
    LatencyEvent event(&blobfs_->GetMutableVnodeMetrics()->create, blobfs_->CollectingMetrics());
    assert(memchr(name.data(), '/', name.length()) == nullptr);

    Digest digest;
    zx_status_t status;
    if ((status = digest.Parse(name.data(), name.length())) != ZX_OK) {
        return status;
    }

    fbl::RefPtr<Blob> vn = fbl::AdoptRef(new Blob(blobfs_, std::move(digest)));
    if ((status = Cache().Add(vn)) != ZX_OK) {
        return status;
    }
    vn->Open(0, nullptr);
    *out = std::move(vn);
    return ZX_OK;
}

#ifdef __Fuchsia__

constexpr const char kFsName[] = "blobfs";

zx_status_t Directory::QueryFilesystem(fuchsia_io_FilesystemInfo* info) {
    static_assert(fbl::constexpr_strlen(kFsName) + 1 < fuchsia_io_MAX_FS_NAME_BUFFER,
                  "Blobfs name too long");

    memset(info, 0, sizeof(*info));
    info->block_size = kBlobfsBlockSize;
    info->max_filename_size = Digest::kLength * 2;
    info->fs_type = VFS_TYPE_BLOBFS;
    info->fs_id = blobfs_->GetFsId();
    info->total_bytes = blobfs_->Info().data_block_count * blobfs_->Info().block_size;
    info->used_bytes = blobfs_->Info().alloc_block_count * blobfs_->Info().block_size;
    info->total_nodes = blobfs_->Info().inode_count;
    info->used_nodes = blobfs_->Info().alloc_inode_count;
    strlcpy(reinterpret_cast<char*>(info->name), kFsName, fuchsia_io_MAX_FS_NAME_BUFFER);
    return ZX_OK;
}

zx_status_t Directory::GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) {
    return blobfs_->Device()->GetDevicePath(buffer_len, out_name, out_len);
}
#endif

zx_status_t Directory::Unlink(fbl::StringPiece name, bool must_be_dir) {
    TRACE_DURATION("blobfs", "Directory::Unlink", "name", name, "must_be_dir", must_be_dir);
    LatencyEvent event(&blobfs_->GetMutableVnodeMetrics()->unlink, blobfs_->CollectingMetrics());
    assert(memchr(name.data(), '/', name.length()) == nullptr);

    zx_status_t status;
    Digest digest;
    if ((status = digest.Parse(name.data(), name.length())) != ZX_OK) {
        return status;
    }
    fbl::RefPtr<CacheNode> cache_node;
    if ((status = Cache().Lookup(digest, &cache_node)) != ZX_OK) {
        return status;
    }
    auto vnode = fbl::RefPtr<Blob>::Downcast(std::move(cache_node));
    blobfs_->LocalMetrics().UpdateLookup(vnode->SizeData());
    return vnode->QueueUnlink();
}

void Directory::Sync(SyncCallback closure) {
    blobfs_->Sync([this, cb = std::move(closure)](zx_status_t status) {
        if (status != ZX_OK) {
            cb(status);
            return;
        }

        fs::WriteTxn sync_txn(blobfs_);
        sync_txn.EnqueueFlush();
        status = sync_txn.Transact();
        cb(status);
    });
}

// DirectoryConnection overrides the base Connection class to allow Directory to
// dispatch its own ordinals.
class DirectoryConnection : public fs::Connection {
public:
    using DirectoryConnectionBinder = fidl::Binder<DirectoryConnection>;

    DirectoryConnection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode, zx::channel channel,
                        uint32_t flags)
            : Connection(vfs, std::move(vnode), std::move(channel), flags) {}

private:
    Directory& GetDirectory() const {
        return reinterpret_cast<Directory&>(GetVnode());
    }

    zx_status_t GetAllocatedRegions(fidl_txn_t* txn) {
        return GetDirectory().GetAllocatedRegions(txn);
    }

    static const fuchsia_blobfs_Blobfs_ops* Ops() {
        static const fuchsia_blobfs_Blobfs_ops kBlobfsOps = {
            .GetAllocatedRegions =
                DirectoryConnectionBinder::BindMember<&DirectoryConnection::GetAllocatedRegions>,
        };
        return &kBlobfsOps;
    }

    zx_status_t HandleFsSpecificMessage(fidl_msg_t* msg, fidl_txn_t* txn) final {
        return fuchsia_blobfs_Blobfs_dispatch(this, txn, msg, Ops());
    }
};

#ifdef __Fuchsia__
zx_status_t Directory::Serve(fs::Vfs* vfs, zx::channel channel, uint32_t flags) {
    return vfs->ServeConnection(std::make_unique<DirectoryConnection>(
        vfs, fbl::WrapRefPtr(this), std::move(channel), flags));
}
#endif

zx_status_t Directory::GetAllocatedRegions(fidl_txn_t* txn) const {
    zx::vmo vmo;
    zx_status_t status = ZX_OK;
    fbl::Vector<BlockRegion> buffer =  blobfs_->GetAllocatedRegions();
    uint64_t allocations = buffer.size();
    if (allocations != 0) {
        status = zx::vmo::create(sizeof(BlockRegion) * allocations, 0, &vmo);
        if (status == ZX_OK) {
            status = vmo.write(buffer.get(), 0, sizeof(BlockRegion) * allocations);
        }
    }
    return fuchsia_blobfs_BlobfsGetAllocatedRegions_reply(txn, status,
                                                          status == ZX_OK ? vmo.get() :
                                                          ZX_HANDLE_INVALID,
                                                          status == ZX_OK ? allocations : 0);
}

} // namespace blobfs
