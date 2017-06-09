// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/usb.h>
#include <sync/completion.h>
#include <stdio.h>
#include <string.h>

#include "usb-device.h"
#include "util.h"

static void usb_device_control_complete(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t*)cookie);
}

mx_status_t usb_device_control(mx_device_t* hci_device, uint32_t device_id,
                               uint8_t request_type,  uint8_t request, uint16_t value,
                               uint16_t index, void* data, size_t length) {
    iotxn_t* txn;

    uint32_t flags = (length == 0 ? IOTXN_ALLOC_POOL : 0);
    mx_status_t status = iotxn_alloc(&txn, flags, length);
    if (status != MX_OK) return status;
    txn->protocol = MX_PROTOCOL_USB;
    usb_protocol_data_t* proto_data = iotxn_pdata(txn, usb_protocol_data_t);
    memset(proto_data, 0, sizeof(*proto_data));

    // fill in protocol data
    usb_setup_t* setup = &proto_data->setup;
    setup->bmRequestType = request_type;
    setup->bRequest = request;
    setup->wValue = value;
    setup->wIndex = index;
    setup->wLength = length;
    proto_data->ep_address = 0;
    proto_data->device_id = device_id;

    bool out = !!((request_type & USB_DIR_MASK) == USB_DIR_OUT);
    if (length > 0 && out) {
        iotxn_copyto(txn, data, length, 0);
    }

    completion_t completion = COMPLETION_INIT;

    txn->length = length;
    txn->complete_cb = usb_device_control_complete;
    txn->cookie = &completion;
    iotxn_queue(hci_device, txn);
    completion_wait(&completion, MX_TIME_INFINITE);

    status = txn->status;
    if (status == MX_OK) {
        status = txn->actual;

        if (length > 0 && !out) {
            iotxn_copyfrom(txn, data, txn->actual, 0);
        }
    }
    iotxn_release(txn);
    return status;
}

mx_status_t usb_device_get_descriptor(mx_device_t* hci_device, uint32_t device_id, uint16_t type,
                                      uint16_t index, uint16_t language, void* data, size_t length) {
    return usb_device_control(hci_device, device_id, USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                           USB_REQ_GET_DESCRIPTOR, type << 8 | index, language, data, length);
}

mx_status_t usb_device_get_string_descriptor(mx_device_t* hci_device, uint32_t device_id, uint8_t id,
                                             char* buf, size_t buflen) {
    uint16_t buffer[128];
    uint16_t languages[128];
    int languageCount = 0;

    buf[0] = 0;
    memset(languages, 0, sizeof(languages));

    // read list of supported languages
    mx_status_t result = usb_device_get_descriptor(hci_device, device_id, USB_DT_STRING, 0, 0,
                                                   languages, sizeof(languages));
    if (result < 0) {
        return result;
    }
    languageCount = (result - 2) / 2;

    for (int language = 1; language <= languageCount; language++) {
        memset(buffer, 0, sizeof(buffer));

        result = usb_device_get_descriptor(hci_device, device_id, USB_DT_STRING, id,
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
        }
    }
    // default to empty string
    return 0;
}
