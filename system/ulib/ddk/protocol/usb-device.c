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
#include <endian.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define NEXT_DESCRIPTOR(header) ((usb_descriptor_header_t*)((void*)header + header->bLength))

typedef struct usb_device {
    mx_device_t device;
    int address;
    usb_speed_t speed;

    // device's HCI controller and protocol
    mx_device_t* hcidev;
    usb_hci_protocol_t* hci_protocol;

    // FIXME add code to free these
    usb_device_config_t config;

    mx_device_prop_t props[9];
} usb_device_t;
#define get_usb_device(dev) containerof(dev, usb_device_t, device)

static int count_interfaces(usb_configuration_descriptor_t* desc) {
    int count = 0;
    usb_descriptor_header_t* header = NEXT_DESCRIPTOR(desc);
    usb_descriptor_header_t* end = (usb_descriptor_header_t*)((void*)desc + desc->wTotalLength);
    while (header < end) {
        if (header->bDescriptorType == USB_DT_INTERFACE)
            count++;
        header = NEXT_DESCRIPTOR(header);
    }
    return count;
}

static int count_alt_interfaces(usb_interface_descriptor_t* desc, usb_descriptor_header_t* end) {
    int count = 0;
    usb_descriptor_header_t* header = NEXT_DESCRIPTOR(desc);
    while (header < end) {
        if (header->bDescriptorType == USB_DT_INTERFACE) {
            usb_interface_descriptor_t* test = (usb_interface_descriptor_t*)header;
            if (test->bInterfaceNumber == desc->bInterfaceNumber && test->bAlternateSetting != 0) {
                count++;
            } else {
                break;
            }
        }
        header = NEXT_DESCRIPTOR(header);
    }
    return count;
}

static mx_status_t usb_init_device(usb_device_t* dev, usb_device_descriptor_t* device_descriptor,
                                   usb_configuration_descriptor_t** config_descriptors) {
    usb_device_config_t* device_config = &dev->config;

    if (!device_descriptor || !config_descriptors) return ERR_INVALID_ARGS;
    device_config->descriptor = device_descriptor;

    printf("* found device (0x%04x:0x%04x, USB %x.%x)\n",
              device_descriptor->idVendor, device_descriptor->idProduct,
              device_descriptor->bcdUSB >> 8, device_descriptor->bcdUSB & 0xff);

    int num_configurations = device_descriptor->bNumConfigurations;
    if (num_configurations == 0) {
        /* device isn't usable */
        return -1;
    }

    device_config->num_configurations = num_configurations;
    device_config->configurations = calloc(1, num_configurations * sizeof(usb_configuration_t));
    if (!device_config->configurations) {
        printf("could not allocate buffer for USB_DT_CONFIG\n");
        return -1;
    }
    for (int i = 0; i < num_configurations; i++) {
        usb_configuration_t* config = &device_config->configurations[i];
        usb_configuration_descriptor_t* cd = config_descriptors[i];
        config->descriptor = cd;

        // we can't use cd->bNumInterfaces since it doesn't account for alternate settings
        config->num_interfaces = count_interfaces(cd);
        usb_interface_t* interfaces = calloc(1, config->num_interfaces * sizeof(usb_interface_t));
        if (!interfaces) {
            printf("could not allocate interface list\n");
            return -1;
        }
        config->interfaces = interfaces;

        usb_endpoint_t* endpoints = NULL;
        int endpoint_index = 0;

        usb_interface_descriptor_t* intf = NULL;
        int intf_index = 0;
        int alt_intf_index = 0;
        usb_interface_t* current_interface = NULL;
        usb_descriptor_header_t* ptr = NEXT_DESCRIPTOR(cd);
        usb_descriptor_header_t* end = (usb_descriptor_header_t*)((void*)cd + cd->wTotalLength);

        while (ptr < end) {
            if (ptr->bDescriptorType == USB_DT_INTERFACE) {
                intf = (usb_interface_descriptor_t*)ptr;
                if (intf->bLength != sizeof(*intf)) {
                    printf("Skipping broken USB_DT_INTERFACE\n");
                    return -1;
                }

                usb_interface_t* interface;
                if (intf->bAlternateSetting == 0) {
                    interface = &interfaces[intf_index++];
                    current_interface = interface;
                    list_initialize(&interface->class_descriptors);
                    alt_intf_index = 0;
                    int num_alt_interfaces = count_alt_interfaces(intf, end);
                    if (num_alt_interfaces > 0) {
                        interface->alt_interfaces = calloc(1, num_alt_interfaces * sizeof(usb_interface_t));
                        if (!interface->alt_interfaces) {
                            printf("could not allocate alt interface list\n");
                            return -1;
                        }
                    } else {
                        interface->alt_interfaces = NULL;
                    }
                    interface->num_alt_interfaces = num_alt_interfaces;
                } else {
                    if (current_interface == NULL) {
                        printf("alternate interface with no current interface\n");
                        return -1;
                    }
                    if (intf->bInterfaceNumber != current_interface->descriptor->bInterfaceNumber) {
                        printf("alternate interface does not match current primary interface\n");
                        return -1;
                    }
                    interface = &current_interface->alt_interfaces[alt_intf_index++];
                }

                interface->descriptor = intf;
                // now create endpoint list
                if (intf->bNumEndpoints == 0) {
                    endpoints = NULL;
                } else {
                    endpoints = calloc(1, intf->bNumEndpoints * sizeof(usb_endpoint_t));
                    if (!endpoints) {
                        printf("could not allocate endpoint list\n");
                        return -1;
                    }
                }
                interface->endpoints = endpoints;
                interface->num_endpoints = intf->bNumEndpoints;
                endpoint_index = 0;
            } else if (ptr->bDescriptorType == USB_DT_ENDPOINT) {
                usb_endpoint_descriptor_t* ed = (usb_endpoint_descriptor_t*)ptr;
                if (ed->bLength != sizeof(*ed)) {
                    printf("Skipping broken USB_DT_ENDPOINT\n");
                    return -1;
                }
                if (endpoint_index >= intf->bNumEndpoints) {
                    printf("more endpoints in this interface than expected\n");
                    return -1;
                }
                if (!intf) {
                    printf("endpoint descriptor with no interface, aborting\n");
                    return -1;
                }
                usb_endpoint_t* ep = &endpoints[endpoint_index++];
                ep->descriptor = ed;
                ep->maxpacketsize = ed->wMaxPacketSize;
                ep->direction = ed->bEndpointAddress & USB_ENDPOINT_DIR_MASK;
                ep->type = ed->bmAttributes & USB_ENDPOINT_TYPE_MASK;
            } else {
                if (intf) {
                    usb_class_descriptor_t* desc = calloc(1, sizeof(usb_class_descriptor_t));
                    desc->header = ptr;
                    list_add_tail(&current_interface->class_descriptors, &desc->node);
                }
            }

            ptr = NEXT_DESCRIPTOR(ptr);
        }
    }

    return NO_ERROR;
}

static usb_request_t* usb_alloc_request(mx_device_t* device, usb_endpoint_t* ep, uint16_t length) {
    usb_device_t* dev = get_usb_device(device);
    usb_request_t* request = dev->hci_protocol->alloc_request(dev->hcidev, length);
    if (request) {
        request->endpoint = ep;
    }
    return request;
}

static void usb_free_request(mx_device_t* device, usb_request_t* request) {
    usb_device_t* dev = get_usb_device(device);
    dev->hci_protocol->free_request(dev->hcidev, request);
}

static mx_status_t usb_get_config(mx_device_t* device, usb_device_config_t** config) {
    usb_device_t* dev = get_usb_device(device);
    *config = &dev->config;
    return NO_ERROR;
}

static mx_status_t usb_queue_request(mx_device_t* device, usb_request_t* request) {
    usb_device_t* dev = get_usb_device(device);
    return dev->hci_protocol->queue_request(dev->hcidev, dev->address, request);
}

static usb_speed_t usb_get_speed(mx_device_t* device) {
    usb_device_t* dev = get_usb_device(device);
    return dev->speed;
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
    .alloc_request = usb_alloc_request,
    .free_request = usb_free_request,
    .get_config = usb_get_config,
    .queue_request = usb_queue_request,
    .get_speed = usb_get_speed,
    .configure_hub = usb_configure_hub,
    .hub_device_added = usb_hub_device_added,
    .hub_device_removed = usb_hub_device_removed,
};

static mx_driver_t _driver_usb_device BUILTIN_DRIVER = {
    .name = "usb_device",
};

static void usb_interface_free(usb_interface_t* intf) {
    for (int i = 0; i < intf->num_alt_interfaces; i++) {
        usb_interface_t* alt = &intf->alt_interfaces[i];
        if (alt) {
            usb_interface_free(alt);
        }
    }
    usb_class_descriptor_t* cldesc = NULL;
    usb_class_descriptor_t* tmpdesc = NULL;
    list_for_every_entry_safe(&intf->class_descriptors, cldesc, tmpdesc,
            usb_class_descriptor_t, node) {
        free(cldesc);
    }
    free(intf->alt_interfaces);
    free(intf->endpoints);
}

static void usb_configuration_free(usb_configuration_t* config) {
    for (int i = 0; i < config->num_interfaces; i++) {
        usb_interface_t* intf = &config->interfaces[i];
        if (intf) {
            usb_interface_free(intf);
        }
    }
    free(config->interfaces);
    free(config->descriptor);
}

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
        usb_device_descriptor_t* descriptor = dev->config.descriptor;
        if (out_len < sizeof(*descriptor)) return ERR_NOT_ENOUGH_BUFFER;
        memcpy(out_buf, descriptor, sizeof(*descriptor));
        return sizeof(*descriptor);
    }
    case IOCTL_USB_GET_CONFIG_DESC_SIZE: {
        usb_configuration_descriptor_t* descriptor = dev->config.configurations[0].descriptor;
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_NOT_ENOUGH_BUFFER;
        *reply = le16toh(descriptor->wTotalLength);
        return sizeof(*reply);
    }
    case IOCTL_USB_GET_CONFIG_DESC: {
        usb_configuration_descriptor_t* descriptor = dev->config.configurations[0].descriptor;
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
    free(dev->config.descriptor);

    for (int i = 0; i < dev->config.num_configurations; i++) {
        usb_configuration_t* config = &dev->config.configurations[i];
        if (config) {
            usb_configuration_free(config);
        }
    }
    free(dev->config.configurations);

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

    usb_device_descriptor_t* descriptor = dev->config.descriptor;
    char name[16];
    snprintf(name, sizeof(name), "usb-dev-%03d", address);

    device_init(&dev->device, &_driver_usb_device, name, &usb_device_proto);
    dev->device.protocol_id = MX_PROTOCOL_USB_DEVICE;
    dev->device.protocol_ops = &_device_protocol;

    usb_interface_descriptor_t* ifcdesc = dev->config.configurations[0].interfaces[0].descriptor;
    dev->props[0] = (mx_device_prop_t){ BIND_PROTOCOL, 0, MX_PROTOCOL_USB_DEVICE };
    dev->props[1] = (mx_device_prop_t){ BIND_USB_VID, 0, descriptor->idVendor };
    dev->props[2] = (mx_device_prop_t){ BIND_USB_PID, 0, descriptor->idProduct };
    dev->props[3] = (mx_device_prop_t){ BIND_USB_CLASS, 0, descriptor->bDeviceClass };
    dev->props[4] = (mx_device_prop_t){ BIND_USB_SUBCLASS, 0, descriptor->bDeviceSubClass };
    dev->props[5] = (mx_device_prop_t){ BIND_USB_PROTOCOL, 0, descriptor->bDeviceProtocol };
    // TODO: either we should publish device-per-interface
    // or we need to come up with a better way to represent
    // the various interface properties
    dev->props[6] = (mx_device_prop_t){ BIND_USB_IFC_CLASS, 0, ifcdesc->bInterfaceClass };
    dev->props[7] = (mx_device_prop_t){ BIND_USB_IFC_SUBCLASS, 0, ifcdesc->bInterfaceSubClass };
    dev->props[8] = (mx_device_prop_t){ BIND_USB_IFC_PROTOCOL, 0, ifcdesc->bInterfaceProtocol };
    dev->device.props = dev->props;
    dev->device.prop_count = countof(dev->props);

    device_add(&dev->device, hcidev);
    *out_device = &dev->device;
    return NO_ERROR;
}
