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

static mx_status_t _devfs_add_link(vnode_t* parent, const char* name, vnode_t* target) {
    if ((parent == NULL) || (target == NULL)) {
        return ERR_INVALID_ARGS;
    }

    xprintf("devfs_add_link() p=%p name='%s'\n", parent, name ? name : "###");
    mx_status_t r;
    dnode_t* dn;

    char tmp[8];
    size_t len;
    if (name == NULL) {
        //TODO: something smarter
        // right now we have so few devices and instances this is not a problem
        // but it clearly is not optimal
        // seqcount is used to avoid rapidly re-using device numbers
        for (unsigned n = 0; n < 1000; n++) {
            snprintf(tmp, sizeof(tmp), "%03u", (parent->seqcount++) % 1000);
            if (dn_lookup(parent->dnode, &dn, tmp, 3) != NO_ERROR) {
                name = tmp;
                len = 3;
                goto got_name;
            }
        }
        return ERR_ALREADY_EXISTS;
    } else {
        len = strlen(name);
        if (dn_lookup(parent->dnode, &dn, name, len) == NO_ERROR) {
            return ERR_ALREADY_EXISTS;
        }
    }
got_name:
    if ((r = dn_create(&dn, name, len, target)) < 0) {
        return r;
    }
    dn_add_child(parent->dnode, dn);
    vfs_notify_add(parent, name, len);
    return NO_ERROR;
}

mx_status_t devfs_add_link(vnode_t* parent, const char* name, vnode_t* target) {
    mx_status_t r;
    mtx_lock(&vfs_lock);
    r = _devfs_add_link(parent, name, target);
    mtx_unlock(&vfs_lock);
    return r;
}

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
