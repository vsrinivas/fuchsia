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

#include "device.h"

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/input.h>
#include <ddk/protocol/usb-device.h>

#include <ddk/hexdump.h>
#include <magenta/types.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USB_HID_SUBCLASS_BOOT   0x01
#define USB_HID_PROTOCOL_KBD    0x01

#define USB_HID_OUTPUT_REPORT   0x02

#define to_hid_dev(d) containerof(d, usb_hid_dev_t, dev)

static void usb_interrupt_callback(usb_request_t* request) {
    usb_hid_dev_t* hid = (usb_hid_dev_t*)request->client_data;
#ifdef USB_HID_DEBUG
    printf("usb-hid: callback request status %d\n", request->status);
    hexdump(request->buffer, request->transfer_length);
#endif

    bool requeue = true;
    switch (request->status) {
    case ERR_CHANNEL_CLOSED:
        usb_hid_process_closed(hid);
        requeue = false;
        break;
    case NO_ERROR:
        usb_hid_process_req(hid, request->buffer, request->transfer_length);
        break;
    default:
        break;
    }

    if (requeue) {
        request->transfer_length = request->buffer_length;
        hid->usb->queue_request(hid->usbdev, request);
    }
}

static mx_status_t usb_hid_load_descriptor(usb_interface_t* intf, uint8_t desc_type,
                                           usb_hid_dev_t* hid, const uint8_t** buf, size_t* len) {
    int report_desc = -1;
    for (int i = 0; i < hid->hid_desc->bNumDescriptors; i++) {
        if (hid->hid_desc->descriptors[i].bDescriptorType == desc_type) {
            report_desc = i;
            break;
        }
    }
    if (report_desc < 0) {
        return ERR_NOT_FOUND;
    }

    size_t desc_len = hid->hid_desc->descriptors[report_desc].wDescriptorLength;
    uint8_t* desc_buf = malloc(desc_len);
    mx_status_t status = hid->usb->control(hid->usbdev, (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE),
            USB_REQ_GET_DESCRIPTOR, desc_type << 8, hid->interface, desc_buf, desc_len);
    if (status < 0) {
        printf("usb_hid error reading report desc 0x%02x: %d\n", desc_type, status);
        free(desc_buf);
        return status;
    } else {
        *buf = desc_buf;
        *len = desc_len;
    }
    return NO_ERROR;
}

static mx_status_t usb_hid_bind(mx_driver_t* drv, mx_device_t* dev) {
    usb_device_protocol_t* usb;
    if (device_get_protocol(dev, MX_PROTOCOL_USB_DEVICE, (void**)&usb) != NO_ERROR) {
        return ERR_NOT_SUPPORTED;
    }

    usb_device_config_t* devcfg;
    if (usb->get_config(dev, &devcfg) != NO_ERROR) {
        return ERR_NOT_SUPPORTED;
    }
    if (devcfg->num_configurations < 1) {
        return ERR_NOT_SUPPORTED;
    }

    if (devcfg->num_configurations > 1) {
        printf("multiple USB configurations not supported; using first config\n");
    }

    usb_configuration_t* cfg = &devcfg->configurations[0];
    if (cfg->num_interfaces < 1) {
        return ERR_NOT_SUPPORTED;
    }

    // One usb-hid device per HID interface
    for (int i = 0; i < cfg->num_interfaces; i++) {
        usb_interface_t* intf = &cfg->interfaces[i];
        usb_interface_descriptor_t* desc = intf->descriptor;
        assert(intf->num_endpoints == desc->bNumEndpoints);

        if (desc->bInterfaceClass != USB_CLASS_HID) continue;
        if (desc->bNumEndpoints < 1) continue;
        if (list_is_empty(&intf->class_descriptors)) continue;

        usb_endpoint_t* endpt = NULL;
        for (int e = 0; e < intf->num_endpoints; e++) {
            if (intf->endpoints[e].direction == USB_ENDPOINT_IN &&
                intf->endpoints[e].type == USB_ENDPOINT_INTERRUPT) {
                endpt = &intf->endpoints[e];
            }
        }
        if (endpt == NULL) {
            continue;
        }

        usb_hid_dev_t* hid = NULL;
        mx_status_t status = usb_hid_create_dev(&hid);
        if (hid == NULL) {
            return ERR_NO_MEMORY;
        }

        char name[10];
        snprintf(name, sizeof(name), "usb-hid%02d", i);
        status = device_init(&hid->dev, drv, name, &usb_hid_proto);
        if (status != NO_ERROR) {
            usb_hid_cleanup_dev(hid);
            return status;
        }

        hid->usbdev = dev;
        hid->drv = drv;
        hid->usb = usb;
        hid->endpt = endpt;
        hid->interface = desc->bInterfaceNumber;

        if (desc->bInterfaceSubClass == USB_HID_SUBCLASS_BOOT) {
            // Use the boot protocol for now
            hid->usb->control(hid->usbdev, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
                    USB_HID_SET_PROTOCOL, 0, i, NULL, 0);
            hid->proto = desc->bInterfaceProtocol;
            if (hid->proto == USB_HID_PROTOCOL_KBD) {
                // Disable numlock on boot
                uint8_t zero = 0;
                hid->usb->control(hid->usbdev, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
                        USB_HID_SET_REPORT, USB_HID_OUTPUT_REPORT << 8, i, &zero, sizeof(zero));
            }
        }

        hid->req = hid->usb->alloc_request(hid->usbdev, hid->endpt, hid->endpt->maxpacketsize);
        if (hid->req == NULL) {
            usb_hid_cleanup_dev(hid);
            return ERR_NO_MEMORY;
        }
        hid->req->complete_cb = usb_interrupt_callback;
        hid->req->client_data = hid;

        usb_class_descriptor_t* class_desc = NULL;
        list_for_every_entry(&intf->class_descriptors, class_desc,
                usb_class_descriptor_t, node) {
            if (class_desc->header->bDescriptorType == USB_DT_HID) {
                hid->hid_desc = (usb_hid_descriptor_t*)class_desc->header;
                if (usb_hid_load_descriptor(intf, USB_DT_HIDREPORT, hid,
                            &hid->hid_report_desc, &hid->hid_report_desc_len) == NO_ERROR) {
                    usb_hid_load_hid_report_desc(hid);
                    break;
                }
            }
        }
        if (hid->hid_desc == NULL) {
            usb_hid_cleanup_dev(hid);
            return ERR_NOT_SUPPORTED;
        }

        hid->dev.protocol_id = MX_PROTOCOL_INPUT;
        status = device_add(&hid->dev, dev);
        if (status != NO_ERROR) {
            usb_hid_cleanup_dev(hid);
            return status;
        }

        hid->usb->control(hid->usbdev, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
                USB_HID_SET_IDLE, 0, i, NULL, 0);

        hid->req->transfer_length = hid->req->buffer_length;
        hid->usb->queue_request(hid->usbdev, hid->req);
    }

    return NO_ERROR;
}

static mx_bind_inst_t binding[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB_DEVICE),
    BI_MATCH_IF(EQ, BIND_USB_CLASS, USB_CLASS_HID),
    BI_ABORT_IF(NE, BIND_USB_CLASS, 0),
    BI_MATCH_IF(EQ, BIND_USB_IFC_CLASS, USB_CLASS_HID),
};

mx_driver_t _driver_usb_hid BUILTIN_DRIVER = {
    .name = "usb-hid",
    .ops = {
        .bind = usb_hid_bind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};
