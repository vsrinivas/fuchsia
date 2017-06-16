// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <magenta/types.h>

typedef struct mx_device mx_device_t;
typedef struct mx_driver mx_driver_t;
typedef struct device_add_args device_add_args_t;
typedef struct mx_protocol_device mx_protocol_device_t;
typedef struct mx_device_prop mx_device_prop_t;
typedef struct iotxn iotxn_t;

typedef struct driver_api {
    // Device Interface - Main API
    mx_status_t (*add)(mx_driver_t* driver, mx_device_t* parent,
                       device_add_args_t* args, mx_device_t** out);
    mx_status_t (*remove)(mx_device_t* dev);
    void (*unbind)(mx_device_t* dev);
    mx_status_t (*rebind)(mx_device_t* dev);

    // Device Interface - Accessors
    const char* (*get_name)(mx_device_t* dev);
    mx_device_t* (*get_parent)(mx_device_t* dev);
    mx_status_t (*get_protocol)(mx_device_t* dev, uint32_t proto_id, void* protocol);
    mx_handle_t (*get_resource)(mx_device_t* dev);
    void (*state_clr_set)(mx_device_t* dev, mx_signals_t clr, mx_signals_t set);

    // Device Interface - Direct Ops Access
    mx_off_t (*get_size)(mx_device_t* dev);
    mx_status_t (*read)(mx_device_t* dev, void* buf, size_t count,
                        mx_off_t off, size_t* actual);
    mx_status_t (*write)(mx_device_t* dev, const void* buf, size_t count,
                         mx_off_t off, size_t* actual);
    mx_status_t (*ioctl)(mx_device_t* dev, uint32_t op,
                         const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);
    mx_status_t (*iotxn_queue)(mx_device_t* dev, iotxn_t* txn);

    // Misc Interfaces
    mx_handle_t (*get_root_resource)(void);
    mx_status_t (*load_firmware)(mx_device_t* device, const char* path,
                                 mx_handle_t* fw, size_t* size);
} driver_api_t;

void driver_api_init(driver_api_t* api);
