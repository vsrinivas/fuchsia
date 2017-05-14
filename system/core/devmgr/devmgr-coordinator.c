// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devcoordinator.h"
#include "devmgr.h"
#include "memfs-private.h"

#include <fs/vfs.h>

#include <ddk/device.h>
#include <ddk/driver.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <magenta/processargs.h>

#include <mxio/debug.h>
#include <mxio/dispatcher.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>

#include <magenta/listnode.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// The devmgr coordinator is an rpc service which devhost processes
// use to inform the devmgr when devices are published or removed.
//
// This service makes these published devices visible via the
// device filesystem visible at /dev in the devmgr's root namespace.

// vnodes for root driver and protocols
static VnodeDir* vnroot;
static VnodeDir* vnclass;

#define PNMAX 16
static const char* proto_name(uint32_t id, char buf[PNMAX]) {
    switch (id) {
#define DDK_PROTOCOL_DEF(tag, val, name, flags) case val: return name;
#include <ddk/protodefs.h>
    default:
        snprintf(buf, PNMAX, "proto-%08x", id);
        return buf;
    }
}

typedef struct {
    const char* name;
    VnodeDir* vnode;
    uint32_t id;
    uint32_t flags;
} pinfo_t;

static pinfo_t proto_info[] = {
#define DDK_PROTOCOL_DEF(tag, val, name, flags) { name, NULL, val, flags },
#include <ddk/protodefs.h>
    { NULL, NULL, 0, 0 },
};

static VnodeDir* proto_dir(uint32_t id) {
    for (pinfo_t* info = proto_info; info->name; info++) {
        if (info->id == id) {
            return info->vnode;
        }
    }
    return NULL;
}

static void prepopulate_protocol_dirs(void) {
    for (pinfo_t* info = proto_info; info->name; info++) {
        if (!(info->flags & PF_NOPUB)) {
            memfs_create_device_at(vnclass, &info->vnode, info->name, 0);
        }
    }
}

mx_status_t do_publish(device_t* parent, device_t* ctx) {
    if (memfs_create_device_at(parent->vnode, (VnodeDir**) &ctx->vnode, ctx->name, ctx->hrpc)) {
        printf("devmgr: could not add '%s' to devfs!\n", ctx->name);
        return ERR_INTERNAL;
    }

    if ((ctx->protocol_id == MX_PROTOCOL_MISC_PARENT) ||
        (ctx->protocol_id == MX_PROTOCOL_MISC)) {
        // misc devices are singletons, not a class
        // in the sense of other device classes.
        // They do not get aliases in /dev/class/misc/...
        // instead they exist only under their parent
        // device.
        return NO_ERROR;
    }

    // Create link in /dev/class/... if this id has a published class
    VnodeDir* vnp = proto_dir(ctx->protocol_id);
    if (vnp != NULL) {
        const char* name = ctx->name;
        if ((ctx->protocol_id != MX_PROTOCOL_MISC) &&
            (ctx->protocol_id != MX_PROTOCOL_CONSOLE)) {
            // request a numeric name
            name = NULL;
        }

        if (memfs_add_link(vnp, name, (VnodeMemfs*) ctx->vnode) < 0) {
            printf("devmgr: could not link to '%s'\n", ctx->name);
        }
    }

    return NO_ERROR;
}

void do_unpublish(device_t* dev) {
    if (dev->vnode != NULL) {
        devfs_remove(dev->vnode);
        dev->vnode = NULL;
    }
}

void devmgr_init(mx_handle_t root_job) {
    printf("devmgr: init\n");

    vnroot = devfs_get_root();
    memfs_create_device_at(vnroot, &vnclass, "class", 0);
    prepopulate_protocol_dirs();

    coordinator_init(vnroot, root_job);
}

void devmgr_handle_messages(void) {
    coordinator();
}
