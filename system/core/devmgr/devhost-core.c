// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpi.h"
#include "devhost.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/device.h>
#include <ddk/driver.h>

#include <magenta/listnode.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

#include <mxio/dispatcher.h>
#include <mxio/remoteio.h>

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

#define TRACE_ADD_REMOVE 0

#define BOOT_FIRMWARE_DIR "/boot/lib/firmware"
#define SYSTEM_FIRMWARE_DIR "/system/lib/firmware"

bool __dm_locked = false;
mtx_t __devhost_api_lock = MTX_INIT;

static mx_device_t* root_dev;

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

static mx_status_t default_openat(mx_device_t* dev, mx_device_t** out, const char* path, uint32_t flags) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t default_close(mx_device_t* dev, uint32_t flags) {
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

static mx_status_t default_suspend(mx_device_t* dev) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t default_resume(mx_device_t* dev) {
    return ERR_NOT_SUPPORTED;
}

static struct list_node unmatched_device_list = LIST_INITIAL_VALUE(unmatched_device_list);
static struct list_node driver_list = LIST_INITIAL_VALUE(driver_list);

#define device_is_bound(dev) (!!dev->owner)

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
            printf("device: %p(%s): ref=0, busy, not releasing\n", dev, dev->name);
            return;
        }
#if TRACE_ADD_REMOVE
        printf("device: %p(%s): ref=0. releasing.\n", dev, dev->name);
#endif

        if (!(dev->flags & DEV_FLAG_VERY_DEAD)) {
            printf("device: %p(%s): only mostly dead (this is bad)\n", dev, dev->name);
        }
        if (!list_is_empty(&dev->children)) {
            printf("device: %p(%s): still has children! not good.\n", dev, dev->name);
        }

        mx_handle_close(dev->event);
        DM_UNLOCK();
        dev->ops->release(dev);
        DM_LOCK();
    }
}

static mx_status_t devhost_device_probe(mx_device_t* dev, mx_driver_t* drv) {
    mx_status_t status;

    xprintf("devhost: probe dev=%p(%s) drv=%p(%s)\n",
            dev, dev->name, drv, drv->name ? drv->name : "<NULL>");

    // don't bind to the driver that published this device
    if (drv == dev->driver) {
        return ERR_NOT_SUPPORTED;
    }

    // evaluate the driver's binding program against the device's properties
    if (!devhost_is_bindable_drv(drv, dev)) {
        return ERR_NOT_SUPPORTED;
    }

    DM_UNLOCK();
    status = drv->ops.bind(drv, dev);
    DM_LOCK();
    if (status < 0) {
        return status;
    }
    dev->owner = drv;
    dev_ref_acquire(dev);

    // remove from unbound list if we succeeded
    if (list_in_list(&dev->unode)) {
        list_delete(&dev->unode);
    }
    return NO_ERROR;
}

static void devhost_device_probe_all(mx_device_t* dev, bool autobind) {
    if ((dev->flags & DEV_FLAG_UNBINDABLE) == 0) {
        if (!device_is_bound(dev)) {
            mx_driver_t* drv = NULL;
            list_for_every_entry (&driver_list, drv, mx_driver_t, node) {
                if (autobind && drv->flags & DRV_FLAG_NO_AUTOBIND) {
                    continue;
                }
                if (devhost_device_probe(dev, drv) == NO_ERROR) {
                    break;
                }
            }
        }

        // if no driver is bound, add the device to the unmatched list
        if (!device_is_bound(dev)) {
            list_add_tail(&unmatched_device_list, &dev->unode);
        }
    }
}

void devhost_device_init(mx_device_t* dev, mx_driver_t* driver,
                        const char* name, mx_protocol_device_t* ops) {
    xprintf("devhost: init '%s' drv=%p, ops=%p\n",
            name ? name : "<NULL>", driver, ops);

    memset(dev, 0, sizeof(mx_device_t));
    dev->magic = DEV_MAGIC;
    dev->ops = ops;
    dev->driver = driver;
    list_initialize(&dev->children);

    if (name == NULL) {
        printf("devhost: dev=%p has null name.\n", dev);
        name = "invalid";
        dev->magic = 0;
    }

    size_t len = strlen(name);
    if (len >= MX_DEVICE_NAME_MAX) {
        printf("devhost: dev=%p name too large '%s'\n", dev, name);
        len = MX_DEVICE_NAME_MAX - 1;
        dev->magic = 0;
    }

    memcpy(dev->name, name, len);
    dev->name[len] = 0;
}

mx_status_t devhost_device_create(mx_device_t** out, mx_driver_t* driver,
                                 const char* name, mx_protocol_device_t* ops) {
    mx_device_t* dev = malloc(sizeof(mx_device_t));
    if (dev == NULL) {
        return ERR_NO_MEMORY;
    }
    devhost_device_init(dev, driver, name, ops);
    *out = dev;
    return NO_ERROR;
}

void devhost_device_set_bindable(mx_device_t* dev, bool bindable) {
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

static mx_status_t device_validate(mx_device_t* dev) {
    if (dev == NULL) {
        return ERR_INVALID_ARGS;
    }
    if (dev->magic != DEV_MAGIC) {
        return ERR_BAD_STATE;
    }
    if (dev->ops == NULL) {
        printf("device add: %p(%s): NULL ops\n", dev, dev->name);
        return ERR_INVALID_ARGS;
    }
    // devices which do not declare a primary protocol
    // are implied to be misc devices
    if (dev->protocol_id == 0) {
        dev->protocol_id = MX_PROTOCOL_MISC;
    }

    // install default methods if needed
    mx_protocol_device_t* ops = dev->ops;
    DEFAULT_IF_NULL(ops, get_protocol)
    DEFAULT_IF_NULL(ops, open);
    DEFAULT_IF_NULL(ops, openat);
    DEFAULT_IF_NULL(ops, close);
    DEFAULT_IF_NULL(ops, release);
    DEFAULT_IF_NULL(ops, read);
    DEFAULT_IF_NULL(ops, write);
    DEFAULT_IF_NULL(ops, iotxn_queue);
    DEFAULT_IF_NULL(ops, get_size);
    DEFAULT_IF_NULL(ops, ioctl);
    DEFAULT_IF_NULL(ops, suspend);
    DEFAULT_IF_NULL(ops, resume);

    return NO_ERROR;
}

mx_status_t devhost_device_add_root(mx_device_t* dev) {
    mx_status_t status;
    if ((status = device_validate(dev)) < 0) {
        return status;
    }
    if (root_dev != NULL) {
        printf("devhost: cannot add two root devices\n");
        panic();
    }
    root_dev = dev;
    dev_ref_acquire(dev);

    if (dev->protocol_id != 0) {
        list_add_tail(&unmatched_device_list, &dev->unode);
    }
    return NO_ERROR;
}

mx_status_t devhost_device_add(mx_device_t* dev, mx_device_t* parent) {
    mx_status_t status;
    if ((status = device_validate(dev)) < 0) {
        return status;
    }
    if (parent == NULL) {
        if (root_dev == NULL) {
            printf("device_add: cannot add %p(%s) (no root!)\n", dev, dev->name);
            return ERR_NOT_SUPPORTED;
        }
        parent = root_dev;
    }
    if (parent->flags & DEV_FLAG_DEAD) {
        printf("device add: %p: is dead, cannot add child %p\n", parent, dev);
        return ERR_BAD_STATE;
    }
#if TRACE_ADD_REMOVE
    printf("devhost: device add: %p(%s) parent=%p(%s)\n",
            dev, dev->name, parent, parent->name);
#endif

    // Don't create an event handle if we alredy have one
    if ((dev->event == MX_HANDLE_INVALID) &&
        ((status = mx_event_create(0, &dev->event)) < 0)) {
        printf("device add: %p(%s): cannot create event: %d\n",
               dev, dev->name, status);
       return status;
    }

    dev->flags |= DEV_FLAG_BUSY;

    // this is balanced by end of devhost_device_remove
    // or, for instanced devices, by the last close
    dev_ref_acquire(dev);

    if (!(dev->flags & DEV_FLAG_INSTANCE)) {
        // add to the device tree
        dev_ref_acquire(parent);
        dev->parent = parent;
        list_add_tail(&parent->children, &dev->node);

        mx_status_t r = devhost_add(parent, dev);
        if (r < 0) {
            printf("devhost: %p(%s): remote add failed %d\n", dev, dev->name, r);
            dev_ref_release(dev->parent);
            dev->parent = NULL;
            dev_ref_release(dev);
            list_delete(&dev->node);
            dev->flags &= (~DEV_FLAG_BUSY);
            return r;
        }
    }

    // probe the device
    devhost_device_probe_all(dev, true);

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

static void devhost_unbind_children(mx_device_t* dev) {
    mx_device_t* child = NULL;
    mx_device_t* temp = NULL;
#if TRACE_ADD_REMOVE
    printf("devhost_unbind_children: %p(%s)\n", dev, dev->name);
#endif
    list_for_every_entry_safe(&dev->children, child, temp, mx_device_t, node) {
        // call child's unbind op
        if (child->ops->unbind) {
#if TRACE_ADD_REMOVE
            printf("call unbind child: %p(%s)\n", child, child->name);
#endif
            // hold a reference so the child won't get released during its unbind callback.
            dev_ref_acquire(child);
            DM_UNLOCK();
            child->ops->unbind(child);
            DM_LOCK();
            dev_ref_release(child);
        }
    }
}

mx_status_t devhost_device_remove(mx_device_t* dev) {
    if (dev->flags & (DEV_FLAG_DEAD | DEV_FLAG_BUSY | DEV_FLAG_INSTANCE)) {
        printf("device: %p(%s): cannot be removed (%s)\n",
               dev, dev->name, removal_problem(dev->flags));
        return ERR_INVALID_ARGS;
    }
#if TRACE_ADD_REMOVE
    printf("device: %p(%s): is being removed\n", dev, dev->name);
#endif
    dev->flags |= DEV_FLAG_DEAD;

    devhost_unbind_children(dev);

    // cause the vfs entry to be unpublished to avoid further open() attempts
    xprintf("device: %p: devhost->devmgr remove rpc\n", dev);
    devhost_remove(dev);

    // detach from parent, downref parent
    if (dev->parent) {
        list_delete(&dev->node);
        dev_ref_release(dev->parent);
    }

    // remove from list of unbound devices, if on that list
    if (list_in_list(&dev->unode)) {
        list_delete(&dev->unode);
    }

    // detach from owner, downref on behalf of owner
    if (dev->owner) {
        dev->owner = NULL;
        dev_ref_release(dev);
    }

    dev->flags |= DEV_FLAG_VERY_DEAD;

    // this must be last, since it may result in the device structure being destroyed
    dev_ref_release(dev);

    return NO_ERROR;
}

mx_status_t devhost_device_bind(mx_device_t* dev, const char* drv_name) {
    if (device_is_bound(dev)) {
        return ERR_INVALID_ARGS;
    }
    if (dev->flags & DEV_FLAG_UNBINDABLE) {
        return NO_ERROR;
    }
    dev->flags |= DEV_FLAG_BUSY;
    if (!drv_name) {
        devhost_device_probe_all(dev, false);
    } else {
        // bind the driver with matching name
        mx_driver_t* drv = NULL;
        list_for_every_entry (&driver_list, drv, mx_driver_t, node) {
            if (strcmp(drv->name, drv_name)) {
                continue;
            }
            if (devhost_device_probe(dev, drv) == NO_ERROR) {
                break;
            }
        }
    }
    dev->flags &= ~DEV_FLAG_BUSY;
    return NO_ERROR;
}

mx_status_t devhost_device_rebind(mx_device_t* dev) {
    dev->flags |= DEV_FLAG_REBIND;

    // remove children
    mx_device_t* child = NULL;
    mx_device_t* temp = NULL;
    list_for_every_entry_safe(&dev->children, child, temp, mx_device_t, node) {
        devhost_device_remove(child);
    }

    devhost_unbind_children(dev);

    // detach from owner and downref
    if (dev->owner) {
        dev->owner = NULL;
        dev_ref_release(dev);
    }

    // probe the device again to bind
    devhost_device_probe_all(dev, false);

    dev->flags &= ~DEV_FLAG_REBIND;
    return NO_ERROR;
}

mx_status_t devhost_device_openat(mx_device_t* dev, mx_device_t** out, const char* path, uint32_t flags) {
    if (dev->flags & DEV_FLAG_DEAD) {
        printf("device open: %p(%s) is dead!\n", dev, dev->name);
        return ERR_BAD_STATE;
    }
    dev_ref_acquire(dev);
    mx_status_t r;
    DM_UNLOCK();
    *out = dev;
    if (path) {
        r = dev->ops->openat(dev, out, path, flags);
    } else {
        r = dev->ops->open(dev, out, flags);
    }
    DM_LOCK();
    if (r < 0) {
        dev_ref_release(dev);
    } else if (*out != dev) {
        // open created a per-instance device for us
        dev_ref_release(dev);

        dev = *out;
        if (!(dev->flags & DEV_FLAG_INSTANCE)) {
            printf("device open: %p(%s) in bad state %x\n", dev, dev->name, flags);
            panic();
        }
    }
    return r;
}

mx_status_t devhost_device_close(mx_device_t* dev, uint32_t flags) {
    mx_status_t r;
    DM_UNLOCK();
    r = dev->ops->close(dev, flags);
    DM_LOCK();
    dev_ref_release(dev);
    return r;
}

mx_status_t devhost_driver_add(mx_driver_t* drv) {
    xprintf("driver add: %p(%s)\n", drv, drv->name);

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
        devhost_device_probe(dev, drv);
    }
    return NO_ERROR;
}

mx_status_t devhost_driver_remove(mx_driver_t* drv) {
    // TODO: implement
    return ERR_NOT_SUPPORTED;
}

mx_status_t devhost_driver_unbind(mx_driver_t* drv, mx_device_t* dev) {
    if (dev->owner != drv) {
        return ERR_INVALID_ARGS;
    }
    dev->owner = NULL;
    dev_ref_release(dev);

    return NO_ERROR;
}

typedef struct {
    int fd;
    const char* path;
    int open_failures;
} fw_dir;

static fw_dir fw_dirs[] = {
    { -1, BOOT_FIRMWARE_DIR, 0},
    { -1, SYSTEM_FIRMWARE_DIR, 0},
};

static int devhost_open_firmware(const char* fwpath) {
    for (size_t i = 0; i < countof(fw_dirs); i++) {
        // Open the firmware directory if necessary
        if (fw_dirs[i].fd < 0) {
            fw_dirs[i].fd = open(fw_dirs[i].path, O_RDONLY | O_DIRECTORY);
            // If the directory doesn't open, it could mean there is no firmware
            // at that path (so the build system didn't create the directory),
            // or the filesystem hasn't been mounted yet. Log a warning every 5
            // failures and move on.
            if (fw_dirs[i].fd < 0) {
                if (fw_dirs[i].open_failures++ % 5 == 0) {
                    printf("devhost: warning: could not open firmware dir '%s' (err=%d)\n",
                            fw_dirs[i].path, errno);
                }
            }
        }
        // If the firmware directory is open, try to load the firmware.
        if (fw_dirs[i].fd >= 0) {
            int fwfd = openat(fw_dirs[i].fd, fwpath, O_RDONLY);
            // If the error is NOT that the firmware wasn't found, (e.g.,
            // EACCES), return early, with errno set by openat.
            if (fwfd >= 0 || errno != ENOENT) return fwfd;
        }
    }

    // Firmware wasn't found anywhere.
    errno = ENOENT;
    return -1;
}

mx_status_t devhost_load_firmware(mx_driver_t* drv, const char* path, mx_handle_t* fw,
                                  mx_size_t* size) {
    xprintf("devhost: drv=%p path=%s fw=%p\n", drv, path, fw);

    int fwfd = devhost_open_firmware(path);
    if (fwfd < 0) {
        switch (errno) {
        case ENOENT: return ERR_NOT_FOUND;
        case EACCES: return ERR_ACCESS_DENIED;
        case ENOMEM: return ERR_NO_MEMORY;
        default: return ERR_INTERNAL;
        }
    }

    struct stat fwstat;
    int ret = fstat(fwfd, &fwstat);
    if (ret < 0) {
        int e = errno;
        close(fwfd);
        switch (e) {
        case EACCES: return ERR_ACCESS_DENIED;
        case EBADF:
        case EFAULT: return ERR_BAD_STATE;
        default: return ERR_INTERNAL;
        }
    }

    if (fwstat.st_size == 0) {
        close(fwfd);
        return ERR_NOT_SUPPORTED;
    }

    uint64_t vmo_size = (fwstat.st_size + 4095) & ~4095;
    mx_handle_t vmo;
    mx_status_t status = mx_vmo_create(vmo_size, 0, &vmo);
    if (status != NO_ERROR) {
        close(fwfd);
        return status;
    }

    uint8_t buffer[4096];
    size_t remaining = fwstat.st_size;
    uint64_t off = 0;
    while (remaining) {
        ssize_t r = read(fwfd, buffer, sizeof(buffer));
        if (r < 0) {
            close(fwfd);
            mx_handle_close(vmo);
            // Distinguish other errors?
            return ERR_IO;
        }
        if (r == 0) break;
        mx_size_t actual = 0;
        status = mx_vmo_write(vmo, (const void*)buffer, off, (mx_size_t)r, &actual);
        if (actual < (mx_size_t)r) {
            printf("devhost: BUG: wrote %zu < %zu firmware vmo bytes!\n", actual, r);
            close(fwfd);
            mx_handle_close(vmo);
            return ERR_BAD_STATE;
        }
        off += actual;
        remaining -= actual;
    }

    if (remaining > 0) {
        printf("devhost: EOF found before writing firmware '%s'\n", path);
    }
    *fw = vmo;
    *size = fwstat.st_size;
    close(fwfd);

    return NO_ERROR;
}
