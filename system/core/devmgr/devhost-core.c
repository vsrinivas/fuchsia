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

#include <magenta/assert.h>
#include <magenta/listnode.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

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

static mx_device_t* dev_create_parent;
static mx_device_t* dev_create_device;

mx_device_t* device_create_setup(mx_device_t* parent) {
    DM_LOCK();
    mx_device_t* dev = dev_create_device;
    dev_create_parent = parent;
    dev_create_device = NULL;
    DM_UNLOCK();
    return dev;
}

static mx_status_t default_open(void* ctx, mx_device_t** out, uint32_t flags) {
    return MX_OK;
}

static mx_status_t default_open_at(void* ctx, mx_device_t** out, const char* path, uint32_t flags) {
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t default_close(void* ctx, uint32_t flags) {
    return MX_OK;
}

static void default_unbind(void* ctx) {
}

static void default_release(void* ctx) {
}

static mx_status_t default_read(void* ctx, void* buf, size_t count, mx_off_t off, size_t* actual) {
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t default_write(void* ctx, const void* buf, size_t count, mx_off_t off, size_t* actual) {
    return MX_ERR_NOT_SUPPORTED;
}

static mx_off_t default_get_size(void* ctx) {
    return 0;
}

static mx_status_t default_ioctl(void* ctx, uint32_t op,
                             const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* out_actual) {
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t default_suspend(void* ctx, uint32_t flags) {
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t default_resume(void* ctx, uint32_t flags) {
    return MX_ERR_NOT_SUPPORTED;
}

mx_protocol_device_t device_default_ops = {
    .open = default_open,
    .open_at = default_open_at,
    .close = default_close,
    .unbind = default_unbind,
    .release = default_release,
    .read = default_read,
    .write = default_write,
    .get_size = default_get_size,
    .ioctl = default_ioctl,
    .suspend = default_suspend,
    .resume = default_resume,
};

static struct list_node unmatched_device_list = LIST_INITIAL_VALUE(unmatched_device_list);
static struct list_node driver_list = LIST_INITIAL_VALUE(driver_list);

static inline bool device_is_bound(mx_device_t* dev) {
    return dev->owner != NULL;
}

void dev_ref_release(mx_device_t* dev) {
    if (dev->refcount < 1) {
        printf("device: %p: REFCOUNT GOING NEGATIVE\n", dev);
        //TODO: probably should assert, but to start with let's
        //      see if this is happening in normal use
    }
    dev->refcount--;
    if (dev->refcount == 0) {
        if (dev->flags & DEV_FLAG_INSTANCE) {
            // these don't get removed, so mark dead state here
            dev->flags |= DEV_FLAG_DEAD | DEV_FLAG_VERY_DEAD;
            list_delete(&dev->node);
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
        mx_handle_close(dev->local_event);
        mx_handle_close(dev->resource);
        DM_UNLOCK();
        dev_op_release(dev);
        DM_LOCK();

        // At this point we can safely release the ref on our parent
        if (dev->parent) {
            dev_ref_release(dev->parent);
        }
    }
}

mx_status_t devhost_device_create(mx_driver_t* drv, mx_device_t* parent,
                                  const char* name, void* ctx,
                                  mx_protocol_device_t* ops, mx_device_t** out) {

    if (!drv) {
        printf("devhost: _device_add could not find driver!\n");
        return MX_ERR_INVALID_ARGS;
    }

    mx_device_t* dev = malloc(sizeof(mx_device_t));
    if (dev == NULL) {
        return MX_ERR_NO_MEMORY;
    }

    memset(dev, 0, sizeof(mx_device_t));
    dev->magic = DEV_MAGIC;
    dev->ops = ops;
    dev->driver = drv;
    list_initialize(&dev->children);
    list_initialize(&dev->instances);

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
    dev->ctx = ctx ? ctx : dev;
    *out = dev;
    return MX_OK;
}

#define DEFAULT_IF_NULL(ops,method) \
    if (ops->method == NULL) { \
        ops->method = default_##method; \
    }

static mx_status_t device_validate(mx_device_t* dev) {
    if (dev == NULL) {
        printf("INVAL: NULL!\n");
        return MX_ERR_INVALID_ARGS;
    }
    if (dev->flags & DEV_FLAG_ADDED) {
        printf("device already added: %p(%s)\n", dev, dev->name);
        return MX_ERR_BAD_STATE;
    }
    if (dev->magic != DEV_MAGIC) {
        return MX_ERR_BAD_STATE;
    }
    if (dev->ops == NULL) {
        printf("device add: %p(%s): NULL ops\n", dev, dev->name);
        return MX_ERR_INVALID_ARGS;
    }
    if ((dev->protocol_id == MX_PROTOCOL_MISC_PARENT) ||
        (dev->protocol_id == MX_PROTOCOL_ROOT)) {
        // These protocols is only allowed for the special
        // singleton misc or root parent devices.
        return MX_ERR_INVALID_ARGS;
    }
    // devices which do not declare a primary protocol
    // are implied to be misc devices
    if (dev->protocol_id == 0) {
        dev->protocol_id = MX_PROTOCOL_MISC;
    }

    // install default methods if needed
    mx_protocol_device_t* ops = dev->ops;
    DEFAULT_IF_NULL(ops, open);
    DEFAULT_IF_NULL(ops, open_at);
    DEFAULT_IF_NULL(ops, close);
    DEFAULT_IF_NULL(ops, unbind);
    DEFAULT_IF_NULL(ops, release);
    DEFAULT_IF_NULL(ops, read);
    DEFAULT_IF_NULL(ops, write);
    DEFAULT_IF_NULL(ops, get_size);
    DEFAULT_IF_NULL(ops, ioctl);
    DEFAULT_IF_NULL(ops, suspend);
    DEFAULT_IF_NULL(ops, resume);

    return MX_OK;
}

mx_status_t devhost_device_install(mx_device_t* dev) {
    mx_status_t status;
    if ((status = device_validate(dev)) < 0) {
        dev->flags |= DEV_FLAG_DEAD | DEV_FLAG_VERY_DEAD;
        return status;
    }
    // Don't create an event handle if we alredy have one
    if ((dev->event == MX_HANDLE_INVALID) &&
        ((status = mx_eventpair_create(0, &dev->event, &dev->local_event)) < 0)) {
        printf("device add: %p(%s): cannot create event: %d\n",
               dev, dev->name, status);
        dev->flags |= DEV_FLAG_DEAD | DEV_FLAG_VERY_DEAD;
        return status;
    }
    dev_ref_acquire(dev);
    dev->flags |= DEV_FLAG_ADDED;
    return MX_OK;
}

mx_status_t devhost_device_add(mx_device_t* dev, mx_device_t* parent,
                               const mx_device_prop_t* props, uint32_t prop_count,
                               const char* businfo, mx_handle_t resource) {
    mx_status_t status;
    if ((status = device_validate(dev)) < 0) {
        goto fail;
    }
    if (parent == dev_create_parent) {
        // check for magic parent value indicating
        // shadow device creation, and if so, ensure
        // we don't add more than one shadow device
        // per create() op...
        if (dev_create_device != NULL) {
            return MX_ERR_BAD_STATE;
        }
    } else {
        if (parent == NULL) {
            printf("device_add: cannot add %p(%s) to NULL parent\n", dev, dev->name);
            status = MX_ERR_NOT_SUPPORTED;
            goto fail;
        }
        if (parent->flags & DEV_FLAG_DEAD) {
            printf("device add: %p: is dead, cannot add child %p\n", parent, dev);
            status = MX_ERR_BAD_STATE;
            goto fail;
        }
    }
#if TRACE_ADD_REMOVE
    printf("devhost: device add: %p(%s) parent=%p(%s)\n",
            dev, dev->name, parent, parent->name);
#endif

    // Don't create an event handle if we alredy have one
    if ((dev->event == MX_HANDLE_INVALID) &&
        ((status = mx_eventpair_create(0, &dev->event, &dev->local_event)) < 0)) {
        printf("device add: %p(%s): cannot create event: %d\n",
               dev, dev->name, status);
        goto fail;
    }

    dev->flags |= DEV_FLAG_BUSY;

    // this is balanced by end of devhost_device_remove
    // or, for instanced devices, by the last close
    dev_ref_acquire(dev);

    // shadow devices are created through this handshake process
    if (parent == dev_create_parent) {
        dev_create_device = dev;
        dev->flags |= DEV_FLAG_ADDED;
        dev->flags &= (~DEV_FLAG_BUSY);
        return MX_OK;
    }

    dev_ref_acquire(parent);
    dev->parent = parent;

    if (dev->flags & DEV_FLAG_INSTANCE) {
        list_add_tail(&parent->instances, &dev->node);

        // instanced devices are not remoted and resources
        // attached to them are discarded
        if (resource != MX_HANDLE_INVALID) {
            mx_handle_close(resource);
        }
    } else {
        // add to the device tree
        list_add_tail(&parent->children, &dev->node);

        // devhost_add always consumes the handle
        status = devhost_add(parent, dev, businfo, resource, props, prop_count);
        if (status < 0) {
            printf("devhost: %p(%s): remote add failed %d\n",
                   dev, dev->name, status);
            dev_ref_release(dev->parent);
            dev->parent = NULL;
            dev_ref_release(dev);
            list_delete(&dev->node);
            dev->flags &= (~DEV_FLAG_BUSY);
            return status;
        }
    }
    dev->flags |= DEV_FLAG_ADDED;
    dev->flags &= (~DEV_FLAG_BUSY);
    return MX_OK;

fail:
    if (resource != MX_HANDLE_INVALID) {
        mx_handle_close(resource);
    }
    dev->flags |= DEV_FLAG_DEAD | DEV_FLAG_VERY_DEAD;
    return status;
}

#define REMOVAL_BAD_FLAGS \
    (DEV_FLAG_DEAD | DEV_FLAG_BUSY |\
     DEV_FLAG_INSTANCE | DEV_FLAG_MULTI_BIND)

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
    if (flags & DEV_FLAG_MULTI_BIND) {
        return "multi-bind-able device";
    }
    return "?";
}

static void devhost_unbind_child(mx_device_t* child) {
    // call child's unbind op
    if (child->ops->unbind) {
#if TRACE_ADD_REMOVE
        printf("call unbind child: %p(%s)\n", child, child->name);
#endif
        // hold a reference so the child won't get released during its unbind callback.
        dev_ref_acquire(child);
        DM_UNLOCK();
        dev_op_unbind(child);
        DM_LOCK();
        dev_ref_release(child);
    }
}

static void devhost_unbind_children(mx_device_t* dev) {
    mx_device_t* child = NULL;
    mx_device_t* temp = NULL;
#if TRACE_ADD_REMOVE
    printf("devhost_unbind_children: %p(%s)\n", dev, dev->name);
#endif
    list_for_every_entry_safe(&dev->children, child, temp, mx_device_t, node) {
        devhost_unbind_child(child);
    }
    list_for_every_entry_safe(&dev->instances, child, temp, mx_device_t, node) {
        devhost_unbind_child(child);
    }
}

mx_status_t devhost_device_remove(mx_device_t* dev) {
    if (dev->flags & REMOVAL_BAD_FLAGS) {
        printf("device: %p(%s): cannot be removed (%s)\n",
               dev, dev->name, removal_problem(dev->flags));
        return MX_ERR_INVALID_ARGS;
    }
#if TRACE_ADD_REMOVE
    printf("device: %p(%s): is being removed\n", dev, dev->name);
#endif
    dev->flags |= DEV_FLAG_DEAD;

    devhost_unbind_children(dev);

    // cause the vfs entry to be unpublished to avoid further open() attempts
    xprintf("device: %p: devhost->devmgr remove rpc\n", dev);
    devhost_remove(dev);

    // detach from owner, downref on behalf of owner
    if (dev->owner) {
        if (dev->owner->ops->unbind) {
            dev->owner->ops->unbind(dev->owner->ctx, dev, dev->owner_cookie);
        }
        dev->owner = NULL;
        dev_ref_release(dev);
    }

    // detach from parent.  we do not downref the parent
    // until after our refcount hits zero and our release()
    // hook has been called.
    if (dev->parent) {
        list_delete(&dev->node);
    }

    dev->flags |= DEV_FLAG_VERY_DEAD;

    // this must be last, since it may result in the device structure being destroyed
    dev_ref_release(dev);

    return MX_OK;
}

mx_status_t devhost_device_rebind(mx_device_t* dev) {
    dev->flags |= DEV_FLAG_BUSY;

    // remove children
    mx_device_t* child;
    mx_device_t* temp;
    list_for_every_entry_safe(&dev->children, child, temp, mx_device_t, node) {
        devhost_device_remove(child);
    }

    // notify children that they've been unbound
    devhost_unbind_children(dev);

    // detach from owner and downref
    if (dev->owner) {
        if (dev->owner->ops->unbind) {
            dev->owner->ops->unbind(dev->owner->ctx, dev, dev->owner_cookie);
        }
        dev->owner = NULL;
        dev_ref_release(dev);
    }

    dev->flags &= ~DEV_FLAG_BUSY;

    // ask devcoord to find us a driver if it can
    devhost_device_bind(dev, "");
    return MX_OK;
}

mx_status_t devhost_device_open_at(mx_device_t* dev, mx_device_t** out, const char* path, uint32_t flags) {
    if (dev->flags & DEV_FLAG_DEAD) {
        printf("device open: %p(%s) is dead!\n", dev, dev->name);
        return MX_ERR_BAD_STATE;
    }
    dev_ref_acquire(dev);
    mx_status_t r;
    DM_UNLOCK();
    *out = dev;
    if (path) {
        r = dev_op_open_at(dev, out, path, flags);
    } else {
        r = dev_op_open(dev, out, flags);
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
    r = dev_op_close(dev, flags);
    DM_LOCK();
    dev_ref_release(dev);
    return r;
}

mx_status_t devhost_device_unbind(mx_device_t* dev) {
    if (!dev->owner) {
        return MX_ERR_INVALID_ARGS;
    }
    dev->owner = NULL;
    dev_ref_release(dev);

    return MX_OK;
}

void devhost_device_destroy(mx_device_t* dev) {
    // Only destroy devices immediately after device_create() or after they're dead.
    MX_DEBUG_ASSERT(dev->flags == 0 || dev->flags & DEV_FLAG_VERY_DEAD);
    dev->magic = 0xdeaddead;
    dev->ops = NULL;
    free(dev);
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

mx_status_t devhost_load_firmware(mx_device_t* dev, const char* path, mx_handle_t* fw,
                                  size_t* size) {
    xprintf("devhost: dev=%p path=%s fw=%p\n", dev, path, fw);

    int fwfd = devhost_open_firmware(path);
    if (fwfd < 0) {
        switch (errno) {
        case ENOENT: return MX_ERR_NOT_FOUND;
        case EACCES: return MX_ERR_ACCESS_DENIED;
        case ENOMEM: return MX_ERR_NO_MEMORY;
        default: return MX_ERR_INTERNAL;
        }
    }

    struct stat fwstat;
    int ret = fstat(fwfd, &fwstat);
    if (ret < 0) {
        int e = errno;
        close(fwfd);
        switch (e) {
        case EACCES: return MX_ERR_ACCESS_DENIED;
        case EBADF:
        case EFAULT: return MX_ERR_BAD_STATE;
        default: return MX_ERR_INTERNAL;
        }
    }

    if (fwstat.st_size == 0) {
        close(fwfd);
        return MX_ERR_NOT_SUPPORTED;
    }

    uint64_t vmo_size = (fwstat.st_size + 4095) & ~4095;
    mx_handle_t vmo;
    mx_status_t status = mx_vmo_create(vmo_size, 0, &vmo);
    if (status != MX_OK) {
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
            return MX_ERR_IO;
        }
        if (r == 0) break;
        size_t actual = 0;
        status = mx_vmo_write(vmo, (const void*)buffer, off, (size_t)r, &actual);
        if (actual < (size_t)r) {
            printf("devhost: BUG: wrote %zu < %zu firmware vmo bytes!\n", actual, r);
            close(fwfd);
            mx_handle_close(vmo);
            return MX_ERR_BAD_STATE;
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

    return MX_OK;
}
