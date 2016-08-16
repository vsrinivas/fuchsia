// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/common/usb.h>
#include <ddk/protocol/usb-device.h>
#include <stdio.h>

mx_status_t usb_control(mx_device_t* device, uint8_t request_type, uint8_t request,
                        uint16_t value, uint16_t index, void* data, size_t length) {

    usb_device_protocol_t* protocol;
    if (device_get_protocol(device, MX_PROTOCOL_USB_DEVICE, (void**)&protocol)) {
        printf("not a USB device in usb_control()\n");
        return ERR_INVALID_ARGS;
    }
    return protocol->control(device, request_type, request, value, index, data, length);
}

mx_status_t usb_get_descriptor(mx_device_t* device, uint8_t request_type, uint16_t type,
                               uint16_t index, void* data, size_t length) {
    return usb_control(device, request_type | USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                       type << 8 | index, 0, data, length);
}

mx_status_t usb_get_status(mx_device_t* device, uint8_t request_type, uint16_t index,
                          void* data, size_t length) {
    return usb_control(device, request_type | USB_DIR_IN, USB_REQ_GET_STATUS, 0,
                       index, data, length);
}

mx_status_t usb_set_configuration(mx_device_t* device, int config) {
    return usb_control(device, USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                       USB_REQ_SET_CONFIGURATION, config, 0, NULL, 0);
}

mx_status_t usb_set_feature(mx_device_t* device, uint8_t request_type, int feature, int index) {
    return usb_control(device, request_type, USB_REQ_SET_FEATURE, feature, index, NULL, 0);
}

mx_status_t usb_clear_feature(mx_device_t* device, uint8_t request_type, int feature, int index) {
    return usb_control(device, request_type, USB_REQ_CLEAR_FEATURE, feature, index, NULL, 0);
}
