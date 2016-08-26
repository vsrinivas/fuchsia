// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/common/usb.h>
#include <ddk/protocol/usb-device.h>
#include <ddk/protocol/usb-hci.h>
#include <magenta/device/usb-device.h>
#include <endian.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct usb_device {
    mx_device_t device;
    int address;
    usb_speed_t speed;

    // device's HCI controller and protocol
    mx_device_t* hcidev;
    usb_hci_protocol_t* hci_protocol;

    usb_device_descriptor_t* device_desc;
    usb_configuration_descriptor_t** config_descs;

    mx_device_prop_t props[9];
} usb_device_t;
#define get_usb_device(dev) containerof(dev, usb_device_t, device)

static mx_status_t usb_init_device(usb_device_t* dev, usb_device_descriptor_t* device_descriptor,
                                   usb_configuration_descriptor_t** config_descriptors) {

    if (!device_descriptor || !config_descriptors) return ERR_INVALID_ARGS;
    dev->device_desc = device_descriptor;
    dev->config_descs = config_descriptors;

    printf("* found device (0x%04x:0x%04x, USB %x.%x)\n",
              device_descriptor->idVendor, device_descriptor->idProduct,
              device_descriptor->bcdUSB >> 8, device_descriptor->bcdUSB & 0xff);

    return NO_ERROR;
}

static mx_status_t usb_configure_hub(mx_device_t* device, usb_speed_t speed,
                                     usb_hub_descriptor_t* descriptor) {
    usb_device_t* dev = get_usb_device(device);
    return dev->hci_protocol->configure_hub(dev->hcidev, dev->address, speed, descriptor);
}

static mx_status_t usb_hub_device_added(mx_device_t* device, int port, usb_speed_t speed) {
    usb_device_t* dev = get_usb_device(device);
    return dev->hci_protocol->hub_device_added(dev->hcidev, dev->address, port, speed);
}

static mx_status_t usb_hub_device_removed(mx_device_t* device, int port) {
    usb_device_t* dev = get_usb_device(device);
    return dev->hci_protocol->hub_device_removed(dev->hcidev, dev->address, port);
}

static usb_device_protocol_t _device_protocol = {
    .configure_hub = usb_configure_hub,
    .hub_device_added = usb_hub_device_added,
    .hub_device_removed = usb_hub_device_removed,
};

static mx_driver_t _driver_usb_device BUILTIN_DRIVER = {
    .name = "usb_device",
};

static void usb_iotxn_queue(mx_device_t* device, iotxn_t* txn) {
    usb_device_t* dev = get_usb_device(device);
    usb_protocol_data_t* usb_data = iotxn_pdata(txn, usb_protocol_data_t);
    usb_data->device_id = dev->address;

    // forward iotxn to HCI device
    iotxn_queue(dev->hcidev, txn);
}

static ssize_t usb_device_ioctl(mx_device_t* device, uint32_t op,
        const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    usb_device_t* dev = get_usb_device(device);

    switch (op) {
    case IOCTL_USB_GET_DEVICE_SPEED: {
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_NOT_ENOUGH_BUFFER;
        *reply = dev->speed;
        return sizeof(*reply);
    }
    case IOCTL_USB_GET_DEVICE_DESC: {
        usb_device_descriptor_t* descriptor = dev->device_desc;
        if (out_len < sizeof(*descriptor)) return ERR_NOT_ENOUGH_BUFFER;
        memcpy(out_buf, descriptor, sizeof(*descriptor));
        return sizeof(*descriptor);
    }
    case IOCTL_USB_GET_CONFIG_DESC_SIZE: {
        usb_configuration_descriptor_t* descriptor = dev->config_descs[0];
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_NOT_ENOUGH_BUFFER;
        *reply = le16toh(descriptor->wTotalLength);
        return sizeof(*reply);
    }
    case IOCTL_USB_GET_CONFIG_DESC: {
        usb_configuration_descriptor_t* descriptor = dev->config_descs[0];
        size_t desc_length = le16toh(descriptor->wTotalLength);
        if (out_len < desc_length) return ERR_NOT_ENOUGH_BUFFER;
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
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static mx_status_t usb_device_release(mx_device_t* device) {
    usb_device_t* dev = get_usb_device(device);

    printf("usb_device_release\n");
    if (dev->device_desc && dev->config_descs) {
        for (int i = 0; i < dev->device_desc->bNumConfigurations; i++) {
            free(dev->config_descs[i]);
        }
    }
    free(dev->device_desc);
    free(dev->config_descs);

    return NO_ERROR;
}

static mx_protocol_device_t usb_device_proto = {
    .iotxn_queue = usb_iotxn_queue,
    .ioctl = usb_device_ioctl,
    .release = usb_device_release,
};

mx_status_t usb_add_device(mx_device_t* hcidev, int address, usb_speed_t speed,
                           usb_device_descriptor_t* device_descriptor,
                           usb_configuration_descriptor_t** config_descriptors,
                           mx_device_t** out_device) {
    *out_device = NULL;
    usb_device_t* dev = calloc(1, sizeof(usb_device_t));
    if (!dev)
        return ERR_NO_MEMORY;

    device_get_protocol(hcidev, MX_PROTOCOL_USB_HCI, (void**)&dev->hci_protocol);
    dev->hcidev = hcidev;
    dev->speed = speed;
    dev->address = address;

    mx_status_t status = usb_init_device(dev, device_descriptor, config_descriptors);
    if (status < 0) {
        free(dev);
        return ERR_NO_MEMORY;
    }

    usb_device_descriptor_t* descriptor = dev->device_desc;
    char name[16];
    snprintf(name, sizeof(name), "usb-dev-%03d", address);

    device_init(&dev->device, &_driver_usb_device, name, &usb_device_proto);
    dev->device.protocol_id = MX_PROTOCOL_USB_DEVICE;
    dev->device.protocol_ops = &_device_protocol;

    // Find first interface descriptor
    usb_configuration_descriptor_t* config_desc = dev->config_descs[0];
    usb_interface_descriptor_t* ifcdesc = (usb_interface_descriptor_t *)((uint8_t *)config_desc + config_desc->bLength);
    if (ifcdesc->bDescriptorType != USB_DT_INTERFACE) ifcdesc = NULL;

    dev->props[0] = (mx_device_prop_t){ BIND_PROTOCOL, 0, MX_PROTOCOL_USB_DEVICE };
    dev->props[1] = (mx_device_prop_t){ BIND_USB_VID, 0, descriptor->idVendor };
    dev->props[2] = (mx_device_prop_t){ BIND_USB_PID, 0, descriptor->idProduct };
    dev->props[3] = (mx_device_prop_t){ BIND_USB_CLASS, 0, descriptor->bDeviceClass };
    dev->props[4] = (mx_device_prop_t){ BIND_USB_SUBCLASS, 0, descriptor->bDeviceSubClass };
    dev->props[5] = (mx_device_prop_t){ BIND_USB_PROTOCOL, 0, descriptor->bDeviceProtocol };
    // TODO: either we should publish device-per-interface
    // or we need to come up with a better way to represent
    // the various interface properties
    dev->props[6] = (mx_device_prop_t){ BIND_USB_IFC_CLASS, 0, ifcdesc ? ifcdesc->bInterfaceClass : 0 };
    dev->props[7] = (mx_device_prop_t){ BIND_USB_IFC_SUBCLASS, 0, ifcdesc ? ifcdesc->bInterfaceSubClass : 0 };
    dev->props[8] = (mx_device_prop_t){ BIND_USB_IFC_PROTOCOL, 0, ifcdesc ? ifcdesc->bInterfaceProtocol : 0 };
    dev->device.props = dev->props;
    dev->device.prop_count = countof(dev->props);

    device_add(&dev->device, hcidev);
    *out_device = &dev->device;
    return NO_ERROR;
}
