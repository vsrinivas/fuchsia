// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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

static mx_status_t default_open(mx_device_t* dev, uint32_t flags, void** cookie) {
    return NO_ERROR;
}

static mx_status_t default_close(mx_device_t* dev, void* cookie) {
    return NO_ERROR;
}

static mx_status_t default_release(mx_device_t* dev) {
    return ERR_NOT_SUPPORTED;
}

static ssize_t default_read(mx_device_t* dev, void* buf, size_t count,
                            size_t off, void* cookie) {
    return ERR_NOT_SUPPORTED;
}

static ssize_t default_write(mx_device_t* dev, const void* buf, size_t count,
                             size_t off, void* cookie) {
    return ERR_NOT_SUPPORTED;
}

static size_t default_get_size(mx_device_t* dev, void* cookie) {
    return 0;
}

static ssize_t default_ioctl(mx_device_t* dev, uint32_t op,
                             const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, void* cookie) {
    return ERR_NOT_SUPPORTED;
}

static mx_protocol_device_t root_device_proto = {
    .get_protocol = default_get_protocol,
    .open = default_open,
    .close = default_close,
    .release = default_release,
    .read = default_read,
    .write = default_write,
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
    case MX_PROTOCOL_CONSOLE:
        return "console";
    case MX_PROTOCOL_DISPLAY:
        return "display";
    case MX_PROTOCOL_FB:
        return "fb";
    case MX_PROTOCOL_PCI:
        return "pci";
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

    // TODO: use 0, 1, ... naming here
    return devfs_add_link(vnp, dev->name, dev);
#endif
}

static const char* safename(const char* name) {
    return name ? name : "<noname>";
}

static void dev_ref_release(mx_device_t* dev) {
    dev->refcount--;
    if (dev->refcount == 0) {
        printf("device: %p(%s): ref=0. releasing.\n", dev, safename(dev->name));
        mx_handle_close(dev->event);
        if (dev->vnode) {
            devfs_remove(dev->vnode);
            dev->vnode = NULL;
        }
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
    dev->refcount++;
    return NO_ERROR;
}

static mx_status_t devmgr_device_probe(mx_device_t* dev, mx_driver_t* drv) {
    mx_status_t status;

    xprintf("devmgr: probe dev=%p(%s) drv=%p(%s)\n",
            dev, safename(dev->name), drv, safename(drv->name));

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
        dev->refcount++;
        return NO_ERROR;
    }
    if (status < 0) {
        return status;
    }
    dev->owner = &remote_driver;
    dev->refcount++;
    return NO_ERROR;
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
    xprintf("%s: device add: %p(%s) parent=%p(%s)\n", devmgr_is_remote ? "devhost" : "devmgr",
            dev, safename(dev->name), parent, safename(parent->name));

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
    DEFAULT_IF_NULL(ops, get_size);
    DEFAULT_IF_NULL(ops, ioctl);

    // Don't create an event handle if we alredy have one
    if (dev->event == MX_HANDLE_INVALID && (dev->event = mx_event_create(0)) < 0) {
        printf("device add: %p(%s): cannot create event: %d\n",
               dev, safename(dev->name), dev->event);
        return dev->event;
    }

    dev->flags |= DEV_FLAG_BUSY;

    // add to the device tree
    dev->parent = parent;
    dev->parent->refcount++;

    // this is balanced by end of devmgr_device_remove
    dev->refcount++;
    list_add_tail(&parent->children, &dev->node);

    if (devmgr_is_remote) {
        mx_status_t r = devhost_add(dev, parent);
        if (r < 0) {
            printf("devhost: remote add failed %d\n", r);
            dev->refcount--;
            dev->parent->refcount--;
            list_delete(&dev->node);
            dev->flags &= (~DEV_FLAG_BUSY);
            return r;
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
    if (!devmgr_is_remote && (parent->vnode != NULL)) {
        vnode_t* vn;
        if (devfs_add_node(&vn, parent->vnode, dev->name, dev) == NO_ERROR) {
            devmgr_register_with_protocol(dev, dev->protocol_id);
        }
    }
#endif

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

    dev->flags &= (~DEV_FLAG_BUSY);
    return NO_ERROR;
}

mx_status_t devmgr_device_remove(mx_device_t* dev) {
    if (dev->flags & DEV_FLAG_DEAD) {
        printf("device: %p: cannot be removed (already dead)\n", dev);
        return ERR_INVALID_ARGS;
    }
    if (dev->flags & DEV_FLAG_BUSY) {
        printf("device: %p: cannot be removed (busy)\n", dev);
        return ERR_BAD_STATE;
    }
    printf("device: %p: is being removed\n", dev);
    if (!list_is_empty(&dev->children)) {
        printf("device: %p: still has children! now orphaned.\n", dev);
    }
    dev->flags |= DEV_FLAG_DEAD;
    if (dev->parent) {
        list_delete(&dev->node);
        dev_ref_release(dev->parent);
    }
    if (list_in_list(&dev->unode)) {
        list_delete(&dev->unode);
    }
    if (dev->owner) {
        if (dev->owner->ops.unbind) {
            DM_UNLOCK();
            dev->owner->ops.unbind(dev->owner, dev);
            DM_LOCK();
        }
        dev->owner = NULL;
        dev_ref_release(dev);
    }

    // this must be last, since it may result in the device structure being destroyed
    dev_ref_release(dev);

    return NO_ERROR;
}

mx_status_t devmgr_device_open(mx_device_t* dev, uint32_t flags) {
    if (dev->flags & DEV_FLAG_DEAD) {
        printf("device open: %p(%s) is dead!\n", dev, safename(dev->name));
        return ERR_BAD_STATE;
    }
    dev->refcount++;
    mx_status_t r;
    void* cookie = NULL;
    DM_UNLOCK();
    r = dev->ops->open(dev, flags, &cookie);
    DM_LOCK();
    return r;
}

mx_status_t devmgr_device_close(mx_device_t* dev) {
    mx_status_t r;
    DM_UNLOCK();
    r = dev->ops->close(dev, NULL);
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

#if !LIBDRIVER
    if (!devhost) {
        // init devfs
        vnroot = devfs_get_root();
        root_dev->vnode = vnroot;
        devfs_add_node(&vnclass, vnroot, "class", NULL);

        mxio_dispatcher_create(&devmgr_devhost_dispatcher, devmgr_handler);
    }
#endif

    mxio_dispatcher_create(&devmgr_rio_dispatcher, mxio_rio_handler);
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

mx_device_t* devmgr_device_root(void) {
    return root_dev;
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
