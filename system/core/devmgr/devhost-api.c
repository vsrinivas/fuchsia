// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/compiler.h>

#include "devhost.h"
#include "driver-api.h"

// These are the API entry-points from drivers
// They must take the devhost_api_lock before calling devhost_* internals
//
// Driver code MUST NOT directly call devhost_* APIs

static void _driver_add(mx_driver_t* drv) {
    DM_LOCK();
    devhost_driver_add(drv);
    DM_UNLOCK();
}

static void _driver_remove(mx_driver_t* drv) {
    DM_LOCK();
    devhost_driver_remove(drv);
    DM_UNLOCK();
}

static void _driver_unbind(mx_driver_t* drv, mx_device_t* dev) {
    DM_LOCK();
    devhost_driver_unbind(drv, dev);
    DM_UNLOCK();
}

static mx_status_t _device_create(mx_device_t** dev, mx_driver_t* drv,
                                  const char* name, mx_protocol_device_t* ops) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_create(dev, drv, name, ops);
    DM_UNLOCK();
    return r;
}

static void _device_init(mx_device_t* dev, mx_driver_t* drv,
                         const char* name, mx_protocol_device_t* ops) {
    DM_LOCK();
    devhost_device_init(dev, drv, name, ops);
    DM_UNLOCK();
}

static mx_status_t _device_add(mx_device_t* dev, mx_device_t* parent) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_add(dev, parent);
    DM_UNLOCK();
    return r;
}

static mx_status_t _device_add_instance(mx_device_t* dev, mx_device_t* parent) {
    mx_status_t r;
    DM_LOCK();
    if (dev) {
        dev->flags |= DEV_FLAG_INSTANCE | DEV_FLAG_UNBINDABLE;
    }
    r = devhost_device_add(dev, parent);
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

static void _device_set_bindable(mx_device_t* dev, bool bindable) {
    DM_LOCK();
    devhost_device_set_bindable(dev, bindable);
    DM_UNLOCK();
}

mx_status_t device_bind(mx_device_t* dev, const char* drv_name) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_bind(dev, drv_name);
    DM_UNLOCK();
    return r;
}

mx_status_t device_openat(mx_device_t* dev, mx_device_t** out, const char* path, uint32_t flags) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_openat(dev, out, path, flags);
    DM_UNLOCK();
    return r;
}

mx_status_t device_close(mx_device_t* dev) {
    mx_status_t r;
    DM_LOCK();
    r = devhost_device_close(dev);
    DM_UNLOCK();
    return r;
}

extern mx_handle_t root_resource_handle;

__EXPORT mx_handle_t _get_root_resource(void) {
    return root_resource_handle;
}

driver_api_t devhost_api = {
    .driver_add = _driver_add,
    .driver_remove = _driver_remove,
    .driver_unbind = _driver_unbind,
    .device_create = _device_create,
    .device_init = _device_init,
    .device_add = _device_add,
    .device_add_instance = _device_add_instance,
    .device_remove = _device_remove,
    .device_rebind = _device_rebind,
    .device_set_bindable = _device_set_bindable,
    .get_root_resource = _get_root_resource,
};
