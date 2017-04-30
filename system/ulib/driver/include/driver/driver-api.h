// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <magenta/types.h>

typedef struct mx_driver mx_driver_t;
typedef struct mx_device mx_device_t;
typedef struct mx_protocol_device mx_protocol_device_t;
typedef struct mx_device_prop mx_device_prop_t;

typedef struct driver_api {
    void (*driver_unbind)(mx_driver_t* drv, mx_device_t* dev);

    mx_status_t (*device_create)(const char* name, void* ctx,
                                 mx_protocol_device_t* ops, mx_driver_t* driver,
                                 mx_device_t** out);
    void (*device_set_protocol)(mx_device_t* dev, uint32_t proto_id, void* proto_ops);
    mx_status_t (*device_add)(mx_device_t* dev, mx_device_t* parent,
                              mx_device_prop_t* props, uint32_t prop_count,
                              const char* args, mx_handle_t rsrc);
    mx_status_t (*device_add_instance)(mx_device_t* dev, mx_device_t* parent);
    mx_status_t (*device_remove)(mx_device_t* dev);
    mx_status_t (*device_rebind)(mx_device_t* dev);
    void (*device_destroy)(mx_device_t* dev);
    void (*device_set_bindable)(mx_device_t* dev, bool bindable);

    mx_handle_t (*get_root_resource)(void);
    mx_status_t (*load_firmware)(mx_driver_t* drv, const char* path,
                                 mx_handle_t* fw, size_t* size);
} driver_api_t;

void driver_api_init(driver_api_t* api);
