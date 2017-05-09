// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include <fs/vfs.h>
#include <ddk/device.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/debug.h>
#include <mxtl/auto_lock.h>

#include "dnode.h"
#include "devmgr.h"
#include "device-internal.h"
#include "memfs-private.h"

#define MXDEBUG 0

mx_status_t devfs_remove(VnodeDir* vn) {
    mxtl::AutoLock lock(&vfs_lock);

    xprintf("devfs_remove(%p)\n", vn);
    vn->DetachRemote();

    // If this vnode is a directory, delete its dnode
    if (vn->IsDirectory()) {
        xprintf("devfs_remove(%p) delete dnode\n", vn);
        if (vn->dnode_->HasChildren()) {
            // Detach the vnode, flag it to be deleted later.
            vn->dnode_->RemoveFromParent();
            vn->DetachDevice();
            return NO_ERROR;
        } else {
            vn->dnode_->Detach();
            vn->dnode_ = nullptr;
        }
    }

    // The raw "vn" ptr was originally leaked from a RefPtr when
    // the device was created. Now that no one holds a reference
    // to it, we can delete it, since this code is the only
    // logical "owner" of vn.
    delete vn;
    return NO_ERROR;
}
