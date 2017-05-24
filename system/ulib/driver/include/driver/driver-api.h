// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <magenta/types.h>

typedef struct mx_device mx_device_t;
typedef struct device_add_args device_add_args_t;
typedef struct mx_protocol_device mx_protocol_device_t;
typedef struct mx_device_prop mx_device_prop_t;

typedef struct driver_api {
    void (*device_unbind)(mx_device_t* dev);

    mx_status_t (*device_add)(mx_device_t* parent, device_add_args_t* args,
                              mx_device_t** out);
    mx_status_t (*device_remove)(mx_device_t* dev);
    mx_status_t (*device_rebind)(mx_device_t* dev);

    mx_handle_t (*get_root_resource)(void);
    mx_status_t (*load_firmware)(mx_device_t* device, const char* path,
                                 mx_handle_t* fw, size_t* size);
} driver_api_t;

void driver_api_init(driver_api_t* api);
