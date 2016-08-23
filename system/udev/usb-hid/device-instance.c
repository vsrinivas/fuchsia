// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <ddk/common/usb.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Until we do full HID parsing, we put mouse and keyboard devices into boot
// protocol mode. In particular, a mouse will always send 3 byte reports (see
// ddk/protocol/input.h for the format). This macro sets ioctl return values for
// boot mouse devices to reflect the boot protocol, rather than what the device
// itself reports.
// TODO: update this to include keyboards if we find a keyboard in the wild that
// needs a hack as well.
#define BOOT_MOUSE_HACK 1

#define to_hid_instance(d) containerof(d, usb_hid_dev_instance_t, dev)
#define bits_to_bytes(n) (((n) + 7) / 8)

mx_status_t usb_hid_create_instance(usb_hid_dev_instance_t** dev) {
    *dev = calloc(1, sizeof(usb_hid_dev_instance_t));
    if (*dev == NULL) {
        return ERR_NO_MEMORY;
    }
    mx_hid_fifo_init(&(*dev)->fifo);
    return NO_ERROR;
}

void usb_hid_cleanup_instance(usb_hid_dev_instance_t* dev) {
    if (!(dev->flags & HID_FLAGS_DEAD)) {
        mtx_lock(&dev->root->instance_lock);
        list_delete(&dev->node);
        mtx_unlock(&dev->root->instance_lock);
    }
    free(dev);
}

static input_report_size_t usb_hid_get_report_size_by_id(usb_hid_dev_t* hid,
        input_report_id_t id, input_report_type_t type) {
#if BOOT_MOUSE_HACK
    // Ignore the HID report descriptor from the device, since we're putting the
    // device into boot protocol mode.
    if (hid->proto == INPUT_PROTO_MOUSE) return 3;
#endif
    for (size_t i = 0; i < hid->num_reports; i++) {
        if (hid->sizes[i].id < 0) break;
        if (hid->sizes[i].id == id) {
            switch (type) {
            case INPUT_REPORT_INPUT:
                return bits_to_bytes(hid->sizes[i].in_size);
            case INPUT_REPORT_OUTPUT:
                return bits_to_bytes(hid->sizes[i].out_size);
            case INPUT_REPORT_FEATURE:
                return bits_to_bytes(hid->sizes[i].feat_size);
            }
        }
    }
    return 0;
}

static mx_status_t usb_hid_get_protocol(usb_hid_dev_t* hid, void* out_buf, size_t out_len) {
    if (out_len < sizeof(int)) return ERR_INVALID_ARGS;

    int* reply = out_buf;
    *reply = hid->proto;
    return sizeof(*reply);
}

static mx_status_t usb_hid_get_hid_desc_size(usb_hid_dev_t* hid, void* out_buf, size_t out_len) {
    if (out_len < sizeof(size_t)) return ERR_INVALID_ARGS;

    size_t* reply = out_buf;
    *reply = hid->hid_report_desc_len;
    return sizeof(*reply);
}

static mx_status_t usb_hid_get_hid_desc(usb_hid_dev_t* hid, void* out_buf, size_t out_len) {
    if (out_len < hid->hid_report_desc_len) return ERR_INVALID_ARGS;

    memcpy(out_buf, hid->hid_report_desc, hid->hid_report_desc_len);
    return hid->hid_report_desc_len;
}

static mx_status_t usb_hid_get_num_reports(usb_hid_dev_t* hid, void* out_buf, size_t out_len) {
    if (out_len < sizeof(size_t)) return ERR_INVALID_ARGS;

    size_t* reply = out_buf;
    *reply = hid->num_reports;
#if BOOT_MOUSE_HACK
    if (hid->proto == INPUT_PROTO_MOUSE) *reply = 1;
#endif
    return sizeof(*reply);
}

static mx_status_t usb_hid_get_report_ids(usb_hid_dev_t* hid, void* out_buf, size_t out_len) {
#if BOOT_MOUSE_HACK
    if (hid->proto == INPUT_PROTO_MOUSE) {
        if (out_len < sizeof(input_report_id_t)) {
            return ERR_INVALID_ARGS;
        }
    } else {
        if (out_len < hid->num_reports * sizeof(input_report_id_t))
        return ERR_INVALID_ARGS;
    }
#else
    if (out_len < hid->num_reports * sizeof(input_report_id_t))
        return ERR_INVALID_ARGS;
#endif

    input_report_id_t* reply = out_buf;
#if BOOT_MOUSE_HACK
    if (hid->proto == INPUT_PROTO_MOUSE) {
        *reply = 0;
        return sizeof(input_report_id_t);
    }
#endif
    for (size_t i = 0; i < hid->num_reports; i++) {
        assert(hid->sizes[i].id >= 0);
        *reply++ = (input_report_id_t)hid->sizes[i].id;
    }
    return hid->num_reports * sizeof(input_report_id_t);
}

static mx_status_t usb_hid_get_report_size(usb_hid_dev_t* hid, const void* in_buf, size_t in_len,
                                           void* out_buf, size_t out_len) {
    if (in_len < sizeof(input_get_report_size_t)) return ERR_INVALID_ARGS;
    if (out_len < sizeof(input_report_size_t)) return ERR_INVALID_ARGS;

    const input_get_report_size_t* inp = in_buf;

    input_report_size_t* reply = out_buf;
    *reply = usb_hid_get_report_size_by_id(hid, inp->id, inp->type);
    if (*reply == 0)
        return ERR_INVALID_ARGS;
    return sizeof(*reply);
}

static mx_status_t usb_hid_get_max_reportsize(usb_hid_dev_t* hid, void* out_buf, size_t out_len) {
    if (out_len < sizeof(int)) return ERR_INVALID_ARGS;

    input_report_size_t* reply = out_buf;
    *reply = 0;
    for (int i = 0; i < HID_MAX_REPORT_IDS; i++) {
        if (hid->sizes[i].id >= 0 &&
            hid->sizes[i].in_size > *reply)
            *reply = hid->sizes[i].in_size;
    }

    *reply = bits_to_bytes(*reply);
#if BOOT_MOUSE_HACK
    if (hid->proto == INPUT_PROTO_MOUSE) *reply = 3;
#endif
    return sizeof(*reply);
}

static mx_status_t usb_hid_get_report(usb_hid_dev_t* hid, const void* in_buf, size_t in_len,
                                      void* out_buf, size_t out_len) {
    if (in_len < sizeof(input_get_report_t)) return ERR_INVALID_ARGS;
    const input_get_report_t* inp = in_buf;

    input_report_size_t needed = usb_hid_get_report_size_by_id(hid, inp->id, inp->type);
    if (needed == 0) return ERR_INVALID_ARGS;
    if (out_len < (size_t)needed) return ERR_NOT_ENOUGH_BUFFER;

    return usb_control(hid->usbdev, (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
            USB_HID_GET_REPORT, (inp->type << 8 | inp->id), hid->interface, out_buf, out_len);
}

static mx_status_t usb_hid_set_report(usb_hid_dev_t* hid, const void* in_buf, size_t in_len) {

    if (in_len < sizeof(input_set_report_t)) return ERR_INVALID_ARGS;
    const input_set_report_t* inp = in_buf;

    input_report_size_t needed = usb_hid_get_report_size_by_id(hid, inp->id, inp->type);
    if (needed == 0) return ERR_INVALID_ARGS;
    if (in_len - sizeof(input_set_report_t) < (size_t)needed) return ERR_INVALID_ARGS;

    return usb_control(hid->usbdev, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
            USB_HID_SET_REPORT, (inp->type << 8 | inp->id), hid->interface,
            (void*)inp->data, in_len - sizeof(input_set_report_t));
}

static ssize_t usb_hid_read_instance(mx_device_t* dev, void* buf, size_t count, mx_off_t off) {
    usb_hid_dev_instance_t* hid = to_hid_instance(dev);

    if (hid->flags & HID_FLAGS_DEAD) {
        return ERR_CHANNEL_CLOSED;
    }

    size_t left;
    mtx_lock(&hid->fifo.lock);
    ssize_t r = mx_hid_fifo_read(&hid->fifo, buf, count);
    left = mx_hid_fifo_size(&hid->fifo);
    if (left == 0) {
        device_state_clr(&hid->dev, DEV_STATE_READABLE);
    }
    mtx_unlock(&hid->fifo.lock);
    return r;
}

static ssize_t usb_hid_ioctl_instance(mx_device_t* dev, uint32_t op,
        const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    usb_hid_dev_instance_t* hid = to_hid_instance(dev);
    if (hid->flags & HID_FLAGS_DEAD) return ERR_CHANNEL_CLOSED;

    switch (op) {
    case INPUT_IOCTL_GET_PROTOCOL:
        return usb_hid_get_protocol(hid->root, out_buf, out_len);
    case INPUT_IOCTL_GET_REPORT_DESC_SIZE:
        return usb_hid_get_hid_desc_size(hid->root, out_buf, out_len);
    case INPUT_IOCTL_GET_REPORT_DESC:
        return usb_hid_get_hid_desc(hid->root, out_buf, out_len);
    case INPUT_IOCTL_GET_NUM_REPORTS:
        return usb_hid_get_num_reports(hid->root, out_buf, out_len);
    case INPUT_IOCTL_GET_REPORT_IDS:
        return usb_hid_get_report_ids(hid->root, out_buf, out_len);
    case INPUT_IOCTL_GET_REPORT_SIZE:
        return usb_hid_get_report_size(hid->root, in_buf, in_len, out_buf, out_len);
    case INPUT_IOCTL_GET_MAX_REPORTSIZE:
        return usb_hid_get_max_reportsize(hid->root, out_buf, out_len);
    case INPUT_IOCTL_GET_REPORT:
        return usb_hid_get_report(hid->root, in_buf, in_len, out_buf, out_len);
    case INPUT_IOCTL_SET_REPORT:
        return usb_hid_set_report(hid->root, in_buf, in_len);
    }
    return ERR_NOT_SUPPORTED;
}

static mx_status_t usb_hid_release_instance(mx_device_t* dev) {
    usb_hid_dev_instance_t* hid = to_hid_instance(dev);
    usb_hid_cleanup_instance(hid);
    return NO_ERROR;
}

mx_protocol_device_t usb_hid_instance_proto = {
    .read = usb_hid_read_instance,
    .ioctl = usb_hid_ioctl_instance,
    .release = usb_hid_release_instance,
};
