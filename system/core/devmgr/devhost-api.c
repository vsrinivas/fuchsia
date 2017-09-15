// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include "devhost.h"

#include <stdarg.h>
#include <stdio.h>

// These are the API entry-points from drivers
// They must take the devhost_api_lock before calling devhost_* internals
//
// Driver code MUST NOT directly call devhost_* APIs


// LibDriver Device Interface

__EXPORT zx_status_t device_add_from_driver(zx_driver_t* drv, zx_device_t* parent,
                                            device_add_args_t* args, zx_device_t** out) {
    zx_status_t r;
    zx_device_t* dev = NULL;

    if (!parent) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (!args || args->version != DEVICE_ADD_ARGS_VERSION) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (!args->ops || args->ops->version != DEVICE_OPS_VERSION) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (args->flags & ~(DEVICE_ADD_NON_BINDABLE | DEVICE_ADD_INSTANCE | DEVICE_ADD_MUST_ISOLATE)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if ((args->flags & DEVICE_ADD_INSTANCE) && (args->flags & DEVICE_ADD_MUST_ISOLATE)) {
        return ZX_ERR_INVALID_ARGS;
    }

    DM_LOCK();
    r = devhost_device_create(drv, parent, args->name, args->ctx, args->ops, &dev);
    if (r != ZX_OK) {
        DM_UNLOCK();
        return r;
    }
    if (args->proto_id) {
        dev->protocol_id = args->proto_id;
        dev->protocol_ops = args->proto_ops;
    }
    if (args->flags & DEVICE_ADD_NON_BINDABLE) {
        dev->flags |= DEV_FLAG_UNBINDABLE;
    }

    // out must be set before calling devhost_device_add().
    // devhost_device_add() may result in child devices being created before it returns,
    // and those children may call ops on the device before device_add() returns.
    if (out) {
        *out = dev;
    }

    if (args->flags & DEVICE_ADD_MUST_ISOLATE) {
        r = devhost_device_add(dev, parent, args->props, args->prop_count, args->busdev_args,
                               args->rsrc);
    } else if (args->flags & DEVICE_ADD_INSTANCE) {
        dev->flags |= DEV_FLAG_INSTANCE | DEV_FLAG_UNBINDABLE;
        r = devhost_device_add(dev, parent, NULL, 0, NULL, ZX_HANDLE_INVALID);
    } else {
        r = devhost_device_add(dev, parent, args->props, args->prop_count, NULL, ZX_HANDLE_INVALID);
    }
    if (r != ZX_OK) {
        if (out) {
            *out = NULL;
        }
        devhost_device_destroy(dev);
    }

    DM_UNLOCK();
    return r;
}

__EXPORT zx_status_t device_remove(zx_device_t* dev) {
    zx_status_t r;
    DM_LOCK();
    r = devhost_device_remove(dev);
    DM_UNLOCK();
    return r;
}

__EXPORT void device_unbind(zx_device_t* dev) {
    DM_LOCK();
    devhost_device_unbind(dev);
    DM_UNLOCK();
}

__EXPORT zx_status_t device_rebind(zx_device_t* dev) {
    zx_status_t r;
    DM_LOCK();
    r = devhost_device_rebind(dev);
    DM_UNLOCK();
    return r;
}


__EXPORT const char* device_get_name(zx_device_t* dev) {
    return dev->name;
}

__EXPORT zx_device_t* device_get_parent(zx_device_t* dev) {
    return dev->parent;
}

typedef struct {
    void* ops;
    void* ctx;
} generic_protocol_t;

__EXPORT zx_status_t device_get_protocol(zx_device_t* dev, uint32_t proto_id, void* out) {
    generic_protocol_t* proto = out;
    if (dev->ops->get_protocol) {
        return dev->ops->get_protocol(dev->ctx, proto_id, out);
    }
    if ((proto_id == dev->protocol_id) && (dev->protocol_ops != NULL)) {
        proto->ops = dev->protocol_ops;
        proto->ctx = dev->ctx;
        return ZX_OK;
    }
    return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT zx_handle_t device_get_resource(zx_device_t* dev) {
    zx_handle_t h;
    if (zx_handle_duplicate(dev->resource, ZX_RIGHT_SAME_RIGHTS, &h) < 0) {
        return ZX_HANDLE_INVALID;
    } else {
        return h;
    }
}

__EXPORT void device_state_clr_set(zx_device_t* dev, zx_signals_t clearflag, zx_signals_t setflag) {
    zx_object_signal(dev->event, clearflag, setflag);
}


__EXPORT zx_off_t device_get_size(zx_device_t* dev) {
    return dev->ops->get_size(dev->ctx);
}

__EXPORT zx_status_t device_read(zx_device_t* dev, void* buf, size_t count,
                                 zx_off_t off, size_t* actual) {
    return dev->ops->read(dev->ctx, buf, count, off, actual);
}

__EXPORT zx_status_t device_write(zx_device_t* dev, const void* buf, size_t count,
                                  zx_off_t off, size_t* actual) {
    return dev->ops->write(dev->ctx, buf, count, off, actual);
}

__EXPORT zx_status_t device_ioctl(zx_device_t* dev, uint32_t op,
                                  const void* in_buf, size_t in_len,
                                  void* out_buf, size_t out_len,
                                  size_t* out_actual) {
    return dev->ops->ioctl(dev->ctx, op, in_buf, in_len, out_buf, out_len, out_actual);
}

__EXPORT zx_status_t device_iotxn_queue(zx_device_t* dev, iotxn_t* txn) {
    if (dev->ops->iotxn_queue != NULL) {
        dev->ops->iotxn_queue(dev->ctx, txn);
        return ZX_OK;
    } else {
        return ZX_ERR_NOT_SUPPORTED;
    }
}

// LibDriver Misc Interfaces

extern zx_handle_t root_resource_handle;

__EXPORT zx_handle_t get_root_resource(void) {
    return root_resource_handle;
}

__EXPORT zx_status_t load_firmware(zx_device_t* dev, const char* path,
                                   zx_handle_t* fw, size_t* size) {
    zx_status_t r;
    DM_LOCK();
    r = devhost_load_firmware(dev, path, fw, size);
    DM_UNLOCK();
    return r;
}

// Interface Used by DevHost RPC Layer

zx_status_t device_bind(zx_device_t* dev, const char* drv_libname) {
    zx_status_t r;
    DM_LOCK();
    r = devhost_device_bind(dev, drv_libname);
    DM_UNLOCK();
    return r;
}

zx_status_t device_open_at(zx_device_t* dev, zx_device_t** out, const char* path, uint32_t flags) {
    zx_status_t r;
    DM_LOCK();
    r = devhost_device_open_at(dev, out, path, flags);
    DM_UNLOCK();
    return r;
}

zx_status_t device_close(zx_device_t* dev, uint32_t flags) {
    zx_status_t r;
    DM_LOCK();
    r = devhost_device_close(dev, flags);
    DM_UNLOCK();
    return r;
}
