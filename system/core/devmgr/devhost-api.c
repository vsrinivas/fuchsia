// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/compiler.h>

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

__EXPORT mx_status_t device_add_from_driver(mx_driver_t* drv, mx_device_t* parent,
                                            device_add_args_t* args, mx_device_t** out) {
    mx_status_t r;
    mx_device_t* dev = NULL;

    if (!parent) {
        return MX_ERR_INVALID_ARGS;
    }
    if (!args || args->version != DEVICE_ADD_ARGS_VERSION) {
        return MX_ERR_INVALID_ARGS;
    }
    if (!args->ops || args->ops->version != DEVICE_OPS_VERSION) {
        return MX_ERR_INVALID_ARGS;
    }
    if (args->flags & ~(DEVICE_ADD_NON_BINDABLE | DEVICE_ADD_INSTANCE | DEVICE_ADD_BUSDEV)) {
        return MX_ERR_INVALID_ARGS;
    }
    if ((args->flags & DEVICE_ADD_INSTANCE) && (args->flags & DEVICE_ADD_BUSDEV)) {
        return MX_ERR_INVALID_ARGS;
    }

    DM_LOCK();
    r = devhost_device_create(drv, parent, args->name, args->ctx, args->ops, &dev);
    if (r != MX_OK) {
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

    if (args->flags & DEVICE_ADD_BUSDEV) {
        r = devhost_device_add(dev, parent, args->props, args->prop_count, args->busdev_args,
                               args->rsrc);
    } else if (args->flags & DEVICE_ADD_INSTANCE) {
        dev->flags |= DEV_FLAG_INSTANCE | DEV_FLAG_UNBINDABLE;
        r = devhost_device_add(dev, parent, NULL, 0, NULL, MX_HANDLE_INVALID);
    } else {
        r = devhost_device_add(dev, parent, args->props, args->prop_count, NULL, MX_HANDLE_INVALID);
    }
    if (r != MX_OK) {
        if (out) {
            *out = NULL;
        }
        devhost_device_destroy(dev);
    }

    DM_UNLOCK();
    return r;
}

__EXPORT mx_status_t device_remove(mx_device_t* dev) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_remove(dev);
    DM_UNLOCK();
    return r;
}

__EXPORT void device_unbind(mx_device_t* dev) {
    DM_LOCK();
    devhost_device_unbind(dev);
    DM_UNLOCK();
}

__EXPORT mx_status_t device_rebind(mx_device_t* dev) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_rebind(dev);
    DM_UNLOCK();
    return r;
}


__EXPORT const char* device_get_name(mx_device_t* dev) {
    return dev->name;
}

__EXPORT mx_device_t* device_get_parent(mx_device_t* dev) {
    return dev->parent;
}

typedef struct {
    void* ops;
    void* ctx;
} generic_protocol_t;

__EXPORT mx_status_t device_get_protocol(mx_device_t* dev, uint32_t proto_id, void* out) {
    generic_protocol_t* proto = out;
    if (dev->ops->get_protocol) {
        return dev->ops->get_protocol(dev->ctx, proto_id, out);
    }
    if ((proto_id == dev->protocol_id) && (dev->protocol_ops != NULL)) {
        proto->ops = dev->protocol_ops;
        proto->ctx = dev->ctx;
        return MX_OK;
    }
    return MX_ERR_NOT_SUPPORTED;
}

__EXPORT mx_handle_t device_get_resource(mx_device_t* dev) {
    mx_handle_t h;
    if (mx_handle_duplicate(dev->resource, MX_RIGHT_SAME_RIGHTS, &h) < 0) {
        return MX_HANDLE_INVALID;
    } else {
        return h;
    }
}

__EXPORT void device_state_clr_set(mx_device_t* dev, mx_signals_t clearflag, mx_signals_t setflag) {
    mx_object_signal(dev->event, clearflag, setflag);
}


__EXPORT mx_off_t device_get_size(mx_device_t* dev) {
    return dev->ops->get_size(dev->ctx);
}

__EXPORT mx_status_t device_read(mx_device_t* dev, void* buf, size_t count,
                                 mx_off_t off, size_t* actual) {
    return dev->ops->read(dev->ctx, buf, count, off, actual);
}

__EXPORT mx_status_t device_write(mx_device_t* dev, const void* buf, size_t count,
                                  mx_off_t off, size_t* actual) {
    return dev->ops->write(dev->ctx, buf, count, off, actual);
}

__EXPORT mx_status_t device_ioctl(mx_device_t* dev, uint32_t op,
                                  const void* in_buf, size_t in_len,
                                  void* out_buf, size_t out_len,
                                  size_t* out_actual) {
    return dev->ops->ioctl(dev->ctx, op, in_buf, in_len, out_buf, out_len, out_actual);
}

__EXPORT mx_status_t device_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    if (dev->ops->iotxn_queue != NULL) {
        dev->ops->iotxn_queue(dev->ctx, txn);
        return MX_OK;
    } else {
        return MX_ERR_NOT_SUPPORTED;
    }
}


// LibDriver Misc Interfaces

extern mx_handle_t root_resource_handle;

__EXPORT mx_handle_t get_root_resource(void) {
    return root_resource_handle;
}

__EXPORT mx_status_t load_firmware(mx_device_t* dev, const char* path,
                                   mx_handle_t* fw, size_t* size) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_load_firmware(dev, path, fw, size);
    DM_UNLOCK();
    return r;
}

// Interface Used by DevHost RPC Layer

mx_status_t device_bind(mx_device_t* dev, const char* drv_libname) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_bind(dev, drv_libname);
    DM_UNLOCK();
    return r;
}

mx_status_t device_open_at(mx_device_t* dev, mx_device_t** out, const char* path, uint32_t flags) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_open_at(dev, out, path, flags);
    DM_UNLOCK();
    return r;
}

mx_status_t device_close(mx_device_t* dev, uint32_t flags) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_close(dev, flags);
    DM_UNLOCK();
    return r;
}