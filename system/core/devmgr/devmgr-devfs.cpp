// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devcoordinator.h"
#include "devhost-shared.h"
#include "devmgr.h"
#include "fshost.h"

#include <zircon/fidl.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <zircon/device/vfs.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/string.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/util.h>
#include <lib/fidl/coding.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace devmgr {

struct Watcher : fbl::DoublyLinkedListable<fbl::unique_ptr<Watcher>> {
    Watcher(Devnode* dn, zx::channel ch, uint32_t mask);

    Watcher(const Watcher&) = delete;
    Watcher& operator=(const Watcher&) = delete;

    Watcher(Watcher&&) = delete;
    Watcher& operator=(Watcher&&) = delete;

    Devnode* devnode = nullptr;
    zx::channel handle;
    uint32_t mask = 0;
};

Watcher::Watcher(Devnode* dn, zx::channel ch, uint32_t mask)
    : devnode(dn), handle(fbl::move(ch)), mask(mask) {
}

struct DcIostate {
    DcIostate() = default;

    port_handler_t ph = {};

    // entry in our devnode's iostate list
    fbl::DoublyLinkedListNodeState<DcIostate*> node;
    struct Node {
        static fbl::DoublyLinkedListNodeState<DcIostate*>& node_state(
            DcIostate& obj) {
            return obj.node;
        }
    };

    // pointer to our devnode, nullptr if it has been removed
    Devnode* devnode = nullptr;

    uint64_t readdir_ino = 0;
};

// BUG(ZX-2868): We currently never free these after allocating them
struct Devnode {
    explicit Devnode(fbl::String name);

    Devnode(const Devnode&) = delete;
    Devnode& operator=(const Devnode&) = delete;

    Devnode(Devnode&&) = delete;
    Devnode& operator=(Devnode&&) = delete;

    fbl::String name;
    uint64_t ino = 0;

    // nullptr if we are a pure directory node,
    // otherwise the device we are referencing
    Device* device = nullptr;

    fbl::DoublyLinkedList<fbl::unique_ptr<Watcher>> watchers;

    // entry in our parent devnode's children list
    list_node_t node = {};

    // list of our child devnodes
    list_node_t children;

    // list of attached iostates
    fbl::DoublyLinkedList<DcIostate*, DcIostate::Node> iostate;

    // used to assign unique small device numbers
    // for class device links
    uint32_t seqcount = 0;
};

extern port_t dc_port;

static uint64_t next_ino = 2;

static fbl::unique_ptr<Devnode> root_devnode;

static fbl::unique_ptr<Devnode> class_devnode;

static zx_status_t dc_fidl_handler(port_handler_t* ph, zx_signals_t signals, uint32_t evt);
static fbl::unique_ptr<Devnode> devfs_mkdir(Devnode* parent, const char* name);

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

struct ProtocolInfo {
    const char* name;
    Devnode* devnode;
    uint32_t id;
    uint32_t flags;
};

static ProtocolInfo proto_infos[] = {
#define DDK_PROTOCOL_DEF(tag, val, name, flags) { name, nullptr, val, flags },
#include <ddk/protodefs.h>
};

Devnode::Devnode(fbl::String name)
    : name(fbl::move(name)) {

    list_initialize(&children);
}

static Devnode* proto_dir(uint32_t id) {
    for (const auto& info : proto_infos) {
        if (info.id == id) {
            return info.devnode;
        }
    }
    return nullptr;
}

static void prepopulate_protocol_dirs() {
    class_devnode = devfs_mkdir(root_devnode.get(), "class");
    for (auto& info : proto_infos) {
        if (!(info.flags & PF_NOPUB)) {
            info.devnode = devfs_mkdir(class_devnode.get(), info.name).release();
        }
    }
}

void describe_error(zx_handle_t h, zx_status_t status) {
    zxrio_describe_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.hdr.ordinal = fuchsia_io_NodeOnOpenOrdinal;
    msg.status = status;
    zx_channel_write(h, 0, &msg, sizeof(zxrio_describe_t), nullptr, 0);
    zx_handle_close(h);
}

static zx_status_t iostate_create(Devnode* dn, zx_handle_t h) {
    auto ios = fbl::make_unique<DcIostate>();
    if (ios == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    ios->ph.handle = h;
    ios->ph.waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    ios->ph.func = dc_fidl_handler;
    ios->devnode = dn;
    dn->iostate.push_back(ios.get());

    zx_status_t r;
    if ((r = port_wait(&dc_port, &ios->ph)) != ZX_OK) {
        dn->iostate.erase(*ios);
    } else {
        // dc_port now owns |ios|
        __UNUSED auto ptr = ios.release();
    }
    return r;
}

static void iostate_destroy(DcIostate* ios) {
    if (ios->devnode) {
        ios->devnode->iostate.erase(*ios);
        ios->devnode = nullptr;
    }
    zx_handle_close(ios->ph.handle);
    ios->ph.handle = ZX_HANDLE_INVALID;
    delete ios;
}

// A devnode is a directory (from stat's perspective) if
// it has children, or if it doesn't have a device, or if
// its device has no rpc handle
static bool devnode_is_dir(Devnode* dn) {
    if (list_is_empty(&dn->children)) {
        return (dn->device == nullptr) || (dn->device->hrpc == ZX_HANDLE_INVALID);
    }
    return true;
}

// Local devnodes are ones that we should not hand off OPEN
// RPCs to the underlying devhost
static bool devnode_is_local(Devnode* dn) {
    if (dn->device == nullptr) {
        return true;
    }
    if (dn->device->hrpc == ZX_HANDLE_INVALID) {
        return true;
    }
    if (dn->device->flags & DEV_CTX_MUST_ISOLATE) {
        return true;
    }
    return false;
}

// Notify a single watcher about the given operation and path.  On failure,
// frees the watcher.  This can only be called on a watcher that has not yet
// been added to a Devnode's watchers list.
static void devfs_notify_single(fbl::unique_ptr<Watcher>* watcher,
                                const fbl::String& name, unsigned op) {
    size_t len = name.length();
    if (!*watcher || len > fuchsia_io_MAX_FILENAME) {
        return;
    }

    ZX_ASSERT(!(*watcher)->InContainer());

    uint8_t msg[fuchsia_io_MAX_FILENAME + 2];
    const uint32_t msg_len = static_cast<uint32_t>(len + 2);
    msg[0] = static_cast<uint8_t>(op);
    msg[1] = static_cast<uint8_t>(len);
    memcpy(msg + 2, name.c_str(), len);

    // convert to mask
    op = (1u << op);

    if (!((*watcher)->mask & op)) {
        return;
    }

    if ((*watcher)->handle.write(0, msg, msg_len, nullptr, 0) != ZX_OK) {
        watcher->reset();
    }
}

static void devfs_notify(Devnode* dn, const fbl::String& name, unsigned op) {
    if (dn->watchers.is_empty()) {
        return;
    }

    size_t len = name.length();
    if (len > fuchsia_io_MAX_FILENAME) {
        return;
    }

    uint8_t msg[fuchsia_io_MAX_FILENAME + 2];
    const uint32_t msg_len = static_cast<uint32_t>(len + 2);
    msg[0] = static_cast<uint8_t>(op);
    msg[1] = static_cast<uint8_t>(len);
    memcpy(msg + 2, name.c_str(), len);

    // convert to mask
    op = (1u << op);

    for (auto itr = dn->watchers.begin(); itr != dn->watchers.end(); ) {
        auto& cur = *itr;
        // Advance the iterator now instead of at the end of the loop because we
        // may erase the current element from the list.
        ++itr;

        if (!(cur.mask & op)) {
            continue;
        }

        if (cur.handle.write(0, msg, msg_len, nullptr, 0) != ZX_OK) {
            dn->watchers.erase(cur);
            // The Watcher is free'd here
        }
    }
}

static zx_status_t devfs_watch(Devnode* dn, zx::channel h, uint32_t mask) {
    auto watcher = fbl::make_unique<Watcher>(dn, fbl::move(h), mask);
    if (watcher == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    // If the watcher has asked for all existing entries, send it all of them
    // followed by the end-of-existing marker (IDLE).
    if (mask & fuchsia_io_WATCH_MASK_EXISTING) {
        Devnode* child;
        list_for_every_entry(&dn->children, child, Devnode, node) {
            if (child->device && (child->device->flags & DEV_CTX_INVISIBLE)) {
                continue;
            }
            //TODO: send multiple per write
            devfs_notify_single(&watcher, child->name, fuchsia_io_WATCH_EVENT_EXISTING);
        }
        devfs_notify_single(&watcher, "", fuchsia_io_WATCH_EVENT_IDLE);
    }

    // Watcher may have been freed by devfs_notify_single, so check before
    // adding.
    if (watcher) {
        dn->watchers.push_front(fbl::move(watcher));
    }
    return ZX_OK;
}

static fbl::unique_ptr<Devnode> devfs_mknode(Device* dev, const char* name) {
    auto dn = fbl::make_unique<Devnode>(name);
    if (!dn) {
        return nullptr;
    }
    dn->ino = next_ino++;
    dn->device = dev;
    return dn;
}

static fbl::unique_ptr<Devnode> devfs_mkdir(Devnode* parent, const char* name) {
    fbl::unique_ptr<Devnode> dn = devfs_mknode(nullptr, name);
    if (dn == nullptr) {
        return nullptr;
    }
    list_add_tail(&parent->children, &dn->node);
    return dn;
}

static Devnode* devfs_lookup(Devnode* parent, const char* name) {
    Devnode* child;
    list_for_every_entry(&parent->children, child, Devnode, node) {
        if (!strcmp(name, child->name.c_str())) {
            return child;
        }
    }
    return nullptr;
}

void devfs_advertise(Device* dev) {
    if (dev->link) {
        Devnode* dir = proto_dir(dev->protocol_id);
        devfs_notify(dir, dev->link->name, fuchsia_io_WATCH_EVENT_ADDED);
    }
    if (dev->parent && dev->parent->self) {
        devfs_notify(dev->parent->self, dev->self->name, fuchsia_io_WATCH_EVENT_ADDED);
    }
}

// TODO: generate a MODIFIED event rather than back to back REMOVED and ADDED
void devfs_advertise_modified(Device* dev) {
    if (dev->link) {
        Devnode* dir = proto_dir(dev->protocol_id);
        devfs_notify(dir, dev->link->name, fuchsia_io_WATCH_EVENT_REMOVED);
        devfs_notify(dir, dev->link->name, fuchsia_io_WATCH_EVENT_ADDED);
    }
    if (dev->parent && dev->parent->self) {
        devfs_notify(dev->parent->self, dev->self->name, fuchsia_io_WATCH_EVENT_REMOVED);
        devfs_notify(dev->parent->self, dev->self->name, fuchsia_io_WATCH_EVENT_ADDED);
    }
}

zx_status_t devfs_publish(Device* parent, Device* dev) {
    if ((parent->self == nullptr) || (dev->self != nullptr) || (dev->link != nullptr)) {
        return ZX_ERR_INTERNAL;
    }

    fbl::unique_ptr<Devnode> dnself = devfs_mknode(dev, dev->name);
    if (dnself == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    if ((dev->protocol_id == ZX_PROTOCOL_TEST_PARENT) ||
        (dev->protocol_id == ZX_PROTOCOL_MISC_PARENT) ||
        (dev->protocol_id == ZX_PROTOCOL_MISC)) {
        // misc devices are singletons, not a class
        // in the sense of other device classes.
        // They do not get aliases in /dev/class/misc/...
        // instead they exist only under their parent
        // device.
        goto done;
    }

    // Create link in /dev/class/... if this id has a published class
    Devnode* dir;
    dir = proto_dir(dev->protocol_id);
    if (dir != nullptr) {
        char tmp[32];
        const char* name = dev->name;

        if (dev->protocol_id != ZX_PROTOCOL_CONSOLE) {

            for (unsigned n = 0; n < 1000; n++) {
                snprintf(tmp, sizeof(tmp), "%03u", (dir->seqcount++) % 1000);
                if (devfs_lookup(dir, tmp) == nullptr) {
                    name = tmp;
                    goto got_name;
                }
            }
            return ZX_ERR_ALREADY_EXISTS;
got_name:
            ;
        }

        fbl::unique_ptr<Devnode> dnlink = devfs_mknode(dev, name);
        if (dnlink == nullptr) {
            return ZX_ERR_NO_MEMORY;
        }

        // add link node to class directory
        list_add_tail(&dir->children, &dnlink->node);
        dev->link = dnlink.release();
    }

done:
    // add self node to parent directory
    list_add_tail(&parent->self->children, &dnself->node);
    dev->self = dnself.release();

    if (!(dev->flags & DEV_CTX_INVISIBLE)) {
        devfs_advertise(dev);
    }
    return ZX_OK;
}

static void _devfs_remove(Devnode* dn) {
    if (list_in_list(&dn->node)) {
        list_delete(&dn->node);
    }

    // detach all connected iostates
    for (auto& ios : dn->iostate) {
        ios.devnode = nullptr;
        zx_handle_close(ios.ph.handle);
        ios.ph.handle = ZX_HANDLE_INVALID;
    }

    // notify own file watcher
    if ((dn->device == nullptr) ||
        !(dn->device->flags & DEV_CTX_INVISIBLE)) {
        devfs_notify(dn, "", fuchsia_io_WATCH_EVENT_DELETED);
    }

    // disconnect from device and notify parent/link directory watchers
    if (dn->device != nullptr) {
        if (dn->device->self == dn) {
            dn->device->self = nullptr;

            if ((dn->device->parent != nullptr) &&
                (dn->device->parent->self != nullptr) &&
                !(dn->device->flags & DEV_CTX_INVISIBLE)) {
                devfs_notify(dn->device->parent->self, dn->name, fuchsia_io_WATCH_EVENT_REMOVED);
            }
        }
        if (dn->device->link == dn) {
            dn->device->link = nullptr;

            if (!(dn->device->flags & DEV_CTX_INVISIBLE)) {
                Devnode* dir = proto_dir(dn->device->protocol_id);
                devfs_notify(dir, dn->name, fuchsia_io_WATCH_EVENT_REMOVED);
            }
        }
        dn->device = nullptr;
    }

    // destroy all watchers
    dn->watchers.clear();

    // detach children
    while (list_remove_head(&dn->children) != nullptr) {
        // they will be unpublished when the devices they're
        // associated with are eventually destroyed
    }
}

void devfs_unpublish(Device* dev) {
    if (dev->self != nullptr) {
        _devfs_remove(dev->self);
        dev->self = nullptr;
    }
    if (dev->link != nullptr) {
        _devfs_remove(dev->link);
        dev->link = nullptr;
    }
}

static zx_status_t devfs_walk(Devnode** _dn, char* path, char** pathout) {
    Devnode* dn = *_dn;

again:
    if ((path == nullptr) || (path[0] == 0)) {
        *_dn = dn;
        return ZX_OK;
    }
    char* name = path;
    char* undo = nullptr;
    if ((path = strchr(path, '/')) != nullptr) {
        undo = path;
        *path++ = 0;
    }
    if (name[0] == 0) {
        return ZX_ERR_BAD_PATH;
    }
    Devnode* child;
    list_for_every_entry(&dn->children, child, Devnode, node) {
        if (!strcmp(child->name.c_str(), name)) {
            if(child->device && (child->device->flags & DEV_CTX_INVISIBLE)) {
                continue;
            }
            dn = child;
            goto again;
        }
    }
    if (dn == *_dn) {
        return ZX_ERR_NOT_FOUND;
    }
    if (undo) {
        *undo = '/';
    }
    *_dn = dn;
    *pathout = name;
    return ZX_ERR_NEXT;
}

static void devfs_open(Devnode* dirdn, zx_handle_t h, char* path, uint32_t flags) {
    if (!strcmp(path, ".")) {
        path = nullptr;
    }

    Devnode* dn = dirdn;
    zx_status_t r = devfs_walk(&dn, path, &path);

    bool describe = flags & ZX_FS_FLAG_DESCRIBE;

    bool no_remote = (dn->device == nullptr) || (dn->device->hrpc == ZX_HANDLE_INVALID);
    bool local_required = devnode_is_local(dn);
    bool local_requested = flags & (ZX_FS_FLAG_NOREMOTE | ZX_FS_FLAG_DIRECTORY);

    if (r == ZX_ERR_NEXT) {
        // We only partially matched -- there's more path to walk.
        if (no_remote || local_required) {
            // No remote to pass this on to.
            r = ZX_ERR_NOT_FOUND;
        } else if (local_requested) {
            // Local requested, but this is remote only.
            r = ZX_ERR_NOT_SUPPORTED;
        } else {
            // There is more path to walk, and this node can accept
            // remote requests.
            r = ZX_OK;
        }
    } else {
        path = (char*) ".";
    }

    if (r < 0) {
fail:
        if (describe) {
            describe_error(h, r);
        } else {
            zx_handle_close(h);
        }
        return;
    }

    // If we are a local-only node, or we are asked to not go remote,
    // or we are asked to open-as-a-directory, open locally:
    if (local_requested || local_required) {
        if ((r = iostate_create(dn, h)) < 0) {
            goto fail;
        }
        if (describe) {
            zxrio_describe_t msg;
            memset(&msg, 0, sizeof(msg));
            msg.hdr.ordinal = fuchsia_io_NodeOnOpenOrdinal;
            msg.status = ZX_OK;
            msg.extra_ptr = (zxrio_node_info_t*)FIDL_ALLOC_PRESENT;
            msg.extra.tag = fuchsia_io_NodeInfoTag_directory;
            zx_channel_write(h, 0, &msg, sizeof(zxrio_describe_t), nullptr, 0);
        }
        return;
    }

    // Otherwise we will pass the request on to the remote.
    fuchsia_io_DirectoryOpen(dn->device->hrpc, flags, 0, path, strlen(path), h);
}

// Double-check that Open (the only message we forward)
// cannot be mistaken for an internal dev coordinator RPC message
static_assert((fuchsia_io_DirectoryOpenOrdinal &
               static_cast<uint32_t>(Message::Op::kIdBit)) == 0, "");

static zx_status_t fill_dirent(vdirent_t* de, size_t delen, uint64_t ino,
                               const fbl::String& name, uint8_t type) {
    size_t len = name.length();
    size_t sz = sizeof(vdirent_t) + len;

    if (sz > delen || len > NAME_MAX) {
        return ZX_ERR_INVALID_ARGS;
    }
    de->ino = ino;
    de->size = static_cast<uint8_t>(len);
    de->type = type;
    memcpy(de->name, name.c_str(), len);
    return static_cast<zx_status_t>(sz);
}

static zx_status_t devfs_readdir(Devnode* dn, uint64_t* _ino, void* data, size_t len) {
    char* ptr = static_cast<char*>(data);
    uint64_t ino = *_ino;

    Devnode* child;
    list_for_every_entry(&dn->children, child, Devnode, node) {
        if (child->ino <= ino) {
            continue;
        }
        if (child->device == nullptr) {
            // "pure" directories (like /dev/class/$NAME) do not show up
            // if they have no children, to avoid clutter and confusion.
            // They remain openable, so they can be watched.
            if (list_is_empty(&child->children)) {
                continue;
            }
        } else {
            // invisible devices also do not show up
            if (child->device->flags & DEV_CTX_INVISIBLE) {
                continue;
            }
        }
        ino = child->ino;
        auto vdirent = reinterpret_cast<vdirent_t*>(ptr);
        zx_status_t r = fill_dirent(vdirent, len, ino, child->name,
                                    VTYPE_TO_DTYPE(V_TYPE_DIR));
        if (r < 0) {
            break;
        }
        ptr += r;
        len -= r;
    }

    *_ino = ino;
    return static_cast<zx_status_t>(ptr - static_cast<char*>(data));
}

// Helper macros for |devfs_fidl_handler| which make it easier
// avoid typing generated names.

// Decode the incoming request, returning an error and consuming
// all handles on error.
#define DECODE_REQUEST(MSG, METHOD)                                     \
    do {                                                                \
        zx_status_t r;                                                  \
        if ((r = fidl_decode_msg(&fuchsia_io_ ## METHOD ##RequestTable, \
                                 msg, nullptr)) != ZX_OK) {                \
            return r;                                                   \
        }                                                               \
    } while(0);

// Define a variable |request| from the incoming method, of
// the requested type.
#define DEFINE_REQUEST(MSG, METHOD)                     \
    fuchsia_io_ ## METHOD ## Request* request =         \
        (fuchsia_io_ ## METHOD ## Request*) MSG->bytes;


static zx_status_t devfs_fidl_handler(fidl_msg_t* msg, fidl_txn_t* txn, void* cookie) {
    auto ios = static_cast<DcIostate*>(cookie);
    Devnode* dn = ios->devnode;
    if (dn == nullptr) {
        return ZX_ERR_PEER_CLOSED;
    }

    auto hdr = static_cast<fidl_message_header_t*>(msg->bytes);

    zx_status_t r;
    switch (hdr->ordinal) {
    case fuchsia_io_NodeCloneOrdinal: {
        DECODE_REQUEST(msg, NodeClone);
        DEFINE_REQUEST(msg, NodeClone);
        zx_handle_t h = request->object;
        uint32_t flags = request->flags;
        char path[PATH_MAX];
        path[0] = '\0';
        devfs_open(dn, h, path, flags | ZX_FS_FLAG_NOREMOTE);
        return ZX_OK;
    }
    case fuchsia_io_NodeDescribeOrdinal: {
        DECODE_REQUEST(msg, NodeDescribe);
        fuchsia_io_NodeInfo info;
        memset(&info, 0, sizeof(info));
        info.tag = fuchsia_io_NodeInfoTag_directory;
        return fuchsia_io_NodeDescribe_reply(txn, &info);
    }
    case fuchsia_io_DirectoryOpenOrdinal: {
        DECODE_REQUEST(msg, DirectoryOpen);
        DEFINE_REQUEST(msg, DirectoryOpen);
        uint32_t len = static_cast<uint32_t>(request->path.size);
        char* path = request->path.data;
        zx_handle_t h = request->object;
        uint32_t flags = request->flags;
        if ((len < 1) || (len > 1024)) {
            zx_handle_close(h);
        } else {
            path[len] = '\0';
            devfs_open(dn, h, path, flags);
        }
        return ZX_OK;
    }
    case fuchsia_io_NodeGetAttrOrdinal: {
        DECODE_REQUEST(msg, NodeGetAttr);
        uint32_t mode;
        if (devnode_is_dir(dn)) {
            mode = V_TYPE_DIR | V_IRUSR | V_IWUSR;
        } else {
            mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
        }

        fuchsia_io_NodeAttributes attributes;
        memset(&attributes, 0, sizeof(attributes));
        attributes.mode = mode;
        attributes.content_size = 0;
        attributes.link_count = 1;
        attributes.id = dn->ino;
        return fuchsia_io_NodeGetAttr_reply(txn, ZX_OK, &attributes);
    }
    case fuchsia_io_DirectoryRewindOrdinal: {
        DECODE_REQUEST(msg, DirectoryRewind);
        ios->readdir_ino = 0;
        return fuchsia_io_DirectoryRewind_reply(txn, ZX_OK);
    }
    case fuchsia_io_DirectoryReadDirentsOrdinal: {
        DECODE_REQUEST(msg, DirectoryReadDirents);
        DEFINE_REQUEST(msg, DirectoryReadDirents);

        if (request->max_bytes > ZXFIDL_MAX_MSG_BYTES) {
            return fuchsia_io_DirectoryReadDirents_reply(txn, ZX_ERR_INVALID_ARGS, nullptr, 0);
        }

        uint8_t data[request->max_bytes];
        size_t actual = 0;
        r = devfs_readdir(dn, &ios->readdir_ino, data, request->max_bytes);
        if (r >= 0) {
            actual = r;
            r = ZX_OK;
        }
        return fuchsia_io_DirectoryReadDirents_reply(txn, r, data, actual);
    }
    case fuchsia_io_DirectoryWatchOrdinal: {
        DECODE_REQUEST(msg, DirectoryWatch);
        DEFINE_REQUEST(msg, DirectoryWatch);
        zx::channel watcher(request->watcher);

        request->watcher = ZX_HANDLE_INVALID;
        if (request->mask & (~fuchsia_io_WATCH_MASK_ALL) || request->options != 0) {
            return fuchsia_io_DirectoryWatch_reply(txn, ZX_ERR_INVALID_ARGS);
        }
        r = devfs_watch(dn, fbl::move(watcher), request->mask);
        return fuchsia_io_DirectoryWatch_reply(txn, r);
    }
    case fuchsia_io_DirectoryAdminQueryFilesystemOrdinal: {
        DECODE_REQUEST(msg, DirectoryAdminQueryFilesystem);
        fuchsia_io_FilesystemInfo info;
        memset(&info, 0, sizeof(info));
        strlcpy((char*) info.name, "devfs", fuchsia_io_MAX_FS_NAME_BUFFER);
        return fuchsia_io_DirectoryAdminQueryFilesystem_reply(txn, ZX_OK, &info);
    }
    case fuchsia_io_NodeIoctlOrdinal: {
        DECODE_REQUEST(msg, NodeIoctl);
        zx_handle_close_many(msg->handles, msg->num_handles);
        return fuchsia_io_NodeIoctl_reply(txn, ZX_ERR_NOT_SUPPORTED, nullptr, 0, nullptr, 0);
    }
    }

    // close inbound handles so they do not leak
    zx_handle_close_many(msg->handles, msg->num_handles);
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t dc_fidl_handler(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
    DcIostate* ios = containerof(ph, DcIostate, ph);

    zx_status_t r;
    if (signals & ZX_CHANNEL_READABLE) {
        if ((r = zxfidl_handler(ph->handle, devfs_fidl_handler, ios)) == ZX_OK) {
            return ZX_OK;
        }
    } else if (signals & ZX_CHANNEL_PEER_CLOSED) {
        zxfidl_handler(ZX_HANDLE_INVALID, devfs_fidl_handler, ios);
        r = ZX_ERR_STOP;
    } else {
        printf("dc_fidl_handler: invalid signals %x\n", signals);
        exit(0);
    }

    iostate_destroy(ios);
    return r;
}

static zx::channel g_devfs_root;

zx::unowned_channel devfs_root_borrow() {
    return zx::unowned_channel(g_devfs_root);
}

zx::channel devfs_root_clone() {
    return zx::channel(fdio_service_clone(g_devfs_root.get()));
}

void devfs_init(const zx::job& root_job) {
    printf("devmgr: init\n");

    root_devnode = fbl::make_unique<Devnode>("");
    if (!root_devnode) {
        printf("devmgr: failed to allocate devfs root node\n");
        return;
    }
    root_devnode->ino = 1;

    prepopulate_protocol_dirs();

    root_devnode->device = coordinator_init(root_job);
    root_devnode->device->self = root_devnode.get();

    zx::channel h0, h1;
    if (zx::channel::create(0, &h0, &h1) != ZX_OK) {
        return;
    } else if (iostate_create(root_devnode.get(), h0.release()) != ZX_OK) {
        return;
    }

    g_devfs_root = fbl::move(h1);
}

} // namespace devmgr
