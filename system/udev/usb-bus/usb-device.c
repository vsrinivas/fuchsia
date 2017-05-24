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

static mx_status_t usb_device_add_interfaces(usb_device_t* parent,
                                             usb_configuration_descriptor_t* config);

static mx_status_t usb_device_set_interface(usb_device_t* device, uint8_t interface_id, uint8_t alt_setting) {
    usb_interface_t* intf;
    list_for_every_entry(&device->children, intf, usb_interface_t, node) {
        if (usb_interface_contains_interface(intf, interface_id)) {
            return usb_interface_set_alt_setting(intf, interface_id, alt_setting);
        }
    }
    return ERR_INVALID_ARGS;
}

static usb_configuration_descriptor_t* get_config_desc(usb_device_t* dev, int config) {
    int num_configurations = dev->device_desc.bNumConfigurations;
    for (int i = 0; i < num_configurations; i++) {
        usb_configuration_descriptor_t* desc = dev->config_descs[i];
        if (desc->bConfigurationValue == config) {
            return desc;
        }
    }
    return NULL;
}

static mx_status_t usb_device_set_configuration(usb_device_t* dev, int config) {
    int num_configurations = dev->device_desc.bNumConfigurations;
    usb_configuration_descriptor_t* config_desc = NULL;
    int config_index = -1;

    // validate config and get the new current_config_index
    for (int i = 0; i < num_configurations; i++) {
        usb_configuration_descriptor_t* desc = dev->config_descs[i];
        if (desc->bConfigurationValue == config) {
            config_desc = desc;
            config_index = i;
            break;
        }
    }
    if (!config_desc) return ERR_INVALID_ARGS;

    // set configuration
    mx_status_t status = usb_device_control(dev->hci_mxdev, dev->device_id,
                             USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                             USB_REQ_SET_CONFIGURATION, config, 0,
                             NULL, 0);
    if (status < 0) {
        printf("set configuration failed\n");
        return status;
    }

    dev->current_config_index = config_index;

    // tear down and recreate the subdevices for our interfaces
    usb_device_remove_interfaces(dev);
    return usb_device_add_interfaces(dev, config_desc);
}

static mx_status_t usb_device_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                    void* out_buf, size_t out_len, size_t* out_actual) {
    usb_device_t* dev = ctx;

    switch (op) {
    case IOCTL_USB_GET_DEVICE_TYPE: {
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = USB_DEVICE_TYPE_DEVICE;
        *out_actual = sizeof(*reply);
        return NO_ERROR;
    }
    case IOCTL_USB_GET_DEVICE_SPEED: {
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = dev->speed;
        *out_actual = sizeof(*reply);
        return NO_ERROR;
    }
    case IOCTL_USB_GET_DEVICE_DESC: {
        usb_device_descriptor_t* descriptor = &dev->device_desc;
        if (out_len < sizeof(*descriptor)) return ERR_BUFFER_TOO_SMALL;
        memcpy(out_buf, descriptor, sizeof(*descriptor));
        *out_actual = sizeof(*descriptor);
        return NO_ERROR;
    }
    case IOCTL_USB_GET_CONFIG_DESC_SIZE: {
        if (in_len != sizeof(int)) return ERR_INVALID_ARGS;
        int config = *((int *)in_buf);
        int* reply = out_buf;
        usb_configuration_descriptor_t* descriptor = get_config_desc(dev, config);
        if (!descriptor) {
            return ERR_INVALID_ARGS;
        }
        *reply = le16toh(descriptor->wTotalLength);
        *out_actual = sizeof(*reply);
        return NO_ERROR;
    }
    case IOCTL_USB_GET_DESCRIPTORS_SIZE: {
        usb_configuration_descriptor_t* descriptor = dev->config_descs[dev->current_config_index];
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = le16toh(descriptor->wTotalLength);
        *out_actual = sizeof(*reply);
        return NO_ERROR;
    }
    case IOCTL_USB_GET_CONFIG_DESC: {
        if (in_len != sizeof(int)) return ERR_INVALID_ARGS;
        int config = *((int *)in_buf);
        usb_configuration_descriptor_t* descriptor = get_config_desc(dev, config);
        if (!descriptor) {
            return ERR_INVALID_ARGS;
        }
        size_t desc_length = le16toh(descriptor->wTotalLength);
        if (out_len < desc_length) return ERR_BUFFER_TOO_SMALL;
        memcpy(out_buf, descriptor, desc_length);
        return desc_length;
        *out_actual = desc_length;
        return NO_ERROR;
    }
    case IOCTL_USB_GET_DESCRIPTORS: {
        usb_configuration_descriptor_t* descriptor = dev->config_descs[dev->current_config_index];
        size_t desc_length = le16toh(descriptor->wTotalLength);
        if (out_len < desc_length) return ERR_BUFFER_TOO_SMALL;
        memcpy(out_buf, descriptor, desc_length);
        *out_actual = desc_length;
        return NO_ERROR;
    }
    case IOCTL_USB_GET_STRING_DESC: {
        if (in_len != sizeof(int)) return ERR_INVALID_ARGS;
        if (out_len == 0) return 0;
        int id = *((int *)in_buf);
        char string[MAX_USB_STRING_LEN];
        mx_status_t result = usb_device_get_string_descriptor(dev->hci_mxdev, dev->device_id, id,
                                                              string, sizeof(string));
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
        *out_actual = length;
        return NO_ERROR;
    }
    case IOCTL_USB_SET_INTERFACE: {
        if (in_len != 2 * sizeof(int)) return ERR_INVALID_ARGS;
        int* args = (int *)in_buf;
        return usb_device_set_interface(dev, args[0], args[1]);
    }
    case IOCTL_USB_GET_CURRENT_FRAME: {
        uint64_t* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = dev->hci_protocol->get_current_frame(dev->hci_mxdev);
        *out_actual = sizeof(*reply);
        return NO_ERROR;
    }
    case IOCTL_USB_GET_DEVICE_ID: {
        uint64_t* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = dev->device_id;
        *out_actual = sizeof(*reply);
        return NO_ERROR;
    }
    case IOCTL_USB_GET_DEVICE_HUB_ID: {
        uint64_t* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = dev->hub_id;
        *out_actual = sizeof(*reply);
        return NO_ERROR;
    }
    case IOCTL_USB_GET_CONFIGURATION: {
        int* reply = out_buf;
        if (out_len != sizeof(*reply)) return ERR_INVALID_ARGS;
        usb_configuration_descriptor_t* descriptor = dev->config_descs[dev->current_config_index];
        *reply = descriptor->bConfigurationValue;
        *out_actual = sizeof(*reply);
        return NO_ERROR;
    }
    case IOCTL_USB_SET_CONFIGURATION: {
        if (in_len != sizeof(int)) return ERR_INVALID_ARGS;
        int config = *((int *)in_buf);
        printf("IOCTL_USB_SET_CONFIGURATION %d\n", config);
        return usb_device_set_configuration(dev, config);
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

void usb_device_remove(usb_device_t* dev) {
    usb_device_remove_interfaces(dev);
    device_remove(dev->mxdev);
}

static void usb_device_release(void* ctx) {
    usb_device_t* dev = ctx;

    if (dev->config_descs) {
        int num_configurations = dev->device_desc.bNumConfigurations;
        for (int i = 0; i < num_configurations; i++) {
            if (dev->config_descs[i]) free(dev->config_descs[i]);
        }
        free(dev->config_descs);
    }
    free(dev);
}

static mx_protocol_device_t usb_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = usb_device_ioctl,
    .release = usb_device_release,
};

#define NEXT_DESCRIPTOR(header) ((usb_descriptor_header_t*)((void*)header + header->bLength))

static mx_status_t usb_device_add_interfaces(usb_device_t* parent,
                                             usb_configuration_descriptor_t* config) {
    usb_device_descriptor_t* device_desc = &parent->device_desc;
    mx_status_t result = NO_ERROR;

    // Iterate through interfaces in first configuration and create devices for them
    usb_descriptor_header_t* header = NEXT_DESCRIPTOR(config);
    usb_descriptor_header_t* end = (usb_descriptor_header_t*)((void*)config + le16toh(config->wTotalLength));

    while (header < end) {
        if (header->bDescriptorType == USB_DT_INTERFACE_ASSOCIATION) {
            usb_interface_assoc_descriptor_t* assoc_desc = (usb_interface_assoc_descriptor_t*)header;
            int interface_count = assoc_desc->bInterfaceCount;

            // find end of this interface association
            usb_descriptor_header_t* next = NEXT_DESCRIPTOR(assoc_desc);
            while (next < end) {
                if (next->bDescriptorType == USB_DT_INTERFACE_ASSOCIATION) {
                    break;
                } else if (next->bDescriptorType == USB_DT_INTERFACE) {
                    usb_interface_descriptor_t* test_intf = (usb_interface_descriptor_t*)next;

                    if (test_intf->bAlternateSetting == 0) {
                        if (interface_count == 0) {
                            break;
                        }
                        interface_count--;
                    }
                }
                next = NEXT_DESCRIPTOR(next);
            }

            size_t length = (void *)next - (void *)assoc_desc;
            usb_interface_assoc_descriptor_t* assoc_copy = malloc(length);
            if (!assoc_copy) return ERR_NO_MEMORY;
            memcpy(assoc_copy, assoc_desc, length);

            mx_status_t status = usb_device_add_interface_association(parent, device_desc, assoc_copy, length);
            if (status != NO_ERROR) {
                result = status;
            }

            header = next;
        } else if (header->bDescriptorType == USB_DT_INTERFACE) {
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

mx_status_t usb_device_add(mx_device_t* hci_mxdev, usb_hci_protocol_t* hci_protocol,
                           mx_device_t* parent,  uint32_t device_id, uint32_t hub_id,
                           usb_speed_t speed, usb_device_t** out_device) {

    usb_device_t* dev = calloc(1, sizeof(usb_device_t));
    if (!dev)
        return ERR_NO_MEMORY;

    // read device descriptor
    usb_device_descriptor_t* device_desc = &dev->device_desc;
    mx_status_t status = usb_device_get_descriptor(hci_mxdev, device_id, USB_DT_DEVICE, 0, 0,
                                                   device_desc, sizeof(*device_desc));
    if (status != sizeof(*device_desc)) {
        printf("usb_device_get_descriptor failed\n");
        free(dev);
        return status;
    }

    int num_configurations = device_desc->bNumConfigurations;
    usb_configuration_descriptor_t** configs = calloc(num_configurations,
                                                      sizeof(usb_configuration_descriptor_t*));
    if (!configs) {
        status = ERR_NO_MEMORY;
        goto error_exit;
    }

    for (int config = 0; config < num_configurations; config++) {
        // read configuration descriptor header to determine size
        usb_configuration_descriptor_t config_desc_header;
        status = usb_device_get_descriptor(hci_mxdev, device_id, USB_DT_CONFIG, config, 0,
                                           &config_desc_header, sizeof(config_desc_header));
        if (status != sizeof(config_desc_header)) {
            printf("usb_device_get_descriptor failed\n");
            goto error_exit;
        }
        uint16_t config_desc_size = letoh16(config_desc_header.wTotalLength);
        usb_configuration_descriptor_t* config_desc = malloc(config_desc_size);
        if (!config_desc) {
            status = ERR_NO_MEMORY;
            goto error_exit;
        }
        configs[config] = config_desc;

        // read full configuration descriptor
        status = usb_device_get_descriptor(hci_mxdev, device_id, USB_DT_CONFIG, config, 0,
                                           config_desc, config_desc_size);
         if (status != config_desc_size) {
            printf("usb_device_get_descriptor failed\n");
            goto error_exit;
        }
    }

    // set configuration
    status = usb_device_control(hci_mxdev, device_id,
                             USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                             USB_REQ_SET_CONFIGURATION, configs[0]->bConfigurationValue, 0,
                             NULL, 0);
    if (status < 0) {
        printf("set configuration failed\n");
        goto error_exit;
    }
    dev->current_config_index = 0;

    printf("* found USB device (0x%04x:0x%04x, USB %x.%x)\n", device_desc->idVendor,
           device_desc->idProduct, device_desc->bcdUSB >> 8, device_desc->bcdUSB & 0xff);

    list_initialize(&dev->children);
    dev->hci_mxdev = hci_mxdev;
    dev->hci_protocol = hci_protocol;
    dev->device_id = device_id;
    dev->hub_id = hub_id;
    dev->speed = speed;
    dev->config_descs = configs;

    char name[16];
    snprintf(name, sizeof(name), "usb-dev-%03d", device_id);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = dev,
        .ops = &usb_device_proto,
        .proto_id = MX_PROTOCOL_USB,
        // Do not allow binding to root of a composite device.
        // Clients will bind to the child interfaces instead.
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, &dev->mxdev);
    if (status == NO_ERROR) {
        *out_device = dev;
    } else {
        goto error_exit;
    }

    return usb_device_add_interfaces(dev, configs[0]);

error_exit:
    if (configs) {
        for (int i = 0; i < num_configurations; i++) {
            if (configs[i]) free(configs[i]);
        }
        free(configs);
    }
    free(dev);
    return status;
}
