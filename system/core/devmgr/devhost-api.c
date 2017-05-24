// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/compiler.h>

#include "devhost.h"
#include <driver/driver-api.h>

// These are the API entry-points from drivers
// They must take the devhost_api_lock before calling devhost_* internals
//
// Driver code MUST NOT directly call devhost_* APIs

static void _device_unbind(mx_device_t* dev) {
    DM_LOCK();
    devhost_device_unbind(dev);
    DM_UNLOCK();
}

static mx_status_t _device_add(mx_device_t* parent, device_add_args_t* args,
                               mx_device_t** out) {
    mx_status_t r;
    mx_device_t* dev = NULL;

    if (!parent) {
        return ERR_INVALID_ARGS;
    }
    if (!args || args->version != DEVICE_ADD_ARGS_VERSION) {
        return ERR_INVALID_ARGS;
    }
    if (!args->ops || args->ops->version != DEVICE_OPS_VERSION) {
        return ERR_INVALID_ARGS;
    }
    if (args->flags & ~(DEVICE_ADD_NON_BINDABLE | DEVICE_ADD_INSTANCE | DEVICE_ADD_BUSDEV)) {
        return ERR_INVALID_ARGS;
    }
    if ((args->flags & DEVICE_ADD_INSTANCE) && (args->flags & DEVICE_ADD_BUSDEV)) {
        return ERR_INVALID_ARGS;
    }

    DM_LOCK();
    r = devhost_device_create(parent, args->name, args->ctx, args->ops, &dev);
    if (r != NO_ERROR) {
        DM_UNLOCK();
        return r;
    }
    if (args->proto_id) {
        devhost_device_set_protocol(dev, args->proto_id, args->proto_ops);
    }
    if (args->flags & DEVICE_ADD_NON_BINDABLE) {
        devhost_device_set_bindable(dev, false);
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
    if (r == NO_ERROR) {
        *out = dev;
    } else {
        devhost_device_destroy(dev);
    }

    DM_UNLOCK();
    return r;
}

static mx_status_t _device_remove(mx_device_t* dev) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_remove(dev);
    DM_UNLOCK();
    return r;
}

static mx_status_t _device_rebind(mx_device_t* dev) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_rebind(dev);
    DM_UNLOCK();
    return r;
}

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

extern mx_handle_t root_resource_handle;

__EXPORT mx_handle_t _get_root_resource(void) {
    return root_resource_handle;
}

static mx_status_t _load_firmware(mx_device_t* dev, const char* path, mx_handle_t* fw,
                                  size_t* size) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_load_firmware(dev, path, fw, size);
    DM_UNLOCK();
    return r;
}

driver_api_t devhost_api = {
    .device_unbind = _device_unbind,
    .device_add = _device_add,
    .device_remove = _device_remove,
    .device_rebind = _device_rebind,
    .get_root_resource = _get_root_resource,
    .load_firmware = _load_firmware,
};
