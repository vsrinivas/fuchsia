// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/iotxn.h>
#include <ddk/common/hid.h>
#include <ddk/common/usb.h>
#include <magenta/hw/usb-hid.h>

#include <hexdump/hexdump.h>
#include <magenta/types.h>

#include <stdio.h>
#include <stdlib.h>

#define USB_HID_SUBCLASS_BOOT   0x01
#define USB_HID_PROTOCOL_KBD    0x01
#define USB_HID_PROTOCOL_MOUSE  0x02

#define USB_HID_DEBUG 0
#define to_usb_hid(d) containerof(d, usb_hid_device_t, hiddev)

typedef struct usb_hid_device {
    mx_hid_device_t hiddev;
    mx_device_t* usbdev;

    uint8_t interface;
    usb_hid_descriptor_t* hid_desc;
} usb_hid_device_t;

static void usb_interrupt_callback(iotxn_t* txn, void* cookie) {
    usb_hid_device_t* hid = (usb_hid_device_t*)cookie;
    // TODO use iotxn copyfrom instead of mmap
    void* buffer;
    txn->ops->mmap(txn, &buffer);
#if USB_HID_DEBUG
    printf("usb-hid: callback request status %d\n", txn->status);
    hexdump(buffer, txn->actual);
#endif

    bool requeue = true;
    switch (txn->status) {
    case ERR_REMOTE_CLOSED:
        requeue = false;
        break;
    case NO_ERROR:
        hid_io_queue(&hid->hiddev, buffer, txn->actual);
        break;
    default:
        printf("usb-hid: unknown interrupt status %d\n", txn->status);
        break;
    }

    if (requeue) {
        iotxn_queue(hid->usbdev, txn);
    }
}

static mx_status_t usb_hid_get_descriptor(mx_hid_device_t* dev, uint8_t desc_type,
        void** data, size_t* len) {
    usb_hid_device_t* hid = to_usb_hid(dev);
    int desc_idx = -1;
    for (int i = 0; i < hid->hid_desc->bNumDescriptors; i++) {
        if (hid->hid_desc->descriptors[i].bDescriptorType == desc_type) {
            desc_idx = i;
            break;
        }
    }
    if (desc_idx < 0) {
        return ERR_NOT_FOUND;
    }

    size_t desc_len = hid->hid_desc->descriptors[desc_idx].wDescriptorLength;
    uint8_t* desc_buf = malloc(desc_len);
    mx_status_t status = usb_control(hid->usbdev, (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE),
            USB_REQ_GET_DESCRIPTOR, desc_type << 8, hid->interface, desc_buf, desc_len);
    if (status < 0) {
        printf("usb-hid: error reading report descriptor 0x%02x: %d\n", desc_type, status);
        free(desc_buf);
        return status;
    } else {
        *data = desc_buf;
        *len = desc_len;
    }
    return NO_ERROR;
}

static mx_status_t usb_hid_get_report(mx_hid_device_t* dev, uint8_t rpt_type, uint8_t rpt_id,
        void* data, size_t len) {
    usb_hid_device_t* hid = to_usb_hid(dev);
    return usb_control(hid->usbdev, (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
            USB_HID_GET_REPORT, (rpt_type << 8 | rpt_id), hid->interface, data, len);
}

static mx_status_t usb_hid_set_report(mx_hid_device_t* dev, uint8_t rpt_type, uint8_t rpt_id,
        void* data, size_t len) {
    usb_hid_device_t* hid = to_usb_hid(dev);
    return usb_control(hid->usbdev, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
            USB_HID_SET_REPORT, (rpt_type << 8 | rpt_id), hid->interface, data, len);
}

static mx_status_t usb_hid_get_idle(mx_hid_device_t* dev, uint8_t rpt_id, uint8_t* duration) {
    usb_hid_device_t* hid = to_usb_hid(dev);
    return usb_control(hid->usbdev, (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
            USB_HID_GET_IDLE, rpt_id, hid->interface, duration, sizeof(*duration));
}

static mx_status_t usb_hid_set_idle(mx_hid_device_t* dev, uint8_t rpt_id, uint8_t duration) {
    usb_hid_device_t* hid = to_usb_hid(dev);
    return usb_control(hid->usbdev, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
            USB_HID_SET_IDLE, (duration << 8) | rpt_id, hid->interface, NULL, 0);
}

static mx_status_t usb_hid_get_protocol(mx_hid_device_t* dev, uint8_t* protocol) {
    usb_hid_device_t* hid = to_usb_hid(dev);
    return usb_control(hid->usbdev, (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
            USB_HID_GET_PROTOCOL, 0, hid->interface, protocol, sizeof(*protocol));
}

static mx_status_t usb_hid_set_protocol(mx_hid_device_t* dev, uint8_t protocol) {
    usb_hid_device_t* hid = to_usb_hid(dev);
    return usb_control(hid->usbdev, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
            USB_HID_SET_PROTOCOL, protocol, hid->interface, NULL, 0);
}

static hid_bus_ops_t usb_hid_bus_ops = {
    .get_descriptor = usb_hid_get_descriptor,
    .get_report = usb_hid_get_report,
    .set_report = usb_hid_set_report,
    .get_idle = usb_hid_get_idle,
    .set_idle = usb_hid_set_idle,
    .get_protocol = usb_hid_get_protocol,
    .set_protocol = usb_hid_set_protocol,
};

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

        usb_hid_device_t* usbhid = calloc(1, sizeof(usb_hid_device_t));
        if (usbhid == NULL) {
            usb_desc_iter_release(&iter);
            return ERR_NO_MEMORY;
        }

        usbhid->usbdev = dev;
        usbhid->interface = intf->bInterfaceNumber;
        usbhid->hid_desc = hid_desc;

        bool boot_dev = intf->bInterfaceSubClass == USB_HID_SUBCLASS_BOOT;
        uint8_t dev_class = HID_DEV_CLASS_OTHER;
        if (intf->bInterfaceProtocol == USB_HID_PROTOCOL_KBD) {
            dev_class = HID_DEV_CLASS_KBD;
        } else if (intf->bInterfaceProtocol == USB_HID_PROTOCOL_MOUSE) {
            dev_class = HID_DEV_CLASS_POINTER;
        }

        iotxn_t* usbtxn = usb_alloc_iotxn(endpt->bEndpointAddress, usb_ep_max_packet(endpt), 0);
        if (usbtxn == NULL) {
            usb_desc_iter_release(&iter);
            free(usbhid);
            return ERR_NO_MEMORY;
        }
        usbtxn->length = usb_ep_max_packet(endpt);
        usbtxn->complete_cb = usb_interrupt_callback;
        usbtxn->cookie = usbhid;
        iotxn_queue(usbhid->usbdev, usbtxn);

        hid_init_device(&usbhid->hiddev, &usb_hid_bus_ops, usbhid->interface, boot_dev, dev_class);
        mx_status_t status = hid_add_device(drv, &usbhid->hiddev, dev);
        if (status != NO_ERROR) {
            usb_desc_iter_release(&iter);
            hid_release_device(&usbhid->hiddev);
            free(usbhid);
            return status;
        }

next_interface:
        // move on to next interface
        if (header && header->bDescriptorType == USB_DT_INTERFACE) {
            intf = (usb_interface_descriptor_t*)header;
        } else {
            intf = usb_desc_iter_next_interface(&iter, true);
        }
    }
    usb_desc_iter_release(&iter);

    return NO_ERROR;
}

mx_driver_t _driver_usb_hid = {
    .ops = {
        .bind = usb_hid_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_usb_hid, "usb-hid", "magenta", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB),
    BI_MATCH_IF(EQ, BIND_USB_CLASS, USB_CLASS_HID),
    BI_ABORT_IF(NE, BIND_USB_CLASS, 0),
    BI_MATCH_IF(EQ, BIND_USB_IFC_CLASS, USB_CLASS_HID),
MAGENTA_DRIVER_END(_driver_usb_hid)
