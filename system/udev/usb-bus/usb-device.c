// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/common/usb.h>
#include <magenta/hw/usb-audio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "usb-device.h"
#include "usb-interface.h"
#include "util.h"

static mx_status_t usb_device_set_interface(usb_device_t* device, uint8_t interface_id, uint8_t alt_setting) {
    usb_interface_t* intf;
    list_for_every_entry(&device->children, intf, usb_interface_t, node) {
        if (usb_interface_contains_interface(intf, interface_id)) {
            return usb_interface_set_alt_setting(intf, interface_id, alt_setting);
        }
    }
    return ERR_INVALID_ARGS;
}

static void usb_device_iotxn_queue(mx_device_t* device, iotxn_t* txn) {
    usb_device_t* dev = get_usb_device(device);
    usb_protocol_data_t* usb_data = iotxn_pdata(txn, usb_protocol_data_t);
    usb_data->device_id = dev->device_id;

    // forward iotxn to HCI device
    iotxn_queue(dev->hci_device, txn);
}

static ssize_t usb_device_ioctl(mx_device_t* device, uint32_t op,
        const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    usb_device_t* dev = get_usb_device(device);

    switch (op) {
    case IOCTL_USB_GET_DEVICE_TYPE: {
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = USB_DEVICE_TYPE_DEVICE;
        return sizeof(*reply);
    }
    case IOCTL_USB_GET_DEVICE_SPEED: {
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = dev->speed;
        return sizeof(*reply);
    }
    case IOCTL_USB_GET_DEVICE_DESC: {
        usb_device_descriptor_t* descriptor = &dev->device_desc;
        if (out_len < sizeof(*descriptor)) return ERR_BUFFER_TOO_SMALL;
        memcpy(out_buf, descriptor, sizeof(*descriptor));
        return sizeof(*descriptor);
    }
    case IOCTL_USB_GET_CONFIG_DESC_SIZE:
    case IOCTL_USB_GET_DESCRIPTORS_SIZE: {
        usb_configuration_descriptor_t* descriptor = dev->config_desc;
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = le16toh(descriptor->wTotalLength);
        return sizeof(*reply);
    }
    case IOCTL_USB_GET_CONFIG_DESC:
    case IOCTL_USB_GET_DESCRIPTORS: {
        usb_configuration_descriptor_t* descriptor = dev->config_desc;
        size_t desc_length = le16toh(descriptor->wTotalLength);
        if (out_len < desc_length) return ERR_BUFFER_TOO_SMALL;
        memcpy(out_buf, descriptor, desc_length);
        return desc_length;
    }
    case IOCTL_USB_GET_STRING_DESC: {
        if (in_len != sizeof(int)) return ERR_INVALID_ARGS;
        if (out_len == 0) return 0;
        int id = *((int *)in_buf);
        char* string;
        mx_status_t result = usb_get_string_descriptor(device, id, &string);
        if (result < 0) return result;
        size_t length = strlen(string) + 1;
        if (length > out_len) {
            // truncate the string
            memcpy(out_buf, string, out_len - 1);
            ((char *)out_buf)[out_len - 1] = 0;
            length = out_len;
        } else {
            memcpy(out_buf, string, length);
        }
        free(string);
        return length;
    }
    case IOCTL_USB_SET_INTERFACE: {
        if (in_len != 2 * sizeof(int)) return ERR_INVALID_ARGS;
        int* args = (int *)in_buf;
        return usb_device_set_interface(dev, args[0], args[1]);
    }
    case IOCTL_USB_GET_CURRENT_FRAME: {
        uint64_t* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = dev->hci_protocol->get_current_frame(dev->hci_device);
        return sizeof(*reply);
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

void usb_device_remove(usb_device_t* dev) {
    usb_device_remove_interfaces(dev);
    device_remove(&dev->device);
}

static mx_status_t usb_device_release(mx_device_t* device) {
    usb_device_t* dev = get_usb_device(device);

    free(dev->config_desc);
    free(dev);

    return NO_ERROR;
}

static mx_protocol_device_t usb_device_proto = {
    .iotxn_queue = usb_device_iotxn_queue,
    .ioctl = usb_device_ioctl,
    .release = usb_device_release,
};

static mx_driver_t _driver_usb_device = {
    .name = "usb-device",
};

#define NEXT_DESCRIPTOR(header) ((usb_descriptor_header_t*)((void*)header + header->bLength))

static mx_status_t usb_device_add_interfaces(usb_device_t* parent,
                                             usb_device_descriptor_t* device_desc,
                                             usb_configuration_descriptor_t* config) {
    mx_status_t result = NO_ERROR;

    // Iterate through interfaces in first configuration and create devices for them
    usb_descriptor_header_t* header = NEXT_DESCRIPTOR(config);
    usb_descriptor_header_t* end = (usb_descriptor_header_t*)((void*)config + le16toh(config->wTotalLength));

    while (header < end) {
        if (header->bDescriptorType == USB_DT_INTERFACE) {
            usb_interface_descriptor_t* intf_desc = (usb_interface_descriptor_t*)header;
            // find end of current interface descriptor
            usb_descriptor_header_t* next = NEXT_DESCRIPTOR(intf_desc);
            while (next < end) {
                if (next->bDescriptorType == USB_DT_INTERFACE) {
                    usb_interface_descriptor_t* test_intf = (usb_interface_descriptor_t*)next;

                    // Iterate until we find the next top-level interface
                    if (
                        // include alternate interfaces in the current interface
                        (test_intf->bAlternateSetting == 0) &&
                        // Only Audio Control interface should be considered top-level
                        (test_intf->bInterfaceClass != USB_CLASS_AUDIO ||
                            test_intf->bInterfaceSubClass == USB_SUBCLASS_AUDIO_CONTROL)
                        ) {
                        // found the next top level interface
                        break;
                    }
                }
                next = NEXT_DESCRIPTOR(next);
            }

            size_t length = (void *)next - (void *)intf_desc;
            usb_interface_descriptor_t* intf_copy = malloc(length);
            if (!intf_copy) return ERR_NO_MEMORY;
            memcpy(intf_copy, intf_desc, length);

            mx_status_t status = usb_device_add_interface(parent, device_desc, intf_copy, length);
            if (status != NO_ERROR) {
                result = status;
            }

            header = next;
        } else {
            header = NEXT_DESCRIPTOR(header);
        }
    }

    return result;
}

mx_status_t usb_device_add(mx_device_t* hci_device, usb_hci_protocol_t* hci_protocol,
                           mx_device_t* parent,  uint32_t device_id, uint32_t hub_id,
                           usb_speed_t speed, usb_device_t** out_device) {

    usb_device_t* dev = calloc(1, sizeof(usb_device_t));
    if (!dev)
        return ERR_NO_MEMORY;

    // read device descriptor
    usb_device_descriptor_t* device_desc = &dev->device_desc;
    mx_status_t status = usb_device_get_descriptor(hci_device, device_id, USB_DT_DEVICE, 0,
                                                device_desc,
                                                sizeof(*device_desc));
    if (status != sizeof(*device_desc)) {
        printf("usb_device_get_descriptor failed\n");
        free(dev);
        return status;
    }

    // read configuration descriptor header to determine size
    usb_configuration_descriptor_t config_desc_header;
    status = usb_device_get_descriptor(hci_device, device_id, USB_DT_CONFIG, 0,
                                    &config_desc_header, sizeof(config_desc_header));
    if (status != sizeof(config_desc_header)) {
        printf("usb_device_get_descriptor failed\n");
        free(dev);
        return status;
    }
    uint16_t config_desc_size = letoh16(config_desc_header.wTotalLength);
    usb_configuration_descriptor_t* config_desc = malloc(config_desc_size);
    if (!config_desc) {
        free(dev);
        return ERR_NO_MEMORY;
    }

    // read full configuration descriptor
    status = usb_device_get_descriptor(hci_device, device_id, USB_DT_CONFIG, 0,
                                    config_desc, config_desc_size);
     if (status != config_desc_size) {
        printf("usb_device_get_descriptor failed\n");
        free(config_desc);
        free(dev);
        return status;
    }

    // set configuration
    status = usb_device_control(hci_device, device_id,
                             USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                             USB_REQ_SET_CONFIGURATION, config_desc->bConfigurationValue, 0,
                             NULL, 0);
    if (status < 0) {
        printf("set configuration failed\n");
        free(config_desc);
        free(dev);
        return status;
    }

    printf("* found USB device (0x%04x:0x%04x, USB %x.%x)\n", device_desc->idVendor,
           device_desc->idProduct, device_desc->bcdUSB >> 8, device_desc->bcdUSB & 0xff);

    list_initialize(&dev->children);
    dev->hci_device = hci_device;
    dev->hci_protocol = hci_protocol;
    dev->device_id = device_id;
    dev->hub_id = hub_id;
    dev->speed = speed;
    dev->config_desc = config_desc;

    char name[16];
    snprintf(name, sizeof(name), "usb-dev-%03d", device_id);

    device_init(&dev->device, &_driver_usb_device, name, &usb_device_proto);
    dev->device.protocol_id = MX_PROTOCOL_USB;

    int count = 0;
    dev->props[count++] = (mx_device_prop_t){ BIND_PROTOCOL, 0, MX_PROTOCOL_USB };
    dev->props[count++] = (mx_device_prop_t){ BIND_USB_DEVICE_TYPE, 0, USB_DEVICE_TYPE_DEVICE };
    dev->props[count++] = (mx_device_prop_t){ BIND_USB_VID, 0, device_desc->idVendor };
    dev->props[count++] = (mx_device_prop_t){ BIND_USB_PID, 0, device_desc->idProduct };
    dev->props[count++] = (mx_device_prop_t){ BIND_USB_CLASS, 0, device_desc->bDeviceClass };
    dev->props[count++] = (mx_device_prop_t){ BIND_USB_SUBCLASS, 0, device_desc->bDeviceSubClass };
    dev->props[count++] = (mx_device_prop_t){ BIND_USB_PROTOCOL, 0, device_desc->bDeviceProtocol };
    dev->device.props = dev->props;
    dev->device.prop_count = count;

    // Do not allow binding to root of a composite device.
    // Clients will bind to the child interfaces instead.
    device_set_bindable(&dev->device, false);

    status = device_add(&dev->device, parent);
    if (status == NO_ERROR) {
        *out_device = dev;
    } else {
        free(config_desc);
        free(dev);
        return status;
    }

    return usb_device_add_interfaces(dev, device_desc, config_desc);
}
