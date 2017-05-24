// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/compiler.h>
#include <ddk/device.h>
#include <driver/driver-api.h>
#include <ddk/device.h>
#include <ddk/driver.h>

static driver_api_t* API;

__EXPORT void driver_api_init(driver_api_t* api) {
    if (API == NULL) {
        API = api;
    }
}

__EXPORT void device_unbind(mx_device_t* dev) {
    API->device_unbind(dev);
}

__EXPORT mx_status_t device_add(mx_device_t* parent, device_add_args_t* args, mx_device_t** out) {
    return API->device_add(parent, args, out);
}

__EXPORT mx_status_t device_op_get_protocol(mx_device_t* dev, uint32_t proto_id,
                                                 void** protocol) {
    if (dev->ops->get_protocol) {
        return dev->ops->get_protocol(dev->ctx, proto_id, protocol);
    }

    if (proto_id == MX_PROTOCOL_DEVICE) {
        *protocol = dev->ops;
        return NO_ERROR;
    }
    if ((proto_id == dev->protocol_id) && (dev->protocol_ops != NULL)) {
        *protocol = dev->protocol_ops;
        return NO_ERROR;
    }
    return ERR_NOT_SUPPORTED;
}

__EXPORT mx_status_t device_remove(mx_device_t* dev) {
    return API->device_remove(dev);
}

__EXPORT mx_status_t device_rebind(mx_device_t* dev) {
    return API->device_rebind(dev);
}

__EXPORT mx_handle_t get_root_resource(void) {
    return API->get_root_resource();
}

__EXPORT mx_status_t load_firmware(mx_device_t* device, const char* path,
                                   mx_handle_t* fw, size_t* size) {
    return API->load_firmware(device, path, fw, size);
}
