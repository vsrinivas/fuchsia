// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devmgr.h"
#include "vfs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/driver.h>

#include <system/listnode.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>

#include <mxio/dispatcher.h>
#include <mxio/remoteio.h>

#include <runtime/mutex.h>
#include <runtime/thread.h>

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

#define TRACE_ADD_REMOVE 0

// if true this is a devhost, not the actual devmgr
// and devhost_handle is the rpc link to the real devmgr
bool devmgr_is_remote = false;
bool __dm_locked = false;
mx_handle_t devhost_handle;

mxr_mutex_t __devmgr_api_lock = MXR_MUTEX_INIT;

#if !LIBDRIVER
// vnodes for root driver and protocols
static vnode_t* vnroot;
static vnode_t* vnclass;
#endif

// The Root Driver
static mx_driver_t root_driver = {
    .name = "devmgr",
};

static mx_driver_t remote_driver = {
    .name = "devhost",
};

// The Root Device
static mx_status_t default_get_protocol(mx_device_t* dev, uint32_t proto_id, void** proto) {
    if (proto_id == MX_PROTOCOL_DEVICE) {
        *proto = dev->ops;
        return NO_ERROR;
    }
    if ((proto_id == dev->protocol_id) && (dev->protocol_ops != NULL)) {
        *proto = dev->protocol_ops;
        return NO_ERROR;
    }
    return ERR_NOT_SUPPORTED;
}

static mx_status_t default_open(mx_device_t* dev, mx_device_t** out, uint32_t flags) {
    return NO_ERROR;
}

static mx_status_t default_close(mx_device_t* dev) {
    return NO_ERROR;
}

static mx_status_t default_release(mx_device_t* dev) {
    return ERR_NOT_SUPPORTED;
}

static ssize_t default_read(mx_device_t* dev, void* buf, size_t count, mx_off_t off) {
    return ERR_NOT_SUPPORTED;
}

static ssize_t default_write(mx_device_t* dev, const void* buf, size_t count, mx_off_t off) {
    return ERR_NOT_SUPPORTED;
}

static void default_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    ssize_t rc;
    void* buf;
    txn->ops->mmap(txn, &buf);
    if (txn->opcode == IOTXN_OP_READ) {
        rc = dev->ops->read(dev, buf, txn->length, txn->offset);
    } else if (txn->opcode == IOTXN_OP_WRITE) {
        rc = dev->ops->write(dev, buf, txn->length, txn->offset);
    } else {
        rc = ERR_NOT_SUPPORTED;
    }
    if (rc < 0) {
        txn->ops->complete(txn, rc, 0);
    } else {
        txn->ops->complete(txn, NO_ERROR, rc);
    }
}

static mx_off_t default_get_size(mx_device_t* dev) {
    return 0;
}

static ssize_t default_ioctl(mx_device_t* dev, uint32_t op,
                             const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len) {
    return ERR_NOT_SUPPORTED;
}

static mx_protocol_device_t root_device_proto = {
    .get_protocol = default_get_protocol,
    .open = default_open,
    .close = default_close,
    .release = default_release,
    .read = default_read,
    .write = default_write,
    .iotxn_queue = default_iotxn_queue,
    .get_size = default_get_size,
    .ioctl = default_ioctl,
};

static mx_device_t* root_dev = NULL;

static struct list_node unmatched_device_list = LIST_INITIAL_VALUE(unmatched_device_list);

// TODO maybe organize in a tree structure
static struct list_node driver_list = LIST_INITIAL_VALUE(driver_list);

// handler for messages from device host processes
mxio_dispatcher_t* devmgr_devhost_dispatcher;

// handler for messages to device
mxio_dispatcher_t* devmgr_rio_dispatcher;

#define device_is_bound(dev) (!!dev->owner)

#define PNMAX 16
static const char* proto_name(uint32_t id, char buf[PNMAX]) {
    switch (id) {
    case MX_PROTOCOL_DEVICE:
        return "device";
    case MX_PROTOCOL_MISC:
        return "misc";
    case MX_PROTOCOL_BLOCK:
        return "block";
    case MX_PROTOCOL_CONSOLE:
        return "console";
    case MX_PROTOCOL_DISPLAY:
        return "display";
    case MX_PROTOCOL_INPUT:
        return "input";
    case MX_PROTOCOL_PCI:
        return "pci";
    case MX_PROTOCOL_SATA:
        return "sata";
    case MX_PROTOCOL_USB_DEVICE:
        return "usb-device";
    case MX_PROTOCOL_USB_HCI:
        return "usb-hci";
    case MX_PROTOCOL_USB_BUS:
        return "usb-bus";
    case MX_PROTOCOL_USB_HUB:
        return "usb-hub";
    case MX_PROTOCOL_ETHERNET:
        return "ethernet";
    case MX_PROTOCOL_BLUETOOTH_HCI:
        return "bluetooth-hci";
    case MX_PROTOCOL_TPM:
        return "tpm";
    default:
        snprintf(buf, PNMAX, "proto-%08x", id);
        return buf;
    }
}

static mx_status_t devmgr_register_with_protocol(mx_device_t* dev, uint32_t proto_id) {
#if LIBDRIVER
    return 0;
#else
    char buf[PNMAX];
    const char* pname = proto_name(proto_id, buf);

    // find or create a vnode for class/<pname>
    vnode_t* vnp;
    mx_status_t r;
    if ((r = devfs_add_node(&vnp, vnclass, pname, NULL)) < 0) {
        return r;
    }

    const char* name = dev->name;
    if ((proto_id != MX_PROTOCOL_MISC) && (proto_id != MX_PROTOCOL_CONSOLE)) {
        // request a numeric name
        name = NULL;
    }

    return devfs_add_link(vnp, name, dev);
#endif
}

static const char* safename(const char* name) {
    return name ? name : "<noname>";
}

void dev_ref_release(mx_device_t* dev) {
    dev->refcount--;
    if (dev->refcount == 0) {
        if (dev->flags & DEV_FLAG_INSTANCE) {
            // these don't get removed, so mark dead state here
            dev->flags |= DEV_FLAG_DEAD | DEV_FLAG_VERY_DEAD;
        }
        if (dev->flags & DEV_FLAG_BUSY) {
            // this can happen if creation fails
            // the caller to device_add() will free it
            printf("device: %p(%s): ref=0, busy, not releasing\n", dev, safename(dev->name));
            return;
        }
#if TRACE_ADD_REMOVE
        printf("device: %p(%s): ref=0. releasing.\n", dev, safename(dev->name));
#endif

        if (!(dev->flags & DEV_FLAG_VERY_DEAD)) {
            printf("device: %p: only mostly dead (this is bad)\n", dev);
        }
        if (!list_is_empty(&dev->children)) {
            printf("device: %p: still has children! not good.\n", dev);
        }

        mx_handle_close(dev->event);
        DM_UNLOCK();
        dev->ops->release(dev);
        DM_LOCK();
    }
}

static mx_status_t devmgr_driver_probe(mx_device_t* dev) {
    mx_status_t status;

    if ((status = devmgr_host_process(dev, NULL)) < 0) {
        return status;
    }
    dev->owner = &remote_driver;
    dev_ref_acquire(dev);
    return NO_ERROR;
}

static mx_status_t devmgr_device_probe(mx_device_t* dev, mx_driver_t* drv) {
    mx_status_t status;

    xprintf("devmgr: probe dev=%p(%s) drv=%p(%s)\n",
            dev, safename(dev->name), drv, safename(drv->name));

    // don't bind to the driver that published this device
    if (drv == dev->driver) {
        return ERR_NOT_SUPPORTED;
    }

    // evaluate the driver's binding program against the device's properties
    if (!devmgr_is_bindable(drv, dev)) {
        return ERR_NOT_SUPPORTED;
    }

    // Determine if we should remote-host this driver
    if ((status = devmgr_host_process(dev, drv)) == ERR_NOT_SUPPORTED) {
        DM_UNLOCK();
        status = drv->ops.bind(drv, dev);
        DM_LOCK();
        if (status < 0) {
            return status;
        }
        dev->owner = drv;
        dev_ref_acquire(dev);
        return NO_ERROR;
    }
    if (status < 0) {
        return status;
    }
    if (list_in_list(&dev->unode)) {
        list_delete(&dev->unode);
    }
    dev->owner = &remote_driver;
    dev_ref_acquire(dev);
    return NO_ERROR;
}

static void devmgr_device_probe_all(mx_device_t* dev) {
    if ((dev->flags & DEV_FLAG_UNBINDABLE) == 0) {
        if (!device_is_bound(dev)) {
            // first, look for a specific driver binary for this device
            if (devmgr_driver_probe(dev) < 0) {
                // if not found, probe all built-in drivers
                mx_driver_t* drv = NULL;
                list_for_every_entry (&driver_list, drv, mx_driver_t, node) {
                    if (devmgr_device_probe(dev, drv) == NO_ERROR) {
                        break;
                    }
                }
            }
        }

        // if no driver is bound, add the device to the unmatched list
        if (!device_is_bound(dev)) {
            list_add_tail(&unmatched_device_list, &dev->unode);
        }
    }
}

mx_status_t devmgr_device_init(mx_device_t* dev, mx_driver_t* driver,
                               const char* name, mx_protocol_device_t* ops) {
    xprintf("devmgr: init '%s' drv=%p, ops=%p\n", safename(name), driver, ops);

    if (name == NULL)
        return ERR_INVALID_ARGS;
    if (strlen(name) > MX_DEVICE_NAME_MAX)
        return ERR_INVALID_ARGS;

    memset(dev, 0, sizeof(mx_device_t));
    strncpy(dev->namedata, name, MX_DEVICE_NAME_MAX);
    dev->magic = MX_DEVICE_MAGIC;
    dev->name = dev->namedata;
    dev->ops = ops;
    dev->driver = driver;
    list_initialize(&dev->children);
    return NO_ERROR;
}

mx_status_t devmgr_device_create(mx_device_t** out, mx_driver_t* driver,
                                 const char* name, mx_protocol_device_t* ops) {
    mx_device_t* dev = malloc(sizeof(mx_device_t));
    if (dev == NULL)
        return ERR_NO_MEMORY;
    mx_status_t status = devmgr_device_init(dev, driver, name, ops);
    if (status) {
        free(dev);
    } else {
        *out = dev;
    }
    return status;
}

void devmgr_device_set_bindable(mx_device_t* dev, bool bindable) {
    if (bindable) {
        dev->flags &= ~DEV_FLAG_UNBINDABLE;
    } else {
        dev->flags |= DEV_FLAG_UNBINDABLE;
    }
}

#define DEFAULT_IF_NULL(ops,method) \
    if (ops->method == NULL) { \
        ops->method = default_##method; \
    }

mx_status_t devmgr_device_add(mx_device_t* dev, mx_device_t* parent) {
    if (dev == NULL) {
        return ERR_INVALID_ARGS;
    }
    if (parent == NULL) {
        if (devmgr_is_remote) {
            //printf("device add: %p(%s): not allowed in devhost\n", dev, safename(dev->name));
            return ERR_NOT_SUPPORTED;
        }
        parent = root_dev;
    }

    if (parent->flags & DEV_FLAG_DEAD) {
        printf("device add: %p: is dead, cannot add child %p\n", parent, dev);
        return ERR_BAD_STATE;
    }
#if TRACE_ADD_REMOVE
    printf("%s: device add: %p(%s) parent=%p(%s)\n", devmgr_is_remote ? "devhost" : "devmgr",
            dev, safename(dev->name), parent, safename(parent->name));
#endif

    if (dev->ops == NULL) {
        printf("device add: %p(%s): NULL ops\n", dev, safename(dev->name));
        return ERR_INVALID_ARGS;
    }

    // install default methods if needed
    mx_protocol_device_t* ops = dev->ops;
    DEFAULT_IF_NULL(ops, get_protocol)
    DEFAULT_IF_NULL(ops, open);
    DEFAULT_IF_NULL(ops, close);
    DEFAULT_IF_NULL(ops, release);
    DEFAULT_IF_NULL(ops, read);
    DEFAULT_IF_NULL(ops, write);
    DEFAULT_IF_NULL(ops, iotxn_queue);
    DEFAULT_IF_NULL(ops, get_size);
    DEFAULT_IF_NULL(ops, ioctl);

    // Don't create an event handle if we alredy have one
    if (dev->event == MX_HANDLE_INVALID && (dev->event = mx_event_create(0)) < 0) {
        printf("device add: %p(%s): cannot create event: %d\n",
               dev, safename(dev->name), dev->event);
       return dev->event;
    }

    dev->flags |= DEV_FLAG_BUSY;

    // this is balanced by end of devmgr_device_remove
    // or, for instanced devices, by the last close
    dev_ref_acquire(dev);

    if (!(dev->flags & DEV_FLAG_INSTANCE)) {
        // add to the device tree
        dev_ref_acquire(parent);
        dev->parent = parent;
        list_add_tail(&parent->children, &dev->node);

        if (devmgr_is_remote) {
            mx_status_t r = devhost_add(dev, parent);
            if (r < 0) {
                printf("devhost: remote add failed %d\n", r);
                dev_ref_release(dev->parent);
                dev->parent = NULL;
                dev_ref_release(dev);
                list_delete(&dev->node);
                dev->flags &= (~DEV_FLAG_BUSY);
                return r;
            }
        }
    }

    if (dev->flags & DEV_FLAG_REMOTE) {
        xprintf("dev %p is REMOTE\n", dev);
        // for now devhost'd devices are openable but not bindable
        dev->flags |= DEV_FLAG_UNBINDABLE;
    }

#if !LIBDRIVER
    // devices which do not declare a primary protocol
    // are implied to be misc devices
    if (dev->protocol_id == 0) {
        dev->protocol_id = MX_PROTOCOL_MISC;
    }

    // add device to devfs
    // unless we're remote... or its parent is not in devfs... or it's an instance
    if (!devmgr_is_remote && (parent->vnode != NULL) && !(dev->flags & DEV_FLAG_INSTANCE)) {
        vnode_t* vn;
        if (devfs_add_node(&vn, parent->vnode, dev->name, dev) == NO_ERROR) {
            devmgr_register_with_protocol(dev, dev->protocol_id);
        }
    }
#endif

    // probe the device
    devmgr_device_probe_all(dev);

    dev->flags &= (~DEV_FLAG_BUSY);
    return NO_ERROR;
}

static const char* removal_problem(uint32_t flags) {
    if (flags & DEV_FLAG_DEAD) {
        return "already dead";
    }
    if (flags & DEV_FLAG_BUSY) {
        return "being created";
    }
    if (flags & DEV_FLAG_INSTANCE) {
        return "ephemeral device";
    }
    return "?";
}

mx_status_t devmgr_device_remove(mx_device_t* dev) {
    if (dev->flags & (DEV_FLAG_DEAD | DEV_FLAG_BUSY | DEV_FLAG_INSTANCE)) {
        printf("device: %p(%s): cannot be removed (%s)\n",
               dev, safename(dev->name), removal_problem(dev->flags));
        return ERR_INVALID_ARGS;
    }
#if TRACE_ADD_REMOVE
    printf("device: %p(%s): is being removed\n", dev, safename(dev->name));
#endif
    dev->flags |= DEV_FLAG_DEAD;

    // remove entry from vfs to avoid any further open() attempts
#if !LIBDRIVER
    if (dev->vnode) {
        xprintf("device: %p: removing vnode\n", dev);
        devfs_remove(dev->vnode);
        dev->vnode = NULL;
    }
#endif

    // detach from parent, downref parent
    if (dev->parent) {
        list_delete(&dev->node);
        dev_ref_release(dev->parent);
    }

    // remove from list of unbound devices, if on that list
    if (list_in_list(&dev->unode)) {
        list_delete(&dev->unode);
    }

    // detach from owner, call unbind(), downref on behalf of owner
    if (dev->owner) {
        if (dev->owner->ops.unbind) {
            DM_UNLOCK();
            dev->owner->ops.unbind(dev->owner, dev);
            DM_LOCK();
        }
        dev->owner = NULL;
        dev_ref_release(dev);
    }

    if (devmgr_is_remote) {
        xprintf("device: %p: devhost->devmgr remove rpc\n", dev);
        devhost_remove(dev);
    }
    dev->flags |= DEV_FLAG_VERY_DEAD;

    // this must be last, since it may result in the device structure being destroyed
    dev_ref_release(dev);

    return NO_ERROR;
}

mx_status_t devmgr_device_rebind(mx_device_t* dev) {
    dev->flags |= DEV_FLAG_REBIND;

    // remove children
    mx_device_t* child = NULL;
    mx_device_t* temp = NULL;
    list_for_every_entry_safe(&dev->children, child, temp, mx_device_t, node) {
        devmgr_device_remove(child);
    }

    // detach from owner and call unbind, downref
    if (dev->owner) {
        if (dev->owner->ops.unbind) {
            DM_UNLOCK();
            dev->owner->ops.unbind(dev->owner, dev);
            DM_LOCK();
        }
        dev->owner = NULL;
        dev_ref_release(dev);
    }

    // probe the device again to bind
    devmgr_device_probe_all(dev);

    dev->flags &= ~DEV_FLAG_REBIND;
    return NO_ERROR;
}

mx_status_t devmgr_device_open(mx_device_t* dev, mx_device_t** out, uint32_t flags) {
    if (dev->flags & DEV_FLAG_DEAD) {
        printf("device open: %p(%s) is dead!\n", dev, safename(dev->name));
        return ERR_BAD_STATE;
    }
    dev_ref_acquire(dev);
    mx_status_t r;
    DM_UNLOCK();
    *out = dev;
    r = dev->ops->open(dev, out, flags);
    DM_LOCK();
    if (*out != dev) {
        // open created a per-instance device for us
        dev_ref_release(dev);

        dev = *out;
        if (!(dev->flags & DEV_FLAG_INSTANCE)) {
            printf("device open: %p(%s) in bad state %x\n", dev, safename(dev->name), flags);
            panic();
        }
    }
    return r;
}

mx_status_t devmgr_device_close(mx_device_t* dev) {
    mx_status_t r;
    DM_UNLOCK();
    r = dev->ops->close(dev);
    DM_LOCK();
    dev_ref_release(dev);
    return r;
}

mx_status_t devmgr_driver_add(mx_driver_t* drv) {
    xprintf("driver add: %p(%s)\n", drv, safename(drv->name));

    if (drv->ops.init) {
        mx_status_t r;
        DM_UNLOCK();
        r = drv->ops.init(drv);
        DM_LOCK();
        if (r < 0)
            return r;
    }

    // add the driver to the driver list
    list_add_tail(&driver_list, &drv->node);

    // probe unmatched devices with the driver and initialize if the probe is successful
    mx_device_t* dev = NULL;
    mx_device_t* temp = NULL;
    list_for_every_entry_safe (&unmatched_device_list, dev, temp, mx_device_t, unode) {
        if (devmgr_device_probe(dev, drv) == NO_ERROR) {
            break;
        }
    }
    return NO_ERROR;
}

mx_status_t devmgr_driver_remove(mx_driver_t* drv) {
    // TODO: implement
    return ERR_NOT_SUPPORTED;
}

extern mx_driver_t __start_builtin_drivers[] __WEAK;
extern mx_driver_t __stop_builtin_drivers[] __WEAK;

void devmgr_init(bool devhost) {
    xprintf("devmgr: init\n");

    devmgr_is_remote = devhost;

    // init device tree
    device_create(&root_dev, &root_driver, "root", &root_device_proto);
    dev_ref_acquire(root_dev);

#if !LIBDRIVER
    if (!devhost) {
        // init devfs
        vnroot = devfs_get_root();
        root_dev->vnode = vnroot;
        devfs_add_node(&vnclass, vnroot, "class", NULL);

        mxio_dispatcher_create(&devmgr_devhost_dispatcher, devmgr_handler);
    }
#endif

    mxio_dispatcher_create(&devmgr_rio_dispatcher, mxrio_handler);
}

void devmgr_init_builtin_drivers(void) {
    mx_driver_t* drv;
    for (drv = __start_builtin_drivers; drv < __stop_builtin_drivers; drv++) {
        if (devmgr_is_remote) {
            if (drv->binding_size == 0) {
                // root-level devices not loaded on devhost instances
                continue;
            }
        }
        driver_add(drv);
    }
}

static int devhost_dispatcher_thread(void* arg) {
    mxio_dispatcher_run(devmgr_devhost_dispatcher);
    return 0;
}

void devmgr_handle_messages(void) {
    if (devmgr_devhost_dispatcher) {
        mxr_thread_t* t;
        mxr_thread_create(devhost_dispatcher_thread, NULL, "devhost-dispatcher", &t);
    }
    mxio_dispatcher_run(devmgr_rio_dispatcher);
}

mx_device_t* devmgr_device_root(void) {
    return root_dev;
}

#if !LIBDRIVER
static void devmgr_dump_device(unsigned level, mx_device_t* dev) {
    for (unsigned i = 0; i < level; i++) {
        printf("  ");
    }
    printf("%c %s drv@%p", list_is_empty(&dev->children) ? '|' : '+', dev->name, dev->driver);
    if (dev->driver)
        printf(" (%s)", dev->driver->name);
    if (dev->owner)
        printf(" owner: %s", dev->owner->name);
    printf("\n");
}

static void devmgr_dump_recursive(unsigned level, mx_device_t* _dev) {
    devmgr_dump_device(level, _dev);
    mx_device_t* dev = NULL;
    list_for_every_entry (&_dev->children, dev, mx_device_t, node) {
        devmgr_dump_recursive(level + 1, dev);
    }
}

static void devmgr_dump_protocols(void) {
// TODO: walk devfs for this
#if 0
    //XXX walk proto nodes
    struct devmgr_protocol_list_node* protocol = NULL;
    list_for_every_entry (&device_list_by_protocol, protocol, struct devmgr_protocol_list_node, node) {
        printf("%s:\n", protocol->name);
        mx_device_t* dev = NULL;
        list_for_every_entry (&protocol->device_list, dev, mx_device_t, pnode) {
            printf("  %s drv@%p\n", dev->name, dev->driver);
        }
    }
#endif
}

void devmgr_dump(void) {
    mx_device_t* dev = NULL;
    DM_LOCK();
    printf("---- Device Tree ----\n");
    devmgr_dump_recursive(0, root_dev);
    printf("---- End Device Tree ----\n");
    printf("\n");
    printf("---- Protocols ----\n");
    devmgr_dump_protocols();
    printf("---- End Protocols ----\n");
    printf("\n");
    printf("---- Unmatched Devices -----\n");
    list_for_every_entry (&unmatched_device_list, dev, mx_device_t, unode) {
        if (!dev->owner) {
            devmgr_dump_device(0, dev);
        }
    }
    printf("---- End Unmatched Devices ----\n");
    printf("\n");
    mx_driver_t* drv = NULL;
    printf("---- Driver List ----\n");
    list_for_every_entry (&driver_list, drv, mx_driver_t, node) {
        printf("%s\n", drv->name);
    }
    printf("---- End Driver List ----\n");
    DM_UNLOCK();
}

mx_status_t devmgr_control(const char* cmd) {
    if (!strcmp(cmd, "help")) {
        printf("dump   - dump device tree\n"
               "lsof   - list open remoteio files and devices\n"
               "crash  - crash the device manager\n"
               );
        return NO_ERROR;
    }
    if (!strcmp(cmd, "dump")) {
        devmgr_dump();
        return NO_ERROR;
    }
    if (!strcmp(cmd, "lsof")) {
        vfs_dump_handles();
        return NO_ERROR;
    }
    if (!strcmp(cmd, "crash")) {
        *((int*)0x1234) = 42;
        return NO_ERROR;
    } else {
        return ERR_NOT_SUPPORTED;
    }
}
#endif
