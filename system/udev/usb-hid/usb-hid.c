// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/common/usb.h>
#include <ddk/protocol/input.h>
#include <ddk/protocol/usb-device.h>

#include <hexdump/hexdump.h>
#include <magenta/types.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USB_HID_SUBCLASS_BOOT   0x01
#define USB_HID_PROTOCOL_KBD    0x01

#define USB_HID_OUTPUT_REPORT   0x02

#define to_hid_dev(d) containerof(d, usb_hid_dev_t, dev)

static void usb_interrupt_callback(iotxn_t* txn, void* cookie) {
    usb_hid_dev_t* hid = (usb_hid_dev_t*)cookie;
#ifdef USB_HID_DEBUG
    printf("usb-hid: callback request status %d\n", txn->status);
    hexdump(request->buffer, txn->actual);
#endif
    void* buffer;

    bool requeue = true;
    switch (txn->status) {
    case ERR_CHANNEL_CLOSED:
        usb_hid_process_closed(hid);
        requeue = false;
        break;
    case NO_ERROR:
        txn->ops->mmap(txn, &buffer);
        usb_hid_process_req(hid, buffer, txn->actual);
        break;
    default:
        break;
    }

    if (requeue) {
        iotxn_queue(hid->usbdev, txn);
    }
}

static mx_status_t usb_hid_load_descriptor(usb_hid_descriptor_t* hid_desc, uint8_t desc_type,
                                           usb_hid_dev_t* hid, const uint8_t** buf, size_t* len) {
    int report_desc = -1;
    for (int i = 0; i < hid_desc->bNumDescriptors; i++) {
        if (hid_desc->descriptors[i].bDescriptorType == desc_type) {
            report_desc = i;
            break;
        }
    }
    if (report_desc < 0) {
        return ERR_NOT_FOUND;
    }

    size_t desc_len = hid_desc->descriptors[report_desc].wDescriptorLength;
    uint8_t* desc_buf = malloc(desc_len);
    mx_status_t status = usb_control(hid->usbdev, (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE),
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
    usb_desc_iter_t iter;
    mx_status_t result = usb_desc_iter_init(dev, &iter);
    if (result < 0) return result;

    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
     if (!intf) {
        usb_desc_iter_release(&iter);
        return ERR_NOT_SUPPORTED;
    }

    // One usb-hid device per HID interface
    int i = 0;
    while (intf) {
        if (intf->bInterfaceClass != USB_CLASS_HID) {
            intf = usb_desc_iter_next_interface(&iter, true);
            continue;
        }

        usb_endpoint_descriptor_t* endpt = NULL;
        usb_hid_descriptor_t* hid_desc = NULL;

        // look for interrupt endpoint and HID descriptor
        usb_descriptor_header_t* header = usb_desc_iter_next(&iter);
        while (header && !(endpt && hid_desc)) {
            if (header->bDescriptorType == USB_DT_HID) {
                hid_desc = (usb_hid_descriptor_t *)header;
            } else if (header->bDescriptorType == USB_DT_ENDPOINT) {
                endpt = (usb_endpoint_descriptor_t *)header;
                if (usb_ep_direction(endpt) != USB_ENDPOINT_IN &&
                    usb_ep_type(endpt) != USB_ENDPOINT_INTERRUPT) {
                    endpt = NULL;
                }
            } else if (header->bDescriptorType == USB_DT_INTERFACE) {
                goto next_interface;
            }
            header = usb_desc_iter_next(&iter);
        }

        if (!endpt || !hid_desc) {
            goto next_interface;
        }

        usb_hid_dev_t* hid = NULL;
        mx_status_t status = usb_hid_create_dev(&hid);
        if (hid == NULL) {
            usb_desc_iter_release(&iter);
            return ERR_NO_MEMORY;
        }

        char name[10];
        snprintf(name, sizeof(name), "usb-hid%02d", i++);
        device_init(&hid->dev, drv, name, &usb_hid_proto);

        hid->usbdev = dev;
        hid->drv = drv;
        hid->interface = intf->bInterfaceNumber;

        if (intf->bInterfaceSubClass == USB_HID_SUBCLASS_BOOT) {
            // Use the boot protocol for now
            usb_control(hid->usbdev, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
                    USB_HID_SET_PROTOCOL, 0, i, NULL, 0);
            hid->proto = intf->bInterfaceProtocol;
            if (hid->proto == USB_HID_PROTOCOL_KBD) {
                // Disable numlock on boot
                uint8_t zero = 0;
                usb_control(hid->usbdev, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
                        USB_HID_SET_REPORT, USB_HID_OUTPUT_REPORT << 8, i, &zero, sizeof(zero));
            }
        }

        hid->txn = usb_alloc_iotxn(endpt->bEndpointAddress, usb_ep_max_packet(endpt), 0);
        if (hid->txn == NULL) {
            usb_desc_iter_release(&iter);
            usb_hid_cleanup_dev(hid);
            return ERR_NO_MEMORY;
        }
        hid->txn->complete_cb = usb_interrupt_callback;
        hid->txn->cookie = hid;

        if (usb_hid_load_descriptor(hid_desc, USB_DT_HIDREPORT, hid,
                    &hid->hid_report_desc, &hid->hid_report_desc_len) == NO_ERROR) {
            usb_hid_load_hid_report_desc(hid);
        }

        if (hid->hid_report_desc == NULL) {
            usb_desc_iter_release(&iter);
            usb_hid_cleanup_dev(hid);
            return ERR_NOT_SUPPORTED;
        }

        hid->dev.protocol_id = MX_PROTOCOL_INPUT;
        status = device_add(&hid->dev, dev);
        if (status != NO_ERROR) {
            usb_desc_iter_release(&iter);
            usb_hid_cleanup_dev(hid);
            return status;
        }

        usb_control(hid->usbdev, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
                USB_HID_SET_IDLE, 0, i, NULL, 0);

        hid->txn->length = usb_ep_max_packet(endpt);
        iotxn_queue(hid->usbdev, hid->txn);

next_interface:
        // move on to next interface
        if (header && header->bDescriptorType == USB_DT_INTERFACE) {
            intf = (usb_interface_descriptor_t *)header;
        } else {
            intf = usb_desc_iter_next_interface(&iter, true);
        }
    }
    usb_desc_iter_release(&iter);

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
