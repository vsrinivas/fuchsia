// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <hw/usb.h>
#include <system/listnode.h>

typedef int direction_t;
typedef int endpoint_type;

// Values in this enum match those used in XHCI and other parts of the USB specification
typedef enum {
    USB_SPEED_UNDEFINED = 0,
    USB_SPEED_FULL = 1,
    USB_SPEED_LOW = 2,
    USB_SPEED_HIGH = 3,
    USB_SPEED_SUPER = 4,
} usb_speed_t;

typedef struct usb_endpoint {
    usb_endpoint_descriptor_t* descriptor;
    int endpoint;
    direction_t direction;
    int toggle;
    int maxpacketsize;
    endpoint_type type;
    int interval; /* expressed as binary logarithm of the number
			 of microframes (i.e. t = 125us * 2^interval) */
} usb_endpoint_t;

typedef struct usb_class_descriptor {
    usb_descriptor_header_t* header;
    list_node_t node;
} usb_class_descriptor_t;

typedef struct usb_interface {
    usb_interface_descriptor_t* descriptor;
    struct usb_interface* alt_interfaces;
    int num_alt_interfaces;
    usb_endpoint_t* endpoints;
    int num_endpoints;
    list_node_t class_descriptors;
} usb_interface_t;

typedef struct usb_configuration {
    usb_configuration_descriptor_t* descriptor;
    usb_interface_t* interfaces;
    int num_interfaces;
} usb_configuration_t;

typedef struct usb_device_config {
    usb_device_descriptor_t* descriptor;
    usb_configuration_t* configurations;
    int num_configurations;
} usb_device_config_t;

typedef struct usb_request {
    uint8_t* buffer;          // pointer to DMA memory
    uint16_t buffer_length;   // size of DMA buffer
    uint16_t transfer_length; // number of bytes to transfer
    mx_status_t status;
    void (*complete_cb)(struct usb_request* request);
    usb_endpoint_t* endpoint;
    void* client_data; // for client use
    void* driver_data; // for driver use

    // node can be used by client when request is not queued
    list_node_t node;
} usb_request_t;

typedef struct usb_device_protocol {
    usb_request_t* (*alloc_request)(mx_device_t* dev, usb_endpoint_t* ep, uint16_t length);
    void (*free_request)(mx_device_t* dev, usb_request_t* request);

    mx_status_t (*queue_request)(mx_device_t* dev, usb_request_t* request);
    mx_status_t (*control)(mx_device_t* dev, uint8_t request_type, uint8_t request, uint16_t value,
                           uint16_t index, void* data, uint16_t length);

    mx_status_t (*get_config)(mx_device_t* dev, usb_device_config_t** config);
    usb_speed_t (*get_speed)(mx_device_t* device);
    int (*get_address)(mx_device_t* device);
} usb_device_protocol_t;
