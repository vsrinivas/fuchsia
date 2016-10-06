// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <magenta/types.h>
#include <ddk/driver.h>

typedef struct driver_api {
    void (*driver_add)(mx_driver_t* drv);
    void (*driver_remove)(mx_driver_t* drv);
    void (*driver_unbind)(mx_driver_t* drv, mx_device_t* dev);

    mx_status_t (*device_create)(mx_device_t** dev, mx_driver_t* drv,
                                 const char* name, mx_protocol_device_t* ops);
    void (*device_init)(mx_device_t* dev, mx_driver_t* drv,
                        const char* name, mx_protocol_device_t* ops);
    mx_status_t (*device_add)(mx_device_t* dev, mx_device_t* parent);
    mx_status_t (*device_add_instance)(mx_device_t* dev, mx_device_t* parent);
    mx_status_t (*device_remove)(mx_device_t* dev);
    mx_status_t (*device_rebind)(mx_device_t* dev);
    void (*device_set_bindable)(mx_device_t* dev, bool bindable);

    mx_handle_t (*get_root_resource)(void);
} driver_api_t;

void driver_api_init(driver_api_t* api);