// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/usb.h>
#include <magenta/compiler.h>
#include <magenta/hw/usb.h>

__BEGIN_CDECLS;

mx_status_t usb_control(mx_device_t* device, uint8_t request_type, uint8_t request,
                               uint16_t value, uint16_t index, void* data, size_t length);

mx_status_t usb_get_descriptor(mx_device_t* device, uint8_t request_type, uint16_t type,
                               uint16_t index, void* data, size_t length);

usb_speed_t usb_get_speed(mx_device_t* device);

mx_status_t usb_get_status(mx_device_t* device, uint8_t request_type, uint16_t index,
                          void* data, size_t length);

mx_status_t usb_set_configuration(mx_device_t* device, int config);

mx_status_t usb_set_interface(mx_device_t* device, int interface_number, int alt_setting);

mx_status_t usb_set_feature(mx_device_t* device, uint8_t request_type, int feature, int index);

mx_status_t usb_clear_feature(mx_device_t* device, uint8_t request_type, int feature, int index);

// Resets an endpoint that is in a halted or error state.
// Endpoints will be halted if the device returns a STALL in response to a USB transaction.
// When that occurs, the transaction will fail with ERR_IO_REFUSED.
// usb_reset_endpoint() the endpoint to normal running state.
mx_status_t usb_reset_endpoint(mx_device_t* device, uint8_t ep_address);

// returns the maximum amount of data that can be transferred on an endpoint in a single transaction.
size_t usb_get_max_transfer_size(mx_device_t* device, uint8_t ep_address);

// helper function for allocating iotxns for USB transfers
iotxn_t* usb_alloc_iotxn(uint8_t ep_address, size_t data_size);

// sets the frame number in a USB iotxn for scheduling an isochronous transfer
static inline void usb_iotxn_set_frame(iotxn_t* txn, uint64_t frame) {
    ((usb_protocol_data_t *)iotxn_pdata(txn, usb_protocol_data_t))->frame = frame;
}

// Utilities for iterating through descriptors within a device's USB configuration descriptor
typedef struct {
    uint8_t* desc;      // start of configuration descriptor
    uint8_t* desc_end;  // end of configuration descriptor
    uint8_t* current;   // current position in configuration descriptor
} usb_desc_iter_t;

// initializes a usb_desc_iter_t
mx_status_t usb_desc_iter_init(mx_device_t* device, usb_desc_iter_t* iter);

// releases resources in a usb_desc_iter_t
void usb_desc_iter_release(usb_desc_iter_t* iter);

// resets iterator to the beginning
void usb_desc_iter_reset(usb_desc_iter_t* iter);

// returns the next descriptor
usb_descriptor_header_t* usb_desc_iter_next(usb_desc_iter_t* iter);

// returns the next descriptor without incrementing the iterator
usb_descriptor_header_t* usb_desc_iter_peek(usb_desc_iter_t* iter);

// returns the next interface descriptor, optionally skipping alternate interfaces
usb_interface_descriptor_t* usb_desc_iter_next_interface(usb_desc_iter_t* iter, bool skip_alt);

// returns the next endpoint descriptor within the current interface
usb_endpoint_descriptor_t* usb_desc_iter_next_endpoint(usb_desc_iter_t* iter);

usb_configuration_descriptor_t* usb_desc_iter_get_config_desc(usb_desc_iter_t* iter);

__END_CDECLS;
