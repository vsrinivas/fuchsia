// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/usb.h>
#include <ddk/usb-request.h>
#include <sync/completion.h>
#include <stdio.h>
#include <string.h>

#include "usb-bus.h"
#include "usb-device.h"
#include "util.h"

static void usb_device_control_complete(usb_request_t* req, void* cookie) {
    completion_signal((completion_t*)cookie);
}

zx_status_t usb_device_control(usb_device_t* dev, uint8_t request_type,
                               uint8_t request, uint16_t value,
                               uint16_t index, void* data, size_t length) {
    usb_request_t* req = NULL;
    bool use_free_list = length == 0;
    if (use_free_list) {
        req = usb_request_pool_get(&dev->free_reqs, length);
    }
    if (req == NULL) {
        zx_status_t status = usb_request_alloc(&req, dev->bus->bti_handle, length, 0);
        if (status != ZX_OK) return status;
    }

    // fill in protocol data
    usb_setup_t* setup = &req->setup;
    setup->bmRequestType = request_type;
    setup->bRequest = request;
    setup->wValue = value;
    setup->wIndex = index;
    setup->wLength = length;
    req->header.device_id = dev->device_id;

    bool out = !!((request_type & USB_DIR_MASK) == USB_DIR_OUT);
    if (length > 0 && out) {
        usb_request_copyto(req, data, length, 0);
    }

    completion_t completion = COMPLETION_INIT;

    req->header.length = length;
    req->complete_cb = usb_device_control_complete;
    req->cookie = &completion;

    usb_hci_request_queue(&dev->hci, req);
    completion_wait(&completion, ZX_TIME_INFINITE);

    zx_status_t status = req->response.status;
    if (status == ZX_OK) {
        status = req->response.actual;

        if (length > 0 && !out) {
            usb_request_copyfrom(req, data, req->response.actual, 0);
        }
    }
    if (use_free_list) {
        usb_request_pool_add(&dev->free_reqs, req);
    } else {
        usb_request_release(req);
    }
    return status;
}

zx_status_t usb_device_get_descriptor(usb_device_t* dev, uint16_t type, uint16_t index,
                                      uint16_t language, void* data, size_t length) {
    return usb_device_control(dev, USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                           USB_REQ_GET_DESCRIPTOR, type << 8 | index, language, data, length);
}

zx_status_t usb_device_get_string_descriptor(usb_device_t* dev, uint8_t id,
                                             char* buf, size_t buflen) {
    uint16_t buffer[128];
    uint16_t languages[128];
    int languageCount = 0;

    buf[0] = 0;
    memset(languages, 0, sizeof(languages));

    // read list of supported languages
    zx_status_t result = usb_device_get_descriptor(dev, USB_DT_STRING, 0, 0,
                                                   languages, sizeof(languages));
    if (result == ZX_ERR_IO_REFUSED || result == ZX_ERR_IO_INVALID) {
        // some devices do not support fetching language list
        // in that case assume US English (0x0409)
        usb_hci_reset_endpoint(&dev->hci, dev->device_id, 0);
        languages[1] = htole16(0x0409);
        result = 4;
    } else if (result < 0) {
        return result;
    }
    languageCount = (result - 2) / 2;

    for (int language = 1; language <= languageCount; language++) {
        memset(buffer, 0, sizeof(buffer));

        result = usb_device_get_descriptor(dev, USB_DT_STRING, id,
                                           le16toh(languages[language]), buffer, sizeof(buffer));
        // use first language on the list
        if (result > 0) {
            // First word is descriptor length and type
            usb_descriptor_header_t* header = (usb_descriptor_header_t *)buffer;
            uint8_t length = header->bLength;
            if (length > result) {
                length = result;
            }

            uint16_t* src = &buffer[1];
            uint16_t* src_end = src + length / sizeof(uint16_t);
            char* dest = buf;
            // subtract 2 since our output UTF8 chars can be up to 3 bytes long,
            // plus one extra for zero termination
            char* dest_end = buf + buflen - 3;

            // convert to UTF8 while copying to buffer
            while (src < src_end && dest < dest_end) {
                uint16_t uch = *src++;
                if (uch < 0x80u) {
                    *dest++ = (char)uch;
                } else if (uch < 0x800u) {
                    *dest++ = 0xC0 | (uch >> 6);
                    *dest++ = 0x80 | (uch & 0x3F);
                } else {
                    // with 16 bit input, 3 characters of output is the maximum we will see
                   *dest++ = 0xE0 | (uch >> 12);
                   *dest++ = 0x80 | (uch >> 6);
                   *dest++ = 0x80 | (uch & 0x3F);
                }
            }
            *dest++ = 0;
            return dest - buf;
        } else if (result == ZX_ERR_IO_REFUSED || result == ZX_ERR_IO_INVALID) {
            usb_hci_reset_endpoint(&dev->hci, dev->device_id, 0);
        }
    }
    // default to empty string
    return 0;
}
