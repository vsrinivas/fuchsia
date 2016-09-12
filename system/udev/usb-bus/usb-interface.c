// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/common/usb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "usb-device.h"
#include "usb-interface.h"

// Represents an inteface within a composite device
typedef struct {
    mx_device_t device;

    mx_device_t* hci_device;
    uint32_t device_id;

    usb_interface_descriptor_t* interface_desc;
    size_t interface_desc_length;

    mx_device_prop_t props[7];

    list_node_t node;
} usb_interface_t;
#define get_usb_interface(dev) containerof(dev, usb_interface_t, device)

static void usb_interface_iotxn_queue(mx_device_t* device, iotxn_t* txn) {
    usb_interface_t* intf = get_usb_interface(device);
    usb_protocol_data_t* usb_data = iotxn_pdata(txn, usb_protocol_data_t);
    usb_data->device_id = intf->device_id;

    // forward iotxn to HCI device
    iotxn_queue(intf->hci_device, txn);
}

static ssize_t usb_interface_ioctl(mx_device_t* device, uint32_t op,
        const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    usb_interface_t* intf = get_usb_interface(device);

    switch (op) {
    case IOCTL_USB_GET_DEVICE_TYPE: {
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = USB_DEVICE_TYPE_INTERFACE;
        return sizeof(*reply);
    }
    case IOCTL_USB_GET_DEVICE_SPEED:
    case IOCTL_USB_GET_DEVICE_DESC:
    case IOCTL_USB_GET_CONFIG_DESC_SIZE:
    case IOCTL_USB_GET_CONFIG_DESC:
    case IOCTL_USB_GET_STRING_DESC:
        return device->parent->ops->ioctl(device->parent, op, in_buf, in_len, out_buf, out_len);
    case IOCTL_USB_GET_DESCRIPTORS_SIZE: {
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = intf->interface_desc_length;
        return sizeof(*reply);
    }
    case IOCTL_USB_GET_DESCRIPTORS: {
        void* descriptors = intf->interface_desc;
        size_t desc_length = intf->interface_desc_length;
        if (out_len < desc_length) return ERR_BUFFER_TOO_SMALL;
        memcpy(out_buf, descriptors, desc_length);
        return desc_length;
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static mx_status_t usb_interface_release(mx_device_t* device) {
    usb_interface_t* intf = get_usb_interface(device);

    free(intf->interface_desc);
    free(intf);

    return NO_ERROR;
}

static mx_protocol_device_t usb_interface_proto = {
    .iotxn_queue = usb_interface_iotxn_queue,
    .ioctl = usb_interface_ioctl,
    .release = usb_interface_release,
};

static mx_driver_t _driver_usb_interface BUILTIN_DRIVER = {
    .name = "usb-interface",
};

mx_status_t usb_device_add_interface(usb_device_t* device,
                                     usb_device_descriptor_t* device_descriptor,
                                     usb_interface_descriptor_t* interface_desc,
                                     size_t interface_desc_length) {
    usb_interface_t* intf = calloc(1, sizeof(usb_interface_t));
    if (!intf)
        return ERR_NO_MEMORY;

    intf->hci_device = device->hci_device;
    intf->device_id = device->device_id;
    intf->interface_desc = interface_desc;
    intf->interface_desc_length = interface_desc_length;

    char name[20];
    snprintf(name, sizeof(name), "usb-dev-%03d-%d", device->device_id, interface_desc->bInterfaceNumber);

    device_init(&intf->device, &_driver_usb_interface, name, &usb_interface_proto);
    intf->device.protocol_id = MX_PROTOCOL_USB;

    int count = 0;
    intf->props[count++] = (mx_device_prop_t){ BIND_PROTOCOL, 0, MX_PROTOCOL_USB };
    intf->props[count++] = (mx_device_prop_t){ BIND_USB_DEVICE_TYPE, 0, USB_DEVICE_TYPE_INTERFACE };
    intf->props[count++] = (mx_device_prop_t){ BIND_USB_VID, 0, device_descriptor->idVendor };
    intf->props[count++] = (mx_device_prop_t){ BIND_USB_PID, 0, device_descriptor->idProduct };
    intf->props[count++] = (mx_device_prop_t){ BIND_USB_IFC_CLASS, 0, interface_desc->bInterfaceClass };
    intf->props[count++] = (mx_device_prop_t){ BIND_USB_IFC_SUBCLASS, 0, interface_desc->bInterfaceSubClass };
    intf->props[count++] = (mx_device_prop_t){ BIND_USB_IFC_PROTOCOL, 0, interface_desc->bInterfaceProtocol };
    intf->device.props = intf->props;
    intf->device.prop_count = count;

    mx_status_t status = device_add(&intf->device, &device->device);
    if (status == NO_ERROR) {
        list_add_head(&device->children, &intf->node);
    } else {
        free(interface_desc);
        free(intf);
    }
    return status;
}

void usb_device_remove_interfaces(usb_device_t* device) {
    usb_interface_t* intf;
    while ((intf = list_remove_head_type(&device->children, usb_interface_t, node)) != NULL) {
        device_remove(&intf->device);
    }
}
