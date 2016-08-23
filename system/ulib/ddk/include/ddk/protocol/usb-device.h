// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <hw/usb.h>
#include <hw/usb-hub.h>
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
    int maxpacketsize;
    endpoint_type type;
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

// protocol data for iotxns
typedef struct usb_protocol_data {
    usb_setup_t setup;      // for control transactions
    uint32_t device_id;
    uint8_t ep_address;     // bEndpointAddress from endpoint descriptor
} usb_protocol_data_t;

typedef struct usb_device_protocol {
    usb_request_t* (*alloc_request)(mx_device_t* dev, usb_endpoint_t* ep, uint16_t length);
    void (*free_request)(mx_device_t* dev, usb_request_t* request);
    mx_status_t (*queue_request)(mx_device_t* dev, usb_request_t* request);

    mx_status_t (*get_config)(mx_device_t* dev, usb_device_config_t** config);
    usb_speed_t (*get_speed)(mx_device_t* device);

    // These are only used by hub driver
    mx_status_t (*configure_hub)(mx_device_t* device, usb_speed_t speed,
                                    usb_hub_descriptor_t* descriptor);
    mx_status_t (*hub_device_added)(mx_device_t* device, int port, usb_speed_t speed);
    mx_status_t (*hub_device_removed)(mx_device_t* device, int port);
} usb_device_protocol_t;

// For use by HCI controller drivers
mx_status_t usb_add_device(mx_device_t* hcidev, int address, usb_speed_t speed,
                           usb_device_descriptor_t* device_descriptor,
                           usb_configuration_descriptor_t** config_descriptors,
                           mx_device_t** out_device);
