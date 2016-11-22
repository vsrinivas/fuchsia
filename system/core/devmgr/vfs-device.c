// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vfs.h"
#include "dnode.h"
#include "devmgr.h"

#include <magenta/listnode.h>

#include <ddk/device.h>

#include <mxio/debug.h>
#include <mxio/vfs.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <stdlib.h>
#include <string.h>

#include "device-internal.h"


#define MXDEBUG 0


mx_status_t devfs_remove(vnode_t* vn) {
    mtx_lock(&vfs_lock);

    // hold a reference to ourselves so the rug doesn't get pulled out from under us
    vn_acquire(vn);

    xprintf("devfs_remove(%p)\n", vn);
    vn->remote = 0;

    // if this vnode is a directory, delete its dnode
    if (vn->dnode) {
        xprintf("devfs_remove(%p) delete dnode\n", vn);
        dn_delete(vn->dnode);
        vn->dnode = NULL;
    }

    // delete all dnodes that point to this vnode
    // (effectively unlink() it from every directory it is in)
    dnode_t* dn;
    while ((dn = list_peek_head_type(&vn->dn_list, dnode_t, vn_entry)) != NULL) {
        if (vn->dnode == dn) {
            vn->dnode = NULL;
        }
        dn_delete(dn);
    }

    vn_release(vn);
    mtx_unlock(&vfs_lock);

    // with all dnodes destroyed, nothing should hold a reference
    // to the vnode and it should be release()'d
    return NO_ERROR;
}
