// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/iotxn.h>
#include <ddk/protocol/usb.h>
#include <driver/usb.h>
#include <magenta/compiler.h>
#include <stdlib.h>
#include <string.h>

// helper function for allocating iotxns for USB transfers
__EXPORT iotxn_t* usb_alloc_iotxn(uint8_t ep_address, size_t data_size) {
    iotxn_t* txn;

    mx_status_t status = iotxn_alloc(&txn, 0, data_size);
    if (status != MX_OK) {
        return NULL;
    }
    txn->protocol = MX_PROTOCOL_USB;

    usb_protocol_data_t* data = iotxn_pdata(txn, usb_protocol_data_t);
    memset(data, 0, sizeof(*data));
    data->ep_address = ep_address;

    return txn;
}

// initializes a usb_desc_iter_t for iterating on descriptors past the
// interface's existing descriptors.
static mx_status_t usb_desc_iter_additional_init(usb_protocol_t* usb,
                                                 usb_desc_iter_t* iter) {
    memset(iter, 0, sizeof(*iter));

    void* descriptors;
    size_t length;
    mx_status_t status = usb_get_additional_descriptor_list(usb, &descriptors, &length);
    if (status != MX_OK) {
        return status;
    }

    iter->desc = descriptors;
    iter->desc_end = descriptors + length;
    iter->current = descriptors;
    return MX_OK;
}

// helper function for claiming additional interfaces that satisfy the want_interface predicate,
// want_interface will be passed the supplied arg
__EXPORT mx_status_t usb_claim_additional_interfaces(usb_protocol_t* usb,
                                                     bool (*want_interface)(usb_interface_descriptor_t*, void*),
                                                     void* arg) {
    usb_desc_iter_t iter;
    mx_status_t status = usb_desc_iter_additional_init(usb, &iter);
    if (status != MX_OK) {
        return status;
    }

    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
    while (intf != NULL && want_interface(intf, arg)) {
        // We need to find the start of the next interface to calculate the
        // total length of the current one.
        usb_interface_descriptor_t* next = usb_desc_iter_next_interface(&iter, true);
        // If we're currently on the last interface, next will be NULL.
        void* intf_end = next ? next : (void*)iter.desc_end;
        size_t length = intf_end - (void*)intf;

        status = usb_claim_interface(usb, intf, length);
        if (status != MX_OK) {
            break;
        }
        intf = next;
    }
    usb_desc_iter_release(&iter);
    return status;
}

// initializes a usb_desc_iter_t
__EXPORT mx_status_t usb_desc_iter_init(usb_protocol_t* usb, usb_desc_iter_t* iter) {
    memset(iter, 0, sizeof(*iter));

    void* descriptors;
    size_t length;
    mx_status_t status = usb_get_descriptor_list(usb, &descriptors, &length);
    if (status != MX_OK) {
        return status;
    }

    iter->desc = descriptors;
    iter->desc_end = descriptors + length;
    iter->current = descriptors;
    return MX_OK;
}

// releases resources in a usb_desc_iter_t
__EXPORT void usb_desc_iter_release(usb_desc_iter_t* iter) {
    free(iter->desc);
    iter->desc = NULL;
}

// resets iterator to the beginning
__EXPORT void usb_desc_iter_reset(usb_desc_iter_t* iter) {
    iter->current = iter->desc;
}

// returns the next descriptor
__EXPORT usb_descriptor_header_t* usb_desc_iter_next(usb_desc_iter_t* iter) {
    usb_descriptor_header_t* header = usb_desc_iter_peek(iter);
    if (!header) return NULL;
    iter->current += header->bLength;
    return header;
}

// returns the next descriptor without incrementing the iterator
__EXPORT usb_descriptor_header_t* usb_desc_iter_peek(usb_desc_iter_t* iter) {
    if (iter->current + sizeof(usb_descriptor_header_t) > iter->desc_end) {
        return NULL;
    }
    usb_descriptor_header_t* header = (usb_descriptor_header_t *)iter->current;
    if (iter->current + header->bLength > iter->desc_end) {
        return NULL;
    }
    return header;
}

// returns the next interface descriptor, optionally skipping alternate interfaces
__EXPORT usb_interface_descriptor_t* usb_desc_iter_next_interface(usb_desc_iter_t* iter,
                                                                  bool skip_alt) {
    usb_descriptor_header_t* header = usb_desc_iter_next(iter);

    while (header) {
        if (header->bDescriptorType == USB_DT_INTERFACE) {
            usb_interface_descriptor_t* desc = (usb_interface_descriptor_t *)header;
            if (!skip_alt || desc->bAlternateSetting == 0) {
                return desc;
            }
        }
        header = usb_desc_iter_next(iter);
    }
    // not found
    return NULL;
}

// returns the next endpoint descriptor within the current interface
__EXPORT usb_endpoint_descriptor_t* usb_desc_iter_next_endpoint(usb_desc_iter_t* iter) {
    usb_descriptor_header_t* header = usb_desc_iter_peek(iter);
    while (header) {
        if (header->bDescriptorType == USB_DT_INTERFACE) {
            // we are at end of previous interface
            return NULL;
        }
        iter->current += header->bLength;
        if (header->bDescriptorType == USB_DT_ENDPOINT) {
            return (usb_endpoint_descriptor_t *)header;
        }
        header = usb_desc_iter_peek(iter);
    }
    // not found
    return NULL;
}
