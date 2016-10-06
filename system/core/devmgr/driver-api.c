// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/compiler.h>
#include "driver-api.h"

static driver_api_t* API;

__EXPORT void driver_api_init(driver_api_t* api) {
    if (API == NULL) {
        API = api;
    }
}

__EXPORT void driver_add(mx_driver_t* drv) {
    API->driver_add(drv);
}

__EXPORT void driver_remove(mx_driver_t* drv) {
    API->driver_remove(drv);
}

__EXPORT void driver_unbind(mx_driver_t* drv, mx_device_t* dev) {
    API->driver_unbind(drv, dev);
}


__EXPORT mx_status_t device_create(mx_device_t** dev, mx_driver_t* drv,
                                   const char* name, mx_protocol_device_t* ops) {
    return API->device_create(dev, drv, name, ops);
}

__EXPORT void device_init(mx_device_t* dev, mx_driver_t* drv,
                          const char* name, mx_protocol_device_t* ops) {
    API->device_init(dev, drv, name, ops);
}

__EXPORT mx_status_t device_add(mx_device_t* dev, mx_device_t* parent) {
    return API->device_add(dev, parent);
}

__EXPORT mx_status_t device_add_instance(mx_device_t* dev, mx_device_t* parent) {
    return API->device_add_instance(dev, parent);
}

__EXPORT mx_status_t device_remove(mx_device_t* dev) {
    return API->device_remove(dev);
}

__EXPORT mx_status_t device_rebind(mx_device_t* dev) {
    return API->device_rebind(dev);
}

__EXPORT void device_set_bindable(mx_device_t* dev, bool bindable) {
    return API->device_set_bindable(dev, bindable);
}


__EXPORT mx_handle_t get_root_resource(void) {
    return API->get_root_resource();
}
