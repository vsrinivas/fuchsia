// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/iotxn.h>
#include <ddk/protocol/hidbus.h>
#include <driver/usb.h>
#include <magenta/hw/usb-hid.h>

#include <magenta/types.h>
#include <pretty/hexdump.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#define USB_HID_SUBCLASS_BOOT   0x01
#define USB_HID_PROTOCOL_KBD    0x01
#define USB_HID_PROTOCOL_MOUSE  0x02

#define USB_HID_DEBUG 0
#define to_usb_hid(d) containerof(d, usb_hid_device_t, hiddev)

typedef struct usb_hid_device {
    mx_device_t* mxdev;
    mx_device_t* usbdev;
    usb_protocol_t usb;

    hid_info_t info;
    iotxn_t* txn;
    bool txn_queued;

    mtx_t lock;
    hidbus_ifc_t* ifc;
    void* cookie;

    uint8_t interface;
    usb_hid_descriptor_t* hid_desc;
} usb_hid_device_t;

static void usb_interrupt_callback(iotxn_t* txn, void* cookie) {
    usb_hid_device_t* hid = (usb_hid_device_t*)cookie;
    // TODO use iotxn copyfrom instead of mmap
    void* buffer;
    iotxn_mmap(txn, &buffer);
#if USB_HID_DEBUG
    printf("usb-hid: callback request status %d\n", txn->status);
    hexdump(buffer, txn->actual);
#endif

    bool requeue = true;
    switch (txn->status) {
    case MX_ERR_IO_NOT_PRESENT:
        requeue = false;
        break;
    case MX_OK:
        mtx_lock(&hid->lock);
        if (hid->ifc) {
            hid->ifc->io_queue(hid->cookie, buffer, txn->actual);
        }
        mtx_unlock(&hid->lock);
        break;
    default:
        printf("usb-hid: unknown interrupt status %d; not requeuing iotxn\n", txn->status);
        requeue = false;
        break;
    }

    if (requeue) {
        iotxn_queue(hid->usbdev, txn);
    } else {
        hid->txn_queued = false;
    }
}

static mx_status_t usb_hid_query(void* ctx, uint32_t options, hid_info_t* info) {
    if (!info) {
        return MX_ERR_INVALID_ARGS;
    }
    usb_hid_device_t* hid = ctx;
    info->dev_num = hid->info.dev_num;
    info->dev_class = hid->info.dev_class;
    info->boot_device = hid->info.boot_device;
    return MX_OK;
}

static mx_status_t usb_hid_start(void* ctx, hidbus_ifc_t* ifc, void* cookie) {
    usb_hid_device_t* hid = ctx;
    mtx_lock(&hid->lock);
    if (hid->ifc) {
        mtx_unlock(&hid->lock);
        return MX_ERR_ALREADY_BOUND;
    }
    hid->ifc = ifc;
    hid->cookie = cookie;
    if (!hid->txn_queued) {
        hid->txn_queued = true;
        iotxn_queue(hid->usbdev, hid->txn);
    }
    mtx_unlock(&hid->lock);
    return MX_OK;
}

static void usb_hid_stop(void* ctx) {
    // TODO(tkilbourn) set flag to stop requeueing the interrupt request when we start using
    // this callback
    usb_hid_device_t* hid = ctx;
    mtx_lock(&hid->lock);
    hid->ifc = NULL;
    hid->cookie = NULL;
    mtx_unlock(&hid->lock);
}

static mx_status_t usb_hid_get_descriptor(void* ctx, uint8_t desc_type,
                                          void** data, size_t* len) {
    usb_hid_device_t* hid = ctx;
    int desc_idx = -1;
    for (int i = 0; i < hid->hid_desc->bNumDescriptors; i++) {
        if (hid->hid_desc->descriptors[i].bDescriptorType == desc_type) {
            desc_idx = i;
            break;
        }
    }
    if (desc_idx < 0) {
        return MX_ERR_NOT_FOUND;
    }

    size_t desc_len = hid->hid_desc->descriptors[desc_idx].wDescriptorLength;
    uint8_t* desc_buf = malloc(desc_len);
    mx_status_t status = usb_control(&hid->usb,
                                     (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE),
                                     USB_REQ_GET_DESCRIPTOR, desc_type << 8, hid->interface,
                                     desc_buf, desc_len, MX_TIME_INFINITE);
    if (status < 0) {
        printf("usb-hid: error reading report descriptor 0x%02x: %d\n", desc_type, status);
        free(desc_buf);
        return status;
    } else {
        *data = desc_buf;
        *len = desc_len;
    }
    return MX_OK;
}

static mx_status_t usb_hid_get_report(void* ctx, uint8_t rpt_type, uint8_t rpt_id,
                                      void* data, size_t len) {
    usb_hid_device_t* hid = ctx;
    return usb_control(&hid->usb, (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
            USB_HID_GET_REPORT, (rpt_type << 8 | rpt_id), hid->interface, data, len,
            MX_TIME_INFINITE);
}

static mx_status_t usb_hid_set_report(void* ctx, uint8_t rpt_type, uint8_t rpt_id,
                                      void* data, size_t len) {
    usb_hid_device_t* hid = ctx;
    return usb_control(&hid->usb, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
            USB_HID_SET_REPORT, (rpt_type << 8 | rpt_id), hid->interface, data, len,
            MX_TIME_INFINITE);
}

static mx_status_t usb_hid_get_idle(void* ctx, uint8_t rpt_id, uint8_t* duration) {
    usb_hid_device_t* hid = ctx;
    return usb_control(&hid->usb, (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
            USB_HID_GET_IDLE, rpt_id, hid->interface, duration, sizeof(*duration),
            MX_TIME_INFINITE);
}

static mx_status_t usb_hid_set_idle(void* ctx, uint8_t rpt_id, uint8_t duration) {
    mx_status_t status;
    usb_hid_device_t* hid = ctx;
    status = usb_control(&hid->usb, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
            USB_HID_SET_IDLE, (duration << 8) | rpt_id, hid->interface, NULL, 0,
            MX_TIME_INFINITE);
    if (status == MX_ERR_IO_REFUSED) {
        // The SET_IDLE command is optional, so this may stall.
        // If that occurs, reset the endpoint and ignore the error
        status = usb_reset_endpoint(&hid->usb, 0);
    }
    return status;
}

static mx_status_t usb_hid_get_protocol(void* ctx, uint8_t* protocol) {
    usb_hid_device_t* hid = ctx;
    return usb_control(&hid->usb, (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
            USB_HID_GET_PROTOCOL, 0, hid->interface, protocol, sizeof(*protocol),
            MX_TIME_INFINITE);
}

static mx_status_t usb_hid_set_protocol(void* ctx, uint8_t protocol) {
    usb_hid_device_t* hid = ctx;
    return usb_control(&hid->usb, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
            USB_HID_SET_PROTOCOL, protocol, hid->interface, NULL, 0,MX_TIME_INFINITE);
}

static hidbus_protocol_ops_t usb_hid_bus_ops = {
    .query = usb_hid_query,
    .start = usb_hid_start,
    .stop = usb_hid_stop,
    .get_descriptor = usb_hid_get_descriptor,
    .get_report = usb_hid_get_report,
    .set_report = usb_hid_set_report,
    .get_idle = usb_hid_get_idle,
    .set_idle = usb_hid_set_idle,
    .get_protocol = usb_hid_get_protocol,
    .set_protocol = usb_hid_set_protocol,
};

static void usb_hid_unbind(void* ctx) {
    usb_hid_device_t* hid = ctx;
    device_remove(hid->mxdev);
}

static void usb_hid_release(void* ctx) {
    usb_hid_device_t* hid = ctx;
    iotxn_release(hid->txn);
    free(hid);
}

static mx_protocol_device_t usb_hid_dev_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = usb_hid_unbind,
    .release = usb_hid_release,
};

static mx_status_t usb_hid_bind(void* ctx, mx_device_t* dev, void** cookie) {
    usb_protocol_t usb;

    mx_status_t status = device_get_protocol(dev, MX_PROTOCOL_USB, &usb);
    if (status != MX_OK) {
        return status;
    }

    usb_desc_iter_t iter;
    mx_status_t result = usb_desc_iter_init(&usb, &iter);
    if (result < 0) return result;

    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
     if (!intf) {
        usb_desc_iter_release(&iter);
        return MX_ERR_NOT_SUPPORTED;
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
            return MX_ERR_NO_MEMORY;
        }

        usbhid->usbdev = dev;
        memcpy(&usbhid->usb, &usb, sizeof(usbhid->usb));
        usbhid->interface = usbhid->info.dev_num = intf->bInterfaceNumber;
        usbhid->hid_desc = hid_desc;

        usbhid->info.boot_device = intf->bInterfaceSubClass == USB_HID_SUBCLASS_BOOT;
        usbhid->info.dev_class = HID_DEV_CLASS_OTHER;
        if (intf->bInterfaceProtocol == USB_HID_PROTOCOL_KBD) {
            usbhid->info.dev_class = HID_DEV_CLASS_KBD;
        } else if (intf->bInterfaceProtocol == USB_HID_PROTOCOL_MOUSE) {
            usbhid->info.dev_class = HID_DEV_CLASS_POINTER;
        }

        usbhid->txn = usb_alloc_iotxn(endpt->bEndpointAddress, usb_ep_max_packet(endpt));
        if (usbhid->txn == NULL) {
            usb_desc_iter_release(&iter);
            free(usbhid);
            return MX_ERR_NO_MEMORY;
        }
        usbhid->txn->length = usb_ep_max_packet(endpt);
        usbhid->txn->complete_cb = usb_interrupt_callback;
        usbhid->txn->cookie = usbhid;

        device_add_args_t args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = "usb-hid",
            .ctx = usbhid,
            .ops = &usb_hid_dev_ops,
            .proto_id = MX_PROTOCOL_HIDBUS,
            .proto_ops = &usb_hid_bus_ops,
        };

        status = device_add(dev, &args, &usbhid->mxdev);
        if (status != MX_OK) {
            usb_desc_iter_release(&iter);
            iotxn_release(usbhid->txn);
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

    return MX_OK;
}

static mx_driver_ops_t usb_hid_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_hid_bind,
};

MAGENTA_DRIVER_BEGIN(usb_hid, usb_hid_driver_ops, "magenta", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB),
    BI_MATCH_IF(EQ, BIND_USB_CLASS, USB_CLASS_HID),
MAGENTA_DRIVER_END(usb_hid)
