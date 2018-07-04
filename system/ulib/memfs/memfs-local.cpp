// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/vfs.h>
#include <fs/vfs.h>
#include <lib/async/dispatcher.h>
#include <lib/memfs/cpp/vnode.h>
#include <lib/memfs/memfs.h>
#include <sync/completion.h>
#include <zircon/device/vfs.h>

#include "dnode.h"

struct memfs_filesystem {
    memfs::Vfs vfs;
};

zx_status_t memfs_create_filesystem(async_dispatcher_t* dispatcher, memfs_filesystem_t** out_fs,
                                    zx_handle_t* out_root) {
    ZX_DEBUG_ASSERT(dispatcher != nullptr);
    ZX_DEBUG_ASSERT(out_fs != nullptr);
    ZX_DEBUG_ASSERT(out_root != nullptr);

    zx::channel client, server;
    zx_status_t status = zx::channel::create(0, &client, &server);
    if (status != ZX_OK) {
        return status;
    }

    fbl::unique_ptr<memfs_filesystem_t> fs = fbl::make_unique<memfs_filesystem_t>();
    fs->vfs.SetDispatcher(dispatcher);

    fbl::RefPtr<memfs::VnodeDir> root;
    if ((status = memfs::createFilesystem("<tmp>", &fs->vfs, &root)) != ZX_OK) {
        return status;
    }
    if ((status = fs->vfs.ServeDirectory(fbl::move(root), fbl::move(server))) != ZX_OK) {
        return status;
    }

    *out_fs = fs.release();
    *out_root = client.release();
    return ZX_OK;
}

zx_status_t memfs_install_at(async_dispatcher_t* dispatcher, const char* path) {
    fdio_ns_t* ns;
    zx_status_t status = fdio_ns_get_installed(&ns);
    if (status != ZX_OK) {
        return status;
    }

    memfs_filesystem_t* fs;
    zx_handle_t root;
    status = memfs_create_filesystem(dispatcher, &fs, &root);
    if (status != ZX_OK) {
        return status;
    }

    status = fdio_ns_bind(ns, path, root);
    if (status != ZX_OK) {
        memfs_free_filesystem(fs, nullptr);
        zx_handle_close(root);
        return status;
    }

    return ZX_OK;
}

void memfs_free_filesystem(memfs_filesystem_t* fs, completion_t* unmounted) {
    ZX_DEBUG_ASSERT(fs != nullptr);
    fs->vfs.Shutdown([fs, unmounted](zx_status_t status) {
        delete fs;
        if (unmounted) {
            completion_signal(unmounted);
        }
    });
}
