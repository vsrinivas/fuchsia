// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/common/usb.h>
#include <ddk/protocol/usb-device.h>
#include <stdio.h>

static void usb_control_complete(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t*)cookie);
}

mx_status_t usb_control(mx_device_t* device, uint8_t request_type, uint8_t request,
                        uint16_t value, uint16_t index, void* data, size_t length) {
    iotxn_t* txn;

    mx_status_t status = iotxn_alloc(&txn, 0, length, 0);
    if (status != NO_ERROR) return status;
    txn->protocol = MX_PROTOCOL_USB_DEVICE;
    usb_protocol_data_t* proto_data = iotxn_pdata(txn, usb_protocol_data_t);

    // fill in protocol data
    usb_setup_t* setup = &proto_data->setup;
    setup->bmRequestType = request_type;
    setup->bRequest = request;
    setup->wValue = value;
    setup->wIndex = index;
    setup->wLength = length;
    proto_data->ep_address = 0;

    bool out = !!((request_type & USB_DIR_MASK) == USB_DIR_OUT);
    if (length > 0 && out) {
        txn->ops->copyto(txn, data, length, 0);
    }

    completion_t completion = COMPLETION_INIT;

    txn->length = length;
    txn->complete_cb = usb_control_complete;
    txn->cookie = &completion;
    iotxn_queue(device, txn);
    completion_wait(&completion, MX_TIME_INFINITE);

    status = txn->status;
    if (status == NO_ERROR) {
        status = txn->actual;

        if (length > 0 && !out) {
            txn->ops->copyfrom(txn, data, txn->actual, 0);
        }
    }
    txn->ops->release(txn);
    return status;
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
