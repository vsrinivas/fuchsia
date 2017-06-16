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

// Device Interfaces

__EXPORT mx_status_t device_add_from_driver(mx_driver_t* driver, mx_device_t* parent,
                                            device_add_args_t* args, mx_device_t** out) {
    return API->add(driver, parent, args, out);
}

__EXPORT mx_status_t device_remove(mx_device_t* dev) {
    return API->remove(dev);
}

__EXPORT void device_unbind(mx_device_t* dev) {
    API->unbind(dev);
}

__EXPORT mx_status_t device_rebind(mx_device_t* dev) {
    return API->rebind(dev);
}

__EXPORT const char* device_get_name(mx_device_t* dev) {
    return API->get_name(dev);
}

__EXPORT mx_device_t* device_get_parent(mx_device_t* dev) {
    return API->get_parent(dev);
}

__EXPORT mx_status_t device_get_protocol(mx_device_t* dev, uint32_t proto_id,
                                         void* protocol) {
    return API->get_protocol(dev, proto_id, protocol);
}

__EXPORT mx_handle_t device_get_resource(mx_device_t* dev) {
    return API->get_resource(dev);
}

__EXPORT mx_off_t device_get_size(mx_device_t* dev) {
    return API->get_size(dev);
}

__EXPORT mx_status_t device_read(mx_device_t* dev, void* buf, size_t count,
                                 mx_off_t off, size_t* actual) {
    return API->read(dev, buf, count, off, actual);
}

__EXPORT mx_status_t device_write(mx_device_t* dev, const void* buf, size_t count,
                                  mx_off_t off, size_t* actual) {
    return API->write(dev, buf, count, off, actual);
}

__EXPORT mx_status_t device_ioctl(mx_device_t* dev, uint32_t op,
                                  const void* in_buf, size_t in_len,
                                  void* out_buf, size_t out_len, size_t* out_actual) {
    return API->ioctl(dev, op, in_buf, in_len, out_buf, out_len, out_actual);
}

__EXPORT mx_status_t device_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    return API->iotxn_queue(dev, txn);
}

__EXPORT void device_state_clr_set(mx_device_t* dev, mx_signals_t clearflag, mx_signals_t setflag) {
    API->state_clr_set(dev, clearflag, setflag);
}


// Misc Interfaces

__EXPORT mx_handle_t get_root_resource(void) {
    return API->get_root_resource();
}

__EXPORT mx_status_t load_firmware(mx_device_t* device, const char* path,
                                   mx_handle_t* fw, size_t* size) {
    return API->load_firmware(device, path, fw, size);
}
