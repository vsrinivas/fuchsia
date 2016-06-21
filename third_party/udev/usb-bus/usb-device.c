/*
 * This file is part of the libpayload project.
 *
 * Copyright (C) 2013 secunet Security Networks AG
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/usb-hci.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "usb-private.h"
#include "usb-device.h"

#define NEXT_DESCRIPTOR(header) ((descriptor_header_t*)((void*)header + header->bLength))

#define DR_DESC (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE)

typedef struct usb_device {
    mx_device_t device;
    int address;
    usb_speed speed;

    // device's HCI controller and protocol
    mx_device_t* hcidev;
    usb_hci_protocol_t* hci_protocol;

    // FIXME add code to free these
    usb_device_config_t config;

    mx_device_prop_t props[6];
} usb_device_t;
#define get_usb_device(dev) containerof(dev, usb_device_t, device)

/* Normalize bInterval to log2 of microframes */
static int
usb_decode_interval(usb_speed speed, const endpoint_type type, const unsigned char bInterval) {
#define LOG2(a) ((sizeof(unsigned) << 3) - __builtin_clz(a) - 1)
    switch (speed) {
    case LOW_SPEED:
        switch (type) {
        case USB_ENDPOINT_ISOCHRONOUS:
        case USB_ENDPOINT_INTERRUPT:
            return LOG2(bInterval) + 3;
        default:
            return 0;
        }
    case FULL_SPEED:
        switch (type) {
        case USB_ENDPOINT_ISOCHRONOUS:
            return (bInterval - 1) + 3;
        case USB_ENDPOINT_INTERRUPT:
            return LOG2(bInterval) + 3;
        default:
            return 0;
        }
    case HIGH_SPEED:
        switch (type) {
        case USB_ENDPOINT_ISOCHRONOUS:
        case USB_ENDPOINT_INTERRUPT:
            return bInterval - 1;
        default:
            return LOG2(bInterval);
        }
    case SUPER_SPEED:
        switch (type) {
        case USB_ENDPOINT_ISOCHRONOUS:
        case USB_ENDPOINT_INTERRUPT:
            return bInterval - 1;
        default:
            return 0;
        }
    default:
        return 0;
    }
#undef LOG2
}

static int
count_interfaces(usb_configuration_descriptor_t* desc) {
    int count = 0;
    descriptor_header_t* header = NEXT_DESCRIPTOR(desc);
    descriptor_header_t* end = (descriptor_header_t*)((void*)desc + desc->wTotalLength);
    while (header < end) {
        if (header->bDescriptorType == USB_DT_INTERFACE)
            count++;
        header = NEXT_DESCRIPTOR(header);
    }
    return count;
}

static int
count_alt_interfaces(usb_interface_descriptor_t* desc, descriptor_header_t* end) {
    int count = 0;
    descriptor_header_t* header = NEXT_DESCRIPTOR(desc);
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

mx_status_t usb_init_device(usb_device_t* dev) {
    usb_device_config_t* device_config = &dev->config;
    usb_device_descriptor_t* descriptor = malloc(sizeof(usb_device_descriptor_t));
    if (!descriptor || usb_get_descriptor(&dev->device, DR_DESC, USB_DT_DEVICE, 0, descriptor, sizeof(*descriptor)) != sizeof(*descriptor)) {
        usb_debug("get_descriptor(USB_DT_DEVICE) failed\n");
        free(dev);
        return -1;
    }
    device_config->descriptor = descriptor;

    usb_debug("* found device (0x%04x:0x%04x, USB %x.%x)\n",
              descriptor->idVendor, descriptor->idProduct,
              descriptor->bcdUSB >> 8, descriptor->bcdUSB & 0xff);

    int num_configurations = descriptor->bNumConfigurations;
    if (num_configurations == 0) {
        /* device isn't usable */
        usb_debug("... no usable configuration!\n");
        return -1;
    }

    /* workaround for some USB devices: wait until they're ready, or
	 * they send a NAK when they're not allowed to do. 1ms is enough */
    usleep(1000 * 1);
    device_config->num_configurations = num_configurations;
    device_config->configurations = calloc(1, num_configurations * sizeof(usb_configuration_t));
    if (!device_config->configurations) {
        usb_debug("could not allocate buffer for USB_DT_CONFIG\n");
        return -1;
    }
    for (int i = 0; i < num_configurations; i++) {
        usb_configuration_t* config = &device_config->configurations[i];
        usb_configuration_descriptor_t desc;
        if (usb_get_descriptor(&dev->device, DR_DESC, USB_DT_CONFIG, i, &desc, sizeof(desc)) != sizeof(desc)) {
            usb_debug("first get_descriptor(USB_DT_CONFIG) failed\n");
            return -1;
        }

        int length = desc.wTotalLength;
        usb_configuration_descriptor_t* cd = malloc(length);
        if (!cd) {
            usb_debug("could not allocate usb_configuration_descriptor_t\n");
            return -1;
        }
        if (usb_get_descriptor(&dev->device, DR_DESC, USB_DT_CONFIG, 0, cd, length) != length) {
            usb_debug("get_descriptor(USB_DT_CONFIG) failed\n");
            return -1;
        }
        if (cd->wTotalLength != length) {
            usb_debug("configuration descriptor size changed, aborting\n");
            return -1;
        }
        config->descriptor = cd;

        // we can't use cd->bNumInterfaces since it doesn't account for alternate settings
        config->num_interfaces = count_interfaces(cd);
        usb_interface_t* interfaces = calloc(1, config->num_interfaces * sizeof(usb_interface_t));
        if (!interfaces) {
            usb_debug("could not allocate interface list\n");
            return -1;
        }
        config->interfaces = interfaces;

        usb_endpoint_t* endpoints = NULL;
        int endpoint_index = 0;

        usb_interface_descriptor_t* intf = NULL;
        int intf_index = 0;
        int alt_intf_index = 0;
        usb_interface_t* current_interface = NULL;
        descriptor_header_t* ptr = NEXT_DESCRIPTOR(cd);
        descriptor_header_t* end = (descriptor_header_t*)((void*)cd + cd->wTotalLength);

        while (ptr < end) {
            if (ptr->bDescriptorType == USB_DT_INTERFACE) {
                intf = (usb_interface_descriptor_t*)ptr;
                if (intf->bLength != sizeof(*intf)) {
                    usb_debug("Skipping broken USB_DT_INTERFACE\n");
                    return -1;
                }

                usb_interface_t* interface;
                if (intf->bAlternateSetting == 0) {
                    interface = &interfaces[intf_index++];
                    current_interface = interface;
                    alt_intf_index = 0;
                    int num_alt_interfaces = count_alt_interfaces(intf, end);
                    if (num_alt_interfaces > 0) {
                        interface->alt_interfaces = calloc(1, num_alt_interfaces * sizeof(usb_interface_t));
                        if (!interface->alt_interfaces) {
                            usb_debug("could not allocate alt interface list\n");
                            return -1;
                        }
                    } else {
                        interface->alt_interfaces = NULL;
                    }
                    interface->num_alt_interfaces = num_alt_interfaces;
                } else {
                    if (current_interface == NULL) {
                        usb_debug("alternate interface with no current interface\n");
                        return -1;
                    }
                    if (intf->bInterfaceNumber != current_interface->descriptor->bInterfaceNumber) {
                        usb_debug("alternate interface does not match current primary interface\n");
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
                        usb_debug("could not allocate endpoint list\n");
                        return -1;
                    }
                }
                interface->endpoints = endpoints;
                interface->num_endpoints = intf->bNumEndpoints;
                endpoint_index = 0;
            } else if (ptr->bDescriptorType == USB_DT_ENDPOINT) {
                usb_endpoint_descriptor_t* ed = (usb_endpoint_descriptor_t*)ptr;
                if (ed->bLength != sizeof(*ed)) {
                    usb_debug("Skipping broken USB_DT_ENDPOINT\n");
                    return -1;
                }
                if (endpoint_index >= intf->bNumEndpoints) {
                    usb_debug("more endpoints in this interface than expected\n");
                    return -1;
                }
                if (!intf) {
                    usb_debug("endpoint descriptor with no interface, aborting\n");
                    return -1;
                }
                usb_endpoint_t* ep = &endpoints[endpoint_index++];
                ep->descriptor = ed;
                ep->endpoint = ed->bEndpointAddress;
                ep->toggle = 0;
                ep->maxpacketsize = ed->wMaxPacketSize;
                ep->direction = ed->bEndpointAddress & USB_ENDPOINT_DIR_MASK;
                ep->type = ed->bmAttributes & USB_ENDPOINT_TYPE_MASK;
                ep->interval = usb_decode_interval(dev->speed, ep->type,
                                                   ed->bInterval);
            }

            ptr = NEXT_DESCRIPTOR(ptr);
        }
    }

    if ((dev->hci_protocol->finish_device_config(dev->hcidev, dev->address, device_config)) ||
        usb_set_configuration(&dev->device) < 0) {
        usb_debug("Could not finalize device configuration\n");
        return -1;
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

static mx_status_t usb_control(mx_device_t* device, uint8_t request_type, uint8_t request,
                               uint16_t value, uint16_t index, void* data, uint16_t length) {

    usb_setup_t dr;
    dr.bmRequestType = request_type;
    dr.bRequest = request;
    dr.wValue = value;
    dr.wIndex = index;
    dr.wLength = length;

    usb_device_t* dev = get_usb_device(device);
    return dev->hci_protocol->control(dev->hcidev, dev->address, &dr, length, data);
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

static usb_speed usb_get_speed(mx_device_t* device) {
    usb_device_t* dev = get_usb_device(device);
    return dev->speed;
}

static int usb_get_address(mx_device_t* device) {
    usb_device_t* dev = get_usb_device(device);
    return dev->address;
}

usb_device_protocol_t _device_protocol = {
    .alloc_request = usb_alloc_request,
    .free_request = usb_free_request,
    .control = usb_control,
    .get_config = usb_get_config,
    .queue_request = usb_queue_request,
    .get_speed = usb_get_speed,
    .get_address = usb_get_address,
};

static mx_status_t usb_device_probe(mx_driver_t* drv, mx_device_t* dev) {
    //printf("usb_device_probe\n");
    return ERR_NOT_SUPPORTED;
}

mx_driver_t _driver_usb_device BUILTIN_DRIVER = {
    .name = "usb_device",
    .ops = {
        .probe = usb_device_probe,
    },
};

static mx_status_t usb_device_open(mx_device_t* dev, uint32_t flags) {
    printf("usb_device_open\n");
    return NO_ERROR;
}

static mx_status_t usb_device_close(mx_device_t* dev) {
    printf("usb_device_close\n");
    return NO_ERROR;
}

static void usb_interface_free(usb_interface_t* intf) {
    for (int i = 0; i < intf->num_alt_interfaces; i++) {
        usb_interface_t* alt = &intf->alt_interfaces[i];
        if (alt) {
            usb_interface_free(alt);
        }
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

void usb_device_free(usb_device_t* dev) {
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

mx_protocol_device_t usb_device_proto = {
    .get_protocol = device_base_get_protocol,
    .open = usb_device_open,
    .close = usb_device_close,
    .release = usb_device_release,
};

mx_device_t* usb_create_device(mx_device_t* hcidev, int address, usb_speed speed) {
    usb_device_t* dev = calloc(1, sizeof(usb_device_t));
    if (!dev)
        return NULL;

    device_get_protocol(hcidev, MX_PROTOCOL_USB_HCI, (void**)&dev->hci_protocol);
    dev->hcidev = hcidev;
    dev->speed = speed;
    dev->address = address;

    mx_status_t status = usb_init_device(dev);
    if (status < 0) {
        dev->hci_protocol->destroy_device(hcidev, address);
        free(dev);
        return NULL;
    }

    char name[60];
    usb_device_descriptor_t* descriptor = dev->config.descriptor;
    snprintf(name, sizeof(name), "usb_device[%04X:%04X %d %d %d]",
             descriptor->idVendor, descriptor->idProduct,
             descriptor->bDeviceClass, descriptor->bDeviceSubClass, descriptor->bDeviceProtocol);

    status = device_init(&dev->device, &_driver_usb_device, name, &usb_device_proto);
    if (status < 0) {
        free(dev);
        return NULL;
    }
    dev->device.protocol_id = MX_PROTOCOL_USB_DEVICE;
    dev->device.protocol_ops = &_device_protocol;

    dev->props[0] = (mx_device_prop_t){ BIND_PROTOCOL, 0, MX_PROTOCOL_USB_DEVICE };
    dev->props[1] = (mx_device_prop_t){ BIND_USB_VID, 0, descriptor->idVendor };
    dev->props[2] = (mx_device_prop_t){ BIND_USB_PID, 0, descriptor->idProduct };
    dev->props[3] = (mx_device_prop_t){ BIND_USB_CLASS, 0, descriptor->bDeviceClass };
    dev->props[4] = (mx_device_prop_t){ BIND_USB_SUBCLASS, 0, descriptor->bDeviceSubClass };
    dev->props[5] = (mx_device_prop_t){ BIND_USB_PROTOCOL, 0, descriptor->bDeviceProtocol };
    dev->device.props = dev->props;
    dev->device.prop_count = 6;

    return &dev->device;
}

int usb_set_feature(mx_device_t* device, int endp, int feature, int rtype) {
    usb_device_t* dev = get_usb_device(device);
    usb_setup_t dr;

    dr.bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | rtype;
    dr.bRequest = USB_REQ_SET_FEATURE;
    dr.wValue = feature;
    dr.wIndex = endp;
    dr.wLength = 0;

    return dev->hci_protocol->control(dev->hcidev, dev->address, &dr, 0, 0);
}

int usb_get_status(mx_device_t* device, int intf, int rtype, int len, void* data) {
    usb_device_t* dev = get_usb_device(device);
    usb_setup_t dr;

    dr.bmRequestType = USB_DIR_IN | USB_TYPE_STANDARD | rtype;
    dr.bRequest = USB_REQ_GET_STATUS;
    dr.wValue = 0;
    dr.wIndex = intf;
    dr.wLength = len;

    return dev->hci_protocol->control(dev->hcidev, dev->address, &dr, len, data);
}

int usb_get_descriptor(mx_device_t* device, int rtype, int desc_type, int desc_idx,
                       void* data, size_t len) {
    usb_device_t* dev = get_usb_device(device);
    usb_setup_t dr;

    dr.bmRequestType = rtype;
    dr.bRequest = USB_REQ_GET_DESCRIPTOR;
    dr.wValue = desc_type << 8 | desc_idx;
    dr.wIndex = 0;
    dr.wLength = len;

    return dev->hci_protocol->control(dev->hcidev, dev->address, &dr, len, data);
}

int usb_set_configuration(mx_device_t* device) {
    usb_device_t* dev = get_usb_device(device);
    usb_setup_t dr;

    dr.bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    dr.bRequest = USB_REQ_SET_CONFIGURATION;
    dr.wValue = dev->config.configurations[0].descriptor->bConfigurationValue;
    dr.wIndex = 0;
    dr.wLength = 0;

    return dev->hci_protocol->control(dev->hcidev, dev->address, &dr, 0, 0);
}

int usb_clear_feature(mx_device_t* device, int endp, int feature, int rtype) {
    usb_device_t* dev = get_usb_device(device);
    usb_setup_t dr;

    dr.bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | rtype;
    dr.bRequest = USB_REQ_CLEAR_FEATURE;
    dr.wValue = feature;
    dr.wIndex = endp;
    dr.wLength = 0;

    return dev->hci_protocol->control(dev->hcidev, dev->address, &dr, 0, 0) < 0;
}

int usb_clear_stall(mx_device_t* device, usb_endpoint_t* ep) {
    int ret = usb_clear_feature(device, ep->endpoint, USB_ENDPOINT_HALT,
                                USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT);
    ep->toggle = 0;
    return ret;
}
