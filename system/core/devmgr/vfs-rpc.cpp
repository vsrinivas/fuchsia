// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>

#include <async/cpp/loop.h>
#include <fs/vfs.h>
#include <zircon/device/device.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/thread_annotations.h>
#include <fdio/debug.h>
#include <fdio/io.h>
#include <fdio/remoteio.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>

#include "devmgr.h"
#include "memfs-private.h"

#define ZXDEBUG 0

namespace memfs {
namespace {

Vfs root_vfs;
Vfs system_vfs;
fbl::unique_ptr<async::Loop> global_loop;

}  // namespace

static fbl::RefPtr<VnodeDir> global_root = nullptr;
static fbl::RefPtr<VnodeDir> memfs_root = nullptr;
static fbl::RefPtr<VnodeDir> devfs_root = nullptr;
static fbl::RefPtr<VnodeDir> bootfs_root = nullptr;
static fbl::RefPtr<VnodeDir> systemfs_root = nullptr;
static VnodeMemfs* global_vfs_root = nullptr;

zx_status_t add_vmofile(fbl::RefPtr<VnodeDir> vnb, const char* path, zx_handle_t vmo,
                        zx_off_t off, size_t len) {
    zx_status_t r;
    if ((path[0] == '/') || (path[0] == 0))
        return ZX_ERR_INVALID_ARGS;
    for (;;) {
        const char* nextpath = strchr(path, '/');
        if (nextpath == nullptr) {
            if (path[0] == 0) {
                return ZX_ERR_INVALID_ARGS;
            }
            bool vmofile = true;
            return vnb->vfs()->CreateFromVmo(vnb.get(), vmofile,
                                             fbl::StringPiece(path, strlen(path)), vmo, off, len);
        } else {
            if (nextpath == path) {
                return ZX_ERR_INVALID_ARGS;
            }

            fbl::RefPtr<fs::Vnode> out;
            r = vnb->Lookup(&out, fbl::StringPiece(path, nextpath - path));
            if (r == ZX_ERR_NOT_FOUND) {
                r = vnb->Create(&out, fbl::StringPiece(path, nextpath - path), S_IFDIR);
            }

            if (r < 0) {
                return r;
            }
            vnb = fbl::RefPtr<VnodeDir>::Downcast(fbl::move(out));
            path = nextpath + 1;
        }
    }
}

} // namespace memfs

// The following functions exist outside the memfs namespace so they
// can be exposed to C:

fbl::RefPtr<memfs::VnodeDir> SystemfsRoot() {
    if (memfs::systemfs_root == nullptr) {
        zx_status_t r = memfs::createFilesystem("system", &memfs::system_vfs, &memfs::systemfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'system' file system\n", r);
            __builtin_trap();
        }
    }
    return memfs::systemfs_root;
}

fbl::RefPtr<memfs::VnodeDir> MemfsRoot() {
    if (memfs::memfs_root == nullptr) {
        zx_status_t r = memfs::createFilesystem("tmp", &memfs::root_vfs, &memfs::memfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'tmp' file system\n", r);
            __builtin_trap();
        }
    }
    return memfs::memfs_root;
}

fbl::RefPtr<memfs::VnodeDir> DevfsRoot() {
    if (memfs::devfs_root == nullptr) {
        zx_status_t r = memfs::createFilesystem("dev", &memfs::root_vfs, &memfs::devfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'device' file system\n", r);
            __builtin_trap();
        }
    }
    return memfs::devfs_root;
}

fbl::RefPtr<memfs::VnodeDir> BootfsRoot() {
    if (memfs::bootfs_root == nullptr) {
        zx_status_t r = memfs::createFilesystem("boot", &memfs::root_vfs, &memfs::bootfs_root);
        if (r < 0) {
            printf("fatal error %d allocating 'boot' file system\n", r);
            __builtin_trap();
        }
    }
    return memfs::bootfs_root;
}

zx_status_t devfs_mount(zx_handle_t h) {
    return DevfsRoot()->AttachRemote(fs::MountChannel(h));
}

VnodeDir* systemfs_get_root() {
    return SystemfsRoot().get();
}

void systemfs_set_readonly(bool value) {
    SystemfsRoot()->vfs()->SetReadonly(value);
}

zx_status_t bootfs_add_file(const char* path, zx_handle_t vmo, zx_off_t off, size_t len) {
    return add_vmofile(BootfsRoot(), path, vmo, off, len);
}

zx_status_t systemfs_add_file(const char* path, zx_handle_t vmo, zx_off_t off, size_t len) {
    return add_vmofile(SystemfsRoot(), path, vmo, off, len);
}

// Hardcoded initialization function to create/access global root directory
VnodeDir* vfs_create_global_root() {
    if (memfs::global_root == nullptr) {
        zx_status_t r = memfs::createFilesystem("<root>", &memfs::root_vfs, &memfs::global_root);
        if (r < 0) {
            printf("fatal error %d allocating root file system\n", r);
            __builtin_trap();
        }

        memfs::root_vfs.MountSubtree(memfs::global_root.get(), DevfsRoot());
        memfs::root_vfs.MountSubtree(memfs::global_root.get(), BootfsRoot());
        memfs::root_vfs.MountSubtree(memfs::global_root.get(), MemfsRoot());

        fbl::RefPtr<fs::Vnode> vn;
        fbl::StringPiece pathout;
        ZX_ASSERT(memfs::root_vfs.Open(memfs::global_root, &vn, fbl::StringPiece("/data"), &pathout,
                                       ZX_FS_RIGHT_READABLE | ZX_FS_FLAG_CREATE, S_IFDIR) == ZX_OK);
        ZX_ASSERT(memfs::root_vfs.Open(memfs::global_root, &vn, fbl::StringPiece("/volume"), &pathout,
                                       ZX_FS_RIGHT_READABLE | ZX_FS_FLAG_CREATE, S_IFDIR) == ZX_OK);

        memfs::global_loop.reset(new async::Loop());
        memfs::global_loop->StartThread("root-dispatcher");
        memfs::root_vfs.set_async(memfs::global_loop->async());
        memfs::system_vfs.set_async(memfs::global_loop->async());
    }
    return memfs::global_root.get();
}

void devmgr_vfs_exit() {
    memfs::root_vfs.UninstallAll(zx_deadline_after(ZX_SEC(5)));
    memfs::system_vfs.UninstallAll(zx_deadline_after(ZX_SEC(5)));
}

zx_status_t memfs_mount(VnodeDir* parent, const char* name, VnodeDir* subtree) {
    fbl::RefPtr<fs::Vnode> vn;
    zx_status_t status = parent->Create(&vn, fbl::StringPiece(name), S_IFDIR);
    if (status != ZX_OK)
        return status;
    zx_handle_t h;
    status = vfs_create_root_handle(subtree, &h);
    if (status != ZX_OK)
        return status;
    return parent->vfs()->InstallRemote(fbl::move(vn), fs::MountChannel(h));
}

// Acquire the root vnode and return a handle to it through the VFS dispatcher
zx_status_t vfs_create_root_handle(VnodeMemfs* vn, zx_handle_t* out) {
    zx::channel h1, h2;
    zx_status_t r = zx::channel::create(0, &h1, &h2);
    if (r == ZX_OK) {
        r = vn->vfs()->ServeDirectory(fbl::RefPtr<fs::Vnode>(vn),
                                      fbl::move(h1));
    }
    if (r == ZX_OK) {
        *out = h2.release();
    }
    return r;
}

zx_status_t vfs_connect_root_handle(VnodeMemfs* vn, zx_handle_t h) {
    zx::channel ch(h);
    return vn->vfs()->ServeDirectory(fbl::RefPtr<fs::Vnode>(vn), fbl::move(ch));
}
// Initialize the global root VFS node
void vfs_global_init(VnodeDir* root) {
    memfs::global_vfs_root = root;
}

// Return a RIO handle to the global root
zx_status_t vfs_create_global_root_handle(zx_handle_t* out) {
    return vfs_create_root_handle(memfs::global_vfs_root, out);
}

zx_status_t vfs_connect_global_root_handle(zx_handle_t h) {
    return vfs_connect_root_handle(memfs::global_vfs_root, h);
}
