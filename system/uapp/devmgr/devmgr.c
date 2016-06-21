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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/driver.h>

#include <mxu/list.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>

#include <mxio/dispatcher.h>
#include <mxio/remoteio.h>

#include <runtime/mutex.h>

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

// The Root Driver
static mx_driver_t root_driver = {
    .name = "devmgr",
};

static mx_driver_t remote_driver = {
    .name = "devhost",
};

// The Root Device
mx_status_t device_base_get_protocol(mx_device_t* dev, uint32_t proto_id, void** proto) {
    if (proto_id == MX_PROTOCOL_DEVICE) {
        *proto = dev->ops;
        return NO_ERROR;
    }
    if ((proto_id == dev->protocol_id) && (dev->protocol_id != 0)) {
        *proto = dev->protocol_ops;
        return NO_ERROR;
    }
    return ERR_NOT_SUPPORTED;
}

mx_status_t root_open(mx_device_t* dev, uint32_t flags) {
    return NO_ERROR;
}

mx_status_t root_close(mx_device_t* dev) {
    return NO_ERROR;
}

mx_status_t root_release(mx_device_t* dev) {
    return ERR_NOT_SUPPORTED;
}

static mx_protocol_device_t root_device_proto = {
    .get_protocol = device_base_get_protocol,
    .open = root_open,
    .close = root_close,
    .release = root_release,
};

static mx_device_t* root_dev = NULL;
static mx_device_t* proto_dev = NULL;

static struct list_node unmatched_device_list = LIST_INITIAL_VALUE(unmatched_device_list);

// TODO maybe organize in a tree structure
static struct list_node driver_list = LIST_INITIAL_VALUE(driver_list);

// handler for messages from device host processes
mxio_dispatcher_t* devmgr_dispatcher;

#define PNMAX 16
static const char* proto_name(uint32_t id, char buf[PNMAX]) {
    switch (id) {
    case MX_PROTOCOL_DEVICE:
        return "device";
    case MX_PROTOCOL_CHAR:
        return "char";
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
        snprintf(buf, PNMAX, "<%08x>", id);
        return buf;
    }
}

struct devmgr_protocol_list_node {
    uint32_t proto_id;
    const char* name;
    struct list_node device_list;
    struct list_node node;
};

static struct list_node device_list_by_protocol = LIST_INITIAL_VALUE(device_list_by_protocol);

extern mx_driver_t* _builtin_drivers;

#define device_is_bound(dev) (!!dev->owner)

static struct list_node* devmgr_get_device_list_by_protocol(uint32_t proto_id) {
    struct devmgr_protocol_list_node* node = NULL;
    struct devmgr_protocol_list_node* protocol = NULL;
    // find the protocol in the list
    list_for_every_entry (&device_list_by_protocol, node, struct devmgr_protocol_list_node, node) {
        if (node->proto_id == proto_id) {
            protocol = node;
            break;
        }
    }
    // if no list is found, create one for the new protocol
    if (!protocol) {
        char tmp[PNMAX];
        protocol = (struct devmgr_protocol_list_node*)malloc(sizeof(struct devmgr_protocol_list_node));
        assert(protocol); // TODO out of memory
        protocol->proto_id = proto_id;
        protocol->name = proto_name(proto_id, tmp);
        list_initialize(&protocol->device_list);
        list_add_tail(&device_list_by_protocol, &protocol->node);

        // create a device to represent this protocol family
        mx_device_t* pdev;
        if (devmgr_device_create(&pdev, &root_driver, protocol->name, &root_device_proto) == NO_ERROR) {
            pdev->flags |= DEV_FLAG_PROTOCOL | DEV_FLAG_UNBINDABLE;
            pdev->protocol_ops = &protocol->device_list;
            devmgr_device_add(pdev, proto_dev);
        }
    }
    return &protocol->device_list;
}

static const char* safename(const char* name) {
    return name ? name : "<noname>";
}

static void dev_ref_release(mx_device_t* dev) {
    dev->refcount--;
    if (dev->refcount == 0) {
        printf("device: %p(%s): ref=0. releasing.\n", dev, safename(dev->name));
        _magenta_handle_close(dev->event);
        DM_UNLOCK();
        dev->ops->release(dev);
        DM_LOCK();
    }
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
        dev->owner = &remote_driver;
        dev->refcount++;
    }
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
    list_initialize(&dev->device_list);
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

mx_status_t devmgr_device_add(mx_device_t* dev, mx_device_t* parent) {
    if (dev == NULL)
        return ERR_INVALID_ARGS;
    if (parent == NULL) {
        if (devmgr_is_remote) {
            //printf("device add: %p(%s): not allowed in devhost\n", dev, safename(dev->name));
            return ERR_NOT_SUPPORTED;
        }
        parent = root_dev;
    }

    if (parent->flags & DEV_FLAG_DEAD) {
        printf("device add: %p: is dead, cannot add child %p\n", parent, dev);
        return ERR_OBJECT_DESTROYED;
    }
    xprintf("%s: device add: %p(%s) parent=%p(%s)\n", devmgr_is_remote ? "devhost" : "devmgr",
            dev, safename(dev->name), parent, safename(parent->name));

    if (dev->ops == NULL) {
        printf("device add: %p(%s): NULL ops\n", dev, safename(dev->name));
        return ERR_INVALID_ARGS;
    }
    if ((dev->ops->get_protocol == NULL) || (dev->ops->open == NULL) ||
        (dev->ops->close == NULL) || (dev->ops->release == NULL)) {
        printf("device add: %p(%s): incomplete ops\n", dev, safename(dev->name));
        return ERR_INVALID_ARGS;
    }

    // Don't create event handle if we alredy have one
    if (dev->event == MX_HANDLE_INVALID && (dev->event = _magenta_event_create(0)) < 0) {
        printf("device add: %p(%s): cannot create event: %d\n",
               dev, safename(dev->name), dev->event);
        return dev->event;
    }

    dev->flags |= DEV_FLAG_BUSY;

    // add to the protocol list
    if (proto_dev && dev->protocol_id) {
        struct list_node* protocol = devmgr_get_device_list_by_protocol(dev->protocol_id);
        list_add_tail(protocol, &dev->pnode);
    }

    // add to the device tree
    dev->parent = parent;
    dev->parent->refcount++;

    // this is balanced by end of devmgr_device_remove
    dev->refcount++;
    list_add_tail(&parent->device_list, &dev->node);

    if (devmgr_is_remote) {
        mx_status_t r = devhost_add(dev, parent);
        if (r < 0) {
            printf("devhost: remote add failed %d\n", r);
            dev->flags &= (~DEV_FLAG_BUSY);
            return r;
        }
    }

    if (dev->flags & DEV_FLAG_REMOTE) {
        xprintf("dev %p is REMOTE\n", dev);
        // for now devhost'd devices are openable but not bindable
        dev->flags |= DEV_FLAG_UNBINDABLE;
    }

    if ((dev->flags & DEV_FLAG_UNBINDABLE) == 0) {
        if (!device_is_bound(dev)) {
            // probe the device with all drivers and initialize if the probe is successful
            mx_driver_t* drv = NULL;
            list_for_every_entry (&driver_list, drv, mx_driver_t, node) {
                if (devmgr_device_probe(dev, drv) == NO_ERROR) {
                    break;
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
    if (!list_is_empty(&dev->device_list)) {
        printf("device: %p: still has children! now orphaned.\n", dev);
    }
    dev->flags |= DEV_FLAG_DEAD;
    if (dev->parent) {
        list_delete(&dev->node);
        dev_ref_release(dev->parent);
    }
    if (list_in_list(&dev->pnode)) {
        list_delete(&dev->pnode);
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
        return ERR_OBJECT_DESTROYED;
    }
    dev->refcount++;
    mx_status_t r;
    DM_UNLOCK();
    r = dev->ops->open(dev, flags);
    DM_LOCK();
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

    if (!devhost) {
        // init a place to hang protocols
        device_create(&proto_dev, &root_driver, "protocol", &root_device_proto);
        proto_dev->flags |= DEV_FLAG_UNBINDABLE;
        device_add(proto_dev, root_dev);
    }

    mxio_dispatcher_create(&devmgr_dispatcher, devhost ? mxio_rio_handler : devmgr_handler);
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

void devmgr_handle_messages(void) {
    mxio_dispatcher_run(devmgr_dispatcher);
}

static void devmgr_dump_device(uint level, mx_device_t* dev) {
    for (uint i = 0; i < level; i++) {
        printf("  ");
    }
    printf("%c %s drv@%p", list_is_empty(&dev->device_list) ? '|' : '+', dev->name, dev->driver);
    if (dev->driver)
        printf(" (%s)", dev->driver->name);
    if (dev->owner)
        printf(" owner: %s", dev->owner->name);
    printf("\n");
}

static void devmgr_dump_recursive(uint level, mx_device_t* _dev) {
    devmgr_dump_device(level, _dev);
    mx_device_t* dev = NULL;
    list_for_every_entry (&_dev->device_list, dev, mx_device_t, node) {
        devmgr_dump_recursive(level + 1, dev);
    }
}

static void devmgr_dump_protocols(void) {
    struct devmgr_protocol_list_node* protocol = NULL;
    list_for_every_entry (&device_list_by_protocol, protocol, struct devmgr_protocol_list_node, node) {
        printf("%s:\n", protocol->name);
        mx_device_t* dev = NULL;
        list_for_every_entry (&protocol->device_list, dev, mx_device_t, pnode) {
            printf("  %s drv@%p\n", dev->name, dev->driver);
        }
    }
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
    if (!strcmp(cmd, "dump")) {
        devmgr_dump();
        return NO_ERROR;
    }
    if (!strcmp(cmd, "crash")) {
        *((int*)0x1234) = 42;
        return NO_ERROR;
    } else {
        return ERR_NOT_SUPPORTED;
    }
}
