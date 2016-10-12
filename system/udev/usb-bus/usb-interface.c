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
#include "util.h"

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
        // other ioctls are handled by top level device
        return device->parent->ops->ioctl(device->parent, op, in_buf, in_len, out_buf, out_len);
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

static mx_driver_t _driver_usb_interface = {
    .name = "usb-interface",
};

#define NEXT_DESCRIPTOR(header) ((usb_descriptor_header_t*)((void*)header + header->bLength))

static mx_status_t usb_interface_enable_endpoint(usb_interface_t* intf,
                                                 usb_endpoint_descriptor_t* ep,
                                                 bool enable) {
    mx_status_t status = intf->hci_protocol->enable_endpoint(intf->hci_device, intf->device_id, ep,
                                                             enable);
    if (status != NO_ERROR) {
        printf("usb_interface_enable_endpoint failed\n");
    }
    return status;
}

static mx_status_t usb_interface_configure_endpoints(usb_interface_t* intf, uint8_t interface_id,
                                                     uint8_t alt_setting) {
    usb_endpoint_descriptor_t* new_endpoints[USB_MAX_EPS];
    memset(new_endpoints, 0, sizeof(new_endpoints));
    mx_status_t status = NO_ERROR;

    // iterate through our descriptors to find which endpoints should be active
    usb_descriptor_header_t* header = (usb_descriptor_header_t *)intf->interface_desc;
    usb_descriptor_header_t* end = (usb_descriptor_header_t*)((void*)header + intf->interface_desc_length);

    bool enable_endpoints = false;
    while (header < end) {
        if (header->bDescriptorType == USB_DT_INTERFACE) {
            usb_interface_descriptor_t* intf_desc = (usb_interface_descriptor_t*)header;
            enable_endpoints = (intf_desc->bAlternateSetting == alt_setting);
        } else if (header->bDescriptorType == USB_DT_ENDPOINT && enable_endpoints) {
            usb_endpoint_descriptor_t* ep = (usb_endpoint_descriptor_t*)header;
            new_endpoints[get_usb_endpoint_index(ep)] = ep;
        }
        header = NEXT_DESCRIPTOR(header);
    }

    // update to new set of endpoints
    // FIXME - how do we recover if we fail half way through processing the endpoints?
    for (size_t i = 0; i < countof(new_endpoints); i++) {
        usb_endpoint_descriptor_t* old_ep = intf->active_endpoints[i];
        usb_endpoint_descriptor_t* new_ep = new_endpoints[i];
        if (old_ep != new_ep) {
            if (old_ep) {
                mx_status_t ret = usb_interface_enable_endpoint(intf, old_ep, false);
                if (ret != NO_ERROR) status = ret;
            }
            if (new_ep) {
                mx_status_t ret = usb_interface_enable_endpoint(intf, new_ep, true);
                if (ret != NO_ERROR) status = ret;
            }
            intf->active_endpoints[i] = new_ep;
        }
    }
    return status;
}

mx_status_t usb_device_add_interface(usb_device_t* device,
                                     usb_device_descriptor_t* device_desc,
                                     usb_interface_descriptor_t* interface_desc,
                                     size_t interface_desc_length) {
    usb_interface_t* intf = calloc(1, sizeof(usb_interface_t));
    if (!intf)
        return ERR_NO_MEMORY;

    intf->hci_device = device->hci_device;
    intf->hci_protocol = device->hci_protocol;
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
    intf->props[count++] = (mx_device_prop_t){ BIND_USB_VID, 0, device_desc->idVendor };
    intf->props[count++] = (mx_device_prop_t){ BIND_USB_PID, 0, device_desc->idProduct };
    if (device_desc->bDeviceClass != 0) {
        intf->props[count++] = (mx_device_prop_t){ BIND_USB_CLASS, 0, device_desc->bDeviceClass };
        intf->props[count++] = (mx_device_prop_t){ BIND_USB_SUBCLASS, 0, device_desc->bDeviceSubClass };
        intf->props[count++] = (mx_device_prop_t){ BIND_USB_PROTOCOL, 0, device_desc->bDeviceProtocol };
    } else {
        intf->props[count++] = (mx_device_prop_t){ BIND_USB_IFC_CLASS, 0, interface_desc->bInterfaceClass };
        intf->props[count++] = (mx_device_prop_t){ BIND_USB_IFC_SUBCLASS, 0, interface_desc->bInterfaceSubClass };
        intf->props[count++] = (mx_device_prop_t){ BIND_USB_IFC_PROTOCOL, 0, interface_desc->bInterfaceProtocol };
    }
    intf->device.props = intf->props;
    intf->device.prop_count = count;

    mx_status_t status = usb_interface_configure_endpoints(intf, interface_desc->bInterfaceNumber, 0);
    if (status != NO_ERROR) return status;

    // need to do this first so usb_device_set_interface() can be called from driver bind
    list_add_head(&device->children, &intf->node);
    status = device_add(&intf->device, &device->device);
    if (status != NO_ERROR) {
        list_delete(&intf->node);
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

uint32_t usb_interface_get_device_id(mx_device_t* device) {
    usb_interface_t* intf = get_usb_interface(device);
    return intf->device_id;
}

bool usb_interface_contains_interface(usb_interface_t* intf, uint8_t interface_id) {
    usb_descriptor_header_t* header = (usb_descriptor_header_t *)intf->interface_desc;
    usb_descriptor_header_t* end = (usb_descriptor_header_t*)((void*)header + intf->interface_desc_length);

    while (header < end) {
        if (header->bDescriptorType == USB_DT_INTERFACE) {
            usb_interface_descriptor_t* intf_desc = (usb_interface_descriptor_t*)header;
            if (intf_desc->bInterfaceNumber == interface_id) {
                return true;
            }
        }
        header = NEXT_DESCRIPTOR(header);
    }
    return false;
}

mx_status_t usb_interface_set_alt_setting(usb_interface_t* intf, uint8_t interface_id,
                                          uint8_t alt_setting) {
    mx_status_t status = usb_interface_configure_endpoints(intf, interface_id, alt_setting);
    if (status != NO_ERROR) return status;

    return usb_device_control(intf->hci_device, intf->device_id,
                              USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE,
                              USB_REQ_SET_INTERFACE, alt_setting, interface_id, NULL, 0);
}
