// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <ddk/device.h>
#include <fs/vfs.h>
#include <fdio/debug.h>
#include <fdio/io.h>
#include <fdio/vfs.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>

#include "dnode.h"
#include "memfs-private.h"

#define MXDEBUG 0

namespace memfs {

static zx_status_t add_file(fbl::RefPtr<VnodeDir> vnb, const char* path, zx_handle_t vmo,
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
            return vnb->CreateFromVmo(vmofile, path, strlen(path), vmo, off, len);
        } else {
            if (nextpath == path) {
                return ZX_ERR_INVALID_ARGS;
            }

            fbl::RefPtr<fs::Vnode> out;
            r = vnb->Lookup(&out, path, nextpath - path);
            if (r == ZX_ERR_NOT_FOUND) {
                r = vnb->Create(&out, path, nextpath - path, S_IFDIR);
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

// The following functions exist outside the memfs namespace so they can
// be exposed to C:

zx_status_t bootfs_add_file(const char* path, zx_handle_t vmo, zx_off_t off, size_t len) {
    return add_file(BootfsRoot(), path, vmo, off, len);
}

zx_status_t systemfs_add_file(const char* path, zx_handle_t vmo, zx_off_t off, size_t len) {
    return add_file(SystemfsRoot(), path, vmo, off, len);
}
