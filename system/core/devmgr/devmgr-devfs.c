// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devcoordinator.h"
#include "devmgr.h"
#include "memfs-private.h"

#include <magenta/listnode.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

#include <magenta/device/vfs.h>

#include <mxio/remoteio.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern device_t socket_device;

typedef struct dc_watcher watcher_t;
typedef struct dc_iostate iostate_t;

struct dc_watcher {
    watcher_t* next;
    devnode_t* devnode;
    uint32_t mask;
    mx_handle_t handle;
};

struct dc_devnode {
    const char* name;
    uint64_t ino;

    // NULL if we are a pure directory node,
    // otherwise the device we are referencing
    device_t* device;

    watcher_t* watchers;

    // entry in our parent devnode's children list
    list_node_t node;

    // list of our child devnodes
    list_node_t children;

    // list of attached iostates
    list_node_t iostate;

    // used to assign unique small device numbers
    // for class device links
    uint32_t seqcount;
};

struct dc_iostate {
    port_handler_t ph;

    // entry in our devnode's iostate list
    list_node_t node;

    // pointer to our devnode, NULL if it has been removed
    devnode_t* devnode;

    uint64_t readdir_ino;
};

extern port_t dc_port;

static uint64_t next_ino = 2;

static devnode_t root_devnode = {
    .name = "",
    .ino = 1,
    .children = LIST_INITIAL_VALUE(root_devnode.children),
    .iostate = LIST_INITIAL_VALUE(root_devnode.iostate),
};

static devnode_t* class_devnode;

static mx_status_t dc_rio_handler(port_handler_t* ph, mx_signals_t signals, uint32_t evt);
static devnode_t* devfs_mkdir(devnode_t* parent, const char* name);

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
    devnode_t* devnode;
    uint32_t id;
    uint32_t flags;
} pinfo_t;

static pinfo_t proto_info[] = {
#define DDK_PROTOCOL_DEF(tag, val, name, flags) { name, NULL, val, flags },
#include <ddk/protodefs.h>
    { NULL, NULL, 0, 0 },
};

static devnode_t* proto_dir(uint32_t id) {
    for (pinfo_t* info = proto_info; info->name; info++) {
        if (info->id == id) {
            return info->devnode;
        }
    }
    return NULL;
}

static void prepopulate_protocol_dirs(void) {
    class_devnode = devfs_mkdir(&root_devnode, "class");
    for (pinfo_t* info = proto_info; info->name; info++) {
        if (!(info->flags & PF_NOPUB)) {
            info->devnode = devfs_mkdir(class_devnode, info->name);
        }
    }
}

static mx_status_t iostate_create(devnode_t* dn, mx_handle_t h) {
    iostate_t* ios = calloc(1, sizeof(iostate_t));
    if (ios == NULL) {
        return MX_ERR_NO_MEMORY;
    }

    ios->ph.handle = h;
    ios->ph.waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
    ios->ph.func = dc_rio_handler;
    ios->devnode = dn;
    list_add_tail(&dn->iostate, &ios->node);

    mx_status_t r;
    if ((r = port_wait(&dc_port, &ios->ph)) < 0) {
        list_delete(&ios->node);
        free(ios);
    }
    return r;
}

static void iostate_destroy(iostate_t* ios) {
    if (ios->devnode) {
        list_delete(&ios->node);
        ios->devnode = NULL;
    }
    mx_handle_close(ios->ph.handle);
    ios->ph.handle = MX_HANDLE_INVALID;
    free(ios);
}

// A devnode is a directory (from stat's perspective) if
// it has children, or if it doesn't have a device, or if
// its device has no rpc handle
static bool devnode_is_dir(devnode_t* dn) {
    if (list_is_empty(&dn->children)) {
        return (dn->device == NULL) || (dn->device->hrpc == MX_HANDLE_INVALID);
    }
    return true;
}

// Local devnodes are ones that we should not hand off OPEN
// RPCs to the underlying devhost
static bool devnode_is_local(devnode_t* dn) {
    if (dn->device == NULL) {
        return true;
    }
    if (dn->device->hrpc == MX_HANDLE_INVALID) {
        return true;
    }
    if (dn->device->flags & DEV_CTX_BUSDEV) {
        return true;
    }
    return false;
}

static void devfs_notify(devnode_t* dn, const char* name, unsigned op) {
    watcher_t* w = dn->watchers;
    if (w == NULL) {
        return;
    }

    size_t len = strlen(name);
    if (len > VFS_WATCH_NAME_MAX) {
        return;
    }

    uint8_t msg[VFS_WATCH_NAME_MAX + 2];
    msg[0] = op;
    msg[1] = len;
    memcpy(msg + 2, name, len);

    // convert to mask
    op = (1u << op);

    watcher_t** wp;
    watcher_t* next;
    for (wp = &dn->watchers; w != NULL; w = next) {
        next = w->next;
        if (!(w->mask & op)) {
            continue;
        }
        if (mx_channel_write(w->handle, 0, msg, len + 2, NULL, 0) < 0) {
            *wp = next;
            mx_handle_close(w->handle);
            free(w);
        } else {
            wp = &w->next;
        }
    }
}

static mx_status_t devfs_watch(devnode_t* dn, mx_handle_t h, uint32_t mask) {
    watcher_t* watcher = calloc(1, sizeof(watcher_t));
    if (watcher == NULL) {
        return MX_ERR_NO_MEMORY;
    }

    watcher->devnode = dn;
    watcher->next = dn->watchers;
    watcher->handle = h;
    watcher->mask = mask;
    dn->watchers = watcher;

    if (mask & VFS_WATCH_MASK_EXISTING) {
        devnode_t* child;
        list_for_every_entry(&dn->children, child, devnode_t, node) {
            //TODO: send multiple per write
            devfs_notify(dn, child->name, VFS_WATCH_EVT_EXISTING);
        }
        devfs_notify(dn, "", VFS_WATCH_EVT_IDLE);

    }

    // Don't send EXISTING or IDLE events from now on...
    watcher->mask &= ~(VFS_WATCH_MASK_EXISTING | VFS_WATCH_MASK_IDLE);

    return MX_OK;
}

// If namelen is nonzero, it is the null-terminator-inclusive length
// of name, which should be copied into the devnode.  Otherwise name
// is guaranteed to exist for the lifetime of the devnode.
static devnode_t* devfs_mknode(device_t* dev, const char* name, size_t namelen) {
    devnode_t* dn = calloc(1, sizeof(devnode_t) + namelen);
    if (dn == NULL) {
        return NULL;
    }
    if (namelen > 0) {
        char* p = (char*) (dn + 1);
        memcpy(p, name, namelen);
        dn->name = p;
    } else {
        dn->name = name;
    }
    dn->ino = next_ino++;
    dn->device = dev;
    list_initialize(&dn->children);
    list_initialize(&dn->iostate);
    return dn;
}

static devnode_t* devfs_mkdir(devnode_t* parent, const char* name) {
    devnode_t* dn = devfs_mknode(NULL, name, 0);
    if (dn == NULL) {
        return NULL;
    }
    list_add_tail(&parent->children, &dn->node);
    return dn;
}

static devnode_t* devfs_lookup(devnode_t* parent, const char* name) {
    devnode_t* child;
    list_for_every_entry(&parent->children, child, devnode_t, node) {
        if (!strcmp(name, child->name)) {
            return child;
        }
    }
    return NULL;
}

mx_status_t devfs_publish(device_t* parent, device_t* dev) {
    if ((parent->self == NULL) || (dev->self != NULL) || (dev->link != NULL)) {
        return MX_ERR_INTERNAL;
    }

    devnode_t* dnself = devfs_mknode(dev, dev->name, 0);
    if (dnself == NULL) {
        return MX_ERR_NO_MEMORY;
    }

    if ((dev->protocol_id == MX_PROTOCOL_MISC_PARENT) ||
        (dev->protocol_id == MX_PROTOCOL_MISC)) {
        // misc devices are singletons, not a class
        // in the sense of other device classes.
        // They do not get aliases in /dev/class/misc/...
        // instead they exist only under their parent
        // device.
        goto done;
    }

    // Create link in /dev/class/... if this id has a published class
    devnode_t* dir = proto_dir(dev->protocol_id);
    if (dir != NULL) {
        char tmp[32];
        const char* name = dev->name;
        size_t namelen = 0;

        if ((dev->protocol_id != MX_PROTOCOL_MISC) &&
            (dev->protocol_id != MX_PROTOCOL_CONSOLE)) {

            for (unsigned n = 0; n < 1000; n++) {
                snprintf(tmp, sizeof(tmp), "%03u", (dir->seqcount++) % 1000);
                if (devfs_lookup(dir, tmp) == NULL) {
                    name = tmp;
                    namelen = 4;
                    goto got_name;
                }
            }
            free(dnself);
            return MX_ERR_ALREADY_EXISTS;
got_name:
            ;
        }

        devnode_t* dnlink = devfs_mknode(dev, name, namelen);
        if (dnlink == NULL) {
            free(dnself);
            return MX_ERR_NO_MEMORY;
        }

        // add link node to class directory
        list_add_tail(&dir->children, &dnlink->node);
        dev->link = dnlink;
        devfs_notify(dir, dnlink->name, VFS_WATCH_EVT_ADDED);
    }

done:
    // add self node to parent directory
    list_add_tail(&parent->self->children, &dnself->node);
    dev->self = dnself;
    devfs_notify(parent->self, dnself->name, VFS_WATCH_EVT_ADDED);
    return MX_OK;
}

static void _devfs_remove(devnode_t* dn) {
    if (list_in_list(&dn->node)) {
        list_delete(&dn->node);
    }

    // disconnect from device
    if (dn->device != NULL) {
        if (dn->device->self == dn) {
            dn->device->self = NULL;
        }
        if (dn->device->link == dn) {
            dn->device->link = NULL;
        }
        dn->device = NULL;
    }

    // detach all connected iostates
    iostate_t* ios;
    list_for_every_entry(&dn->iostate, ios, iostate_t, node) {
        ios->devnode = NULL;
        mx_handle_close(ios->ph.handle);
        ios->ph.handle = MX_HANDLE_INVALID;
    }

    devfs_notify(dn, "", VFS_WATCH_EVT_DELETED);

    // destroy all watchers
    watcher_t* watcher;
    watcher_t* next;
    for (watcher = dn->watchers; watcher != NULL; watcher = next) {
        next = watcher->next;
        mx_handle_close(watcher->handle);
        free(watcher);
    }
    dn->watchers = NULL;

    // detach children
    while (list_remove_head(&dn->children) != NULL) {
        // they will be unpublished when the devices they're
        // associated with are eventually destroyed
    }
}

void devfs_unpublish(device_t* dev) {
    if (dev->self != NULL) {
        _devfs_remove(dev->self);
        dev->self = NULL;
    }
    if (dev->link != NULL) {
        _devfs_remove(dev->link);
        dev->link = NULL;
    }
}

static mx_status_t devfs_walk(devnode_t** _dn, char* path, char** pathout) {
    devnode_t* dn = *_dn;

again:
    if ((path == NULL) || (path[0] == 0)) {
        *_dn = dn;
        return MX_OK;
    }
    char* name = path;
    char* undo = NULL;
    if ((path = strchr(path, '/')) != NULL) {
        undo = path;
        *path++ = 0;
    }
    if (name[0] == 0) {
        return MX_ERR_BAD_PATH;
    }
    devnode_t* child;
    list_for_every_entry(&dn->children, child, devnode_t, node) {
        if (!strcmp(child->name, name)) {
            dn = child;
            goto again;
        }
    }
    if (dn == *_dn) {
        return MX_ERR_NOT_FOUND;
    }
    if (undo) {
        *undo = '/';
    }
    *_dn = dn;
    *pathout = name;
    return MX_ERR_NEXT;
}

static void devfs_open(devnode_t* dirdn, mx_handle_t h, char* path, uint32_t flags) {
    if (!strcmp(path, ".")) {
        path = NULL;
    }

    devnode_t* dn = dirdn;
    mx_status_t r = devfs_walk(&dn, path, &path);

    bool pipeline = flags & O_PIPELINE;

    if (r == MX_ERR_NEXT) {
        // we only partially matched -- there's more path to walk
        if ((dn->device == NULL) || (dn->device->hrpc == MX_HANDLE_INVALID)) {
            // no remote to pass this on to
            r = MX_ERR_NOT_FOUND;
        } else if (flags & (O_NOREMOTE | O_DIRECTORY)) {
            // local requested, but this is remote only
            r = MX_ERR_NOT_SUPPORTED;
        } else {
            r = MX_OK;
        }
    } else {
        path = (char*) ".";
        if (dn->device == &socket_device) {
            // don't hand off opens of the /dev/socket "directory"
            // to netstack, since it doesn't want to deal with STAT
            // or the like
            flags |= O_NOREMOTE;
        }
    }

    if (r < 0) {
fail:
        if (!pipeline) {
            mxrio_object_t obj;
            obj.status = r;
            obj.type = 0;
            mx_channel_write(h, 0, &obj, MXRIO_OBJECT_MINSIZE, NULL, 0);
        }
        mx_handle_close(h);
        return;
    }

    // If we are a local-only node, or we are asked to not go remote,
    // or we are asked to open-as-a-directory, open locally:
    if ((flags & (O_NOREMOTE | O_DIRECTORY)) || devnode_is_local(dn)) {
        if ((r = iostate_create(dn, h)) < 0) {
            goto fail;
        }
        if (!pipeline) {
            mxrio_object_t obj;
            obj.status = MX_OK;
            obj.type = MXIO_PROTOCOL_REMOTE;
            mx_channel_write(h, 0, &obj, MXRIO_OBJECT_MINSIZE, NULL, 0);
        }
        return;
    }

    // Otherwise we will pass the request on to the remote
    mxrio_msg_t msg;
    memset(&msg, 0, MXRIO_HDR_SZ);
    msg.op = MXRIO_OPEN;
    msg.datalen = strlen(path);
    msg.arg = flags;
    msg.hcount = 1;
    msg.handle[0] = h;
    memcpy(msg.data, path, msg.datalen);

    if ((r = mx_channel_write(dn->device->hrpc, 0, &msg, MXRIO_HDR_SZ + msg.datalen,
                              msg.handle, 1)) < 0) {
        goto fail;
    }
}

static mx_status_t fill_dirent(vdirent_t* de, size_t delen,
                               const char* name, size_t len, uint32_t type) {
    size_t sz = sizeof(vdirent_t) + len + 1;

    // round up to uint32 aligned
    if (sz & 3)
        sz = (sz + 3) & (~3);
    if (sz > delen)
        return MX_ERR_INVALID_ARGS;
    de->size = sz;
    de->type = type;
    memcpy(de->name, name, len);
    de->name[len] = 0;
    return sz;
}

static mx_status_t devfs_readdir(devnode_t* dn, uint64_t* _ino, void* data, size_t len) {
    void* ptr = data;
    uint64_t ino = *_ino;

    devnode_t* child;
    list_for_every_entry(&dn->children, child, devnode_t, node) {
        if (child->ino <= ino) {
            continue;
        }
        // "pure" directories (like /dev/class/$NAME) do not show up
        // if they have no children, to avoid clutter and confusion.
        // They remain openable, so they can be watched.
        if ((child->device == NULL) && list_is_empty(&child->children)) {
            continue;
        }
        ino = child->ino;
        mx_status_t r = fill_dirent(ptr, len, child->name, strlen(child->name),
                                    VTYPE_TO_DTYPE(V_TYPE_DIR));
        if (r < 0) {
            break;
        }
        ptr += r;
        len -= r;
    }

    *_ino = ino;
    return ptr - data;
}

static mx_status_t devfs_rio_handler(mxrio_msg_t* msg, void* cookie) {
    iostate_t* ios = cookie;
    devnode_t* dn = ios->devnode;
    if (dn == NULL) {
        return MX_ERR_PEER_CLOSED;
    }

    // ensure handle count specified by opcode matches reality
    if (msg->hcount != MXRIO_HC(msg->op)) {
        return MX_ERR_IO;
    }
    msg->hcount = 0;

    uint32_t len = msg->datalen;
    int32_t arg = msg->arg;
    msg->datalen = 0;

    switch (MXRIO_OP(msg->op)) {
    case MXRIO_CLONE:
        msg->data[0] = 0;
        devfs_open(dn, msg->handle[0], (char*) msg->data, arg | O_NOREMOTE);
        return ERR_DISPATCHER_INDIRECT;
    case MXRIO_OPEN:
        if ((len < 1) || (len > 1024)) {
            mx_handle_close(msg->handle[0]);
        } else {
            msg->data[len] = 0;
            devfs_open(dn, msg->handle[0], (char*) msg->data, arg);
        }
        return ERR_DISPATCHER_INDIRECT;
    case MXRIO_STAT:
        msg->datalen = sizeof(vnattr_t);
        vnattr_t* attr = (void*)msg->data;
        memset(attr, 0, sizeof(vnattr_t));
        if (devnode_is_dir(dn)) {
            attr->mode = V_TYPE_DIR | V_IRUSR | V_IWUSR;
        } else {
            attr->mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
        }
        attr->size = 0;
        attr->nlink = 1;
        return msg->datalen;
    case MXRIO_READDIR:
        if (arg > MXIO_CHUNK_SIZE) {
            return MX_ERR_INVALID_ARGS;
        }
        if (msg->arg2.off == READDIR_CMD_RESET) {
            ios->readdir_ino = 0;
        }
        mx_status_t r = devfs_readdir(dn, &ios->readdir_ino, msg->data, arg);
        if (r >= 0) {
            msg->datalen = r;
        }
        return r;
    case MXRIO_IOCTL_1H:
        switch (msg->arg2.op) {
        case IOCTL_VFS_MOUNT_FS: {
            mx_status_t r;
            if (len != sizeof(mx_handle_t)) {
                r = MX_ERR_INVALID_ARGS;
            } else if (dn->device != &socket_device) {
                r = MX_ERR_NOT_SUPPORTED;
            } else {
                if (socket_device.hrpc != MX_HANDLE_INVALID) {
                    mx_handle_close(socket_device.hrpc);
                }
                socket_device.hrpc = msg->handle[0];
                r = MX_OK;
            }
            if (r != MX_OK) {
                mx_handle_close(msg->handle[0]);
            }
            return r;
        }
        case IOCTL_VFS_WATCH_DIR: {
            vfs_watch_dir_t* wd = (vfs_watch_dir_t*) msg->data;
            if ((len != sizeof(vfs_watch_dir_t)) ||
                (wd->options != 0) ||
                (wd->mask & (~VFS_WATCH_MASK_ALL))) {
                r = MX_ERR_INVALID_ARGS;
            } else {
                r = devfs_watch(dn, msg->handle[0], wd->mask);
            }
            if (r != MX_OK) {
                mx_handle_close(msg->handle[0]);
            }
            return r;
        }
        }
        break;
    case MXRIO_IOCTL:
        switch (msg->arg2.op) {
        case IOCTL_VFS_QUERY_FS: {
            const char* devfs_name = "devfs";
            if (arg < (int32_t) (sizeof(vfs_query_info_t) + strlen(devfs_name))) {
                return MX_ERR_INVALID_ARGS;
            }
            vfs_query_info_t* info = (vfs_query_info_t*) msg->data;
            memset(info, 0, sizeof(*info));
            memcpy(info->name, devfs_name, strlen(devfs_name));
            msg->datalen = sizeof(vfs_query_info_t) + strlen(devfs_name);
            return sizeof(vfs_query_info_t) + strlen(devfs_name);
        }
        }
        break;
    }

    // close inbound handles so they do not leak
    for (unsigned i = 0; i < MXRIO_HC(msg->op); i++) {
        mx_handle_close(msg->handle[i]);
    }
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t dc_rio_handler(port_handler_t* ph, mx_signals_t signals, uint32_t evt) {
    iostate_t* ios = containerof(ph, iostate_t, ph);

    mx_status_t r;
    mxrio_msg_t msg;
    if (signals & MX_CHANNEL_READABLE) {
        if ((r = mxrio_handle_rpc(ph->handle, &msg, devfs_rio_handler, ios)) == MX_OK) {
            return MX_OK;
        }
    } else if (signals & MX_CHANNEL_PEER_CLOSED) {
        mxrio_handle_close(devfs_rio_handler, ios);
        r = MX_ERR_STOP;
    } else {
        printf("dc_rio_handler: invalid signals %x\n", signals);
        exit(0);
    }

    iostate_destroy(ios);
    return r;
}

void devmgr_init(mx_handle_t root_job) {
    printf("devmgr: init\n");

    prepopulate_protocol_dirs();

    root_devnode.device = coordinator_init(root_job);
    root_devnode.device->self = &root_devnode;

    mx_handle_t h0, h1;
    if (mx_channel_create(0, &h0, &h1) != MX_OK) {
        return;
    } else if (iostate_create(&root_devnode, h0) != MX_OK) {
        mx_handle_close(h0);
        mx_handle_close(h1);
        return;
    }
    // set the "fs ready" signal
    mx_object_signal(h1, 0, MX_USER_SIGNAL_0);
    devfs_mount(h1);
}

void devmgr_handle_messages(void) {
    coordinator();
}
