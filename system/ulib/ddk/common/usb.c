// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/common/usb.h>
#include <magenta/device/usb.h>
#include <endian.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void usb_control_complete(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t*)cookie);
}

mx_status_t usb_control(mx_device_t* device, uint8_t request_type, uint8_t request,
                        uint16_t value, uint16_t index, void* data, size_t length) {
    iotxn_t* txn;

    mx_status_t status = iotxn_alloc(&txn, 0, length, 0);
    if (status != NO_ERROR) return status;
    txn->protocol = MX_PROTOCOL_USB;
    usb_protocol_data_t* proto_data = iotxn_pdata(txn, usb_protocol_data_t);

    // fill in protocol data
    usb_setup_t* setup = &proto_data->setup;
    setup->bmRequestType = request_type;
    setup->bRequest = request;
    setup->wValue = value;
    setup->wIndex = index;
    setup->wLength = length;
    proto_data->ep_address = 0;
    proto_data->frame = 0;

    bool out = !!((request_type & USB_DIR_MASK) == USB_DIR_OUT);
    if (length > 0 && out) {
        txn->ops->copyto(txn, data, length, 0);
    }

    completion_t completion = COMPLETION_INIT;

    txn->length = length;
    txn->complete_cb = usb_control_complete;
    txn->cookie = &completion;
    iotxn_queue(device, txn);
    completion_wait(&completion, MX_TIME_INFINITE);

    status = txn->status;
    if (status == NO_ERROR) {
        status = txn->actual;

        if (length > 0 && !out) {
            txn->ops->copyfrom(txn, data, txn->actual, 0);
        }
    }
    txn->ops->release(txn);
    return status;
}

mx_status_t usb_get_descriptor(mx_device_t* device, uint8_t request_type, uint16_t type,
                               uint16_t index, void* data, size_t length) {
    return usb_control(device, request_type | USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                       type << 8 | index, 0, data, length);
}

mx_status_t usb_get_string_descriptor(mx_device_t* device, uint8_t id, char** out_string) {
    char string[256];
    uint16_t buffer[128];
    uint16_t languages[128];
    int languageCount = 0;

    string[0] = 0;
    *out_string = NULL;
    memset(languages, 0, sizeof(languages));

    // read list of supported languages
    mx_status_t result = usb_control(device,
            USB_DIR_IN | USB_TYPE_STANDARD |  USB_RECIP_DEVICE, USB_REQ_GET_DESCRIPTOR,
            (USB_DT_STRING << 8) | 0, 0, languages, sizeof(languages));
    if (result < 0) return result;
    languageCount = (result - 2) / 2;

    for (int i = 1; i <= languageCount; i++) {
        memset(buffer, 0, sizeof(buffer));

        result = usb_control(device,
                USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE, USB_REQ_GET_DESCRIPTOR,
                (USB_DT_STRING << 8) | id, le16toh(languages[i]), buffer, sizeof(buffer));
        if (result > 0) {
            // skip first word, and copy the rest to the string, changing shorts to bytes.
            result /= 2;
            int j;
            for (j = 1; j < result; j++) {
                string[j - 1] = le16toh(buffer[j]);
            }
            string[j - 1] = 0;
            break;
        }
    }

    char* s = strdup(string);
    if (!s) return ERR_NO_MEMORY;
    *out_string = s;
    return NO_ERROR;
}

usb_speed_t usb_get_speed(mx_device_t* device) {
    int speed;
    ssize_t result = device->ops->ioctl(device, IOCTL_USB_GET_DEVICE_SPEED, NULL, 0,
                                        &speed, sizeof(speed));
    if (result == sizeof(speed)) {
        return (usb_speed_t)speed;
    } else {
        return USB_SPEED_UNDEFINED;
    }
}

mx_status_t usb_get_status(mx_device_t* device, uint8_t request_type, uint16_t index,
                          void* data, size_t length) {
    return usb_control(device, request_type | USB_DIR_IN, USB_REQ_GET_STATUS, 0,
                       index, data, length);
}

mx_status_t usb_set_configuration(mx_device_t* device, int config) {
    return usb_control(device, USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                       USB_REQ_SET_CONFIGURATION, config, 0, NULL, 0);
}

mx_status_t usb_set_interface(mx_device_t* device, int interface_number, int alt_setting) {
    int args[2] = {interface_number, alt_setting};
    return device->ops->ioctl(device, IOCTL_USB_SET_INTERFACE, args, sizeof(args), NULL, 0);
}

mx_status_t usb_set_feature(mx_device_t* device, uint8_t request_type, int feature, int index) {
    return usb_control(device, request_type, USB_REQ_SET_FEATURE, feature, index, NULL, 0);
}

mx_status_t usb_clear_feature(mx_device_t* device, uint8_t request_type, int feature, int index) {
    return usb_control(device, request_type, USB_REQ_CLEAR_FEATURE, feature, index, NULL, 0);
}

// helper function for allocating iotxns for USB transfers
iotxn_t* usb_alloc_iotxn(uint8_t ep_address, size_t data_size, size_t extra_size) {
    iotxn_t* txn;

    mx_status_t status = iotxn_alloc(&txn, 0, data_size, extra_size);
    if (status != NO_ERROR) {
        return NULL;
    }
    txn->protocol = MX_PROTOCOL_USB;

    usb_protocol_data_t* data = iotxn_pdata(txn, usb_protocol_data_t);
    memset(data, 0, sizeof(*data));
    data->ep_address = ep_address;

    return txn;
}

// initializes a usb_desc_iter_t
mx_status_t usb_desc_iter_init(mx_device_t* device, usb_desc_iter_t* iter) {
    memset(iter, 0, sizeof(*iter));

    int desc_size;
    ssize_t result = device->ops->ioctl(device, IOCTL_USB_GET_DESCRIPTORS_SIZE, NULL, 0,
                                        &desc_size, sizeof(desc_size));
    if (result != sizeof(desc_size)) goto fail;

    uint8_t* desc = malloc(desc_size);
    if (!desc) return ERR_NO_MEMORY;
    iter->desc = desc;
    iter->desc_end = desc + desc_size;
    iter->current = desc;

    result = device->ops->ioctl(device, IOCTL_USB_GET_DESCRIPTORS, NULL, 0, desc, desc_size);
    if (result != desc_size) goto fail;
    return NO_ERROR;

fail:
    free(iter->desc);
    if (result < 0) {
        return result;
    } else {
        return ERR_INTERNAL;
    }
}

// releases resources in a usb_desc_iter_t
void usb_desc_iter_release(usb_desc_iter_t* iter) {
    free(iter->desc);
    iter->desc = NULL;
}

// resets iterator to the beginning
void usb_desc_iter_reset(usb_desc_iter_t* iter) {
    iter->current = iter->desc;
}

// returns the next descriptor
usb_descriptor_header_t* usb_desc_iter_next(usb_desc_iter_t* iter) {
    usb_descriptor_header_t* header = usb_desc_iter_peek(iter);
    if (!header) return NULL;
    iter->current += header->bLength;
    return header;
}

// returns the next descriptor without incrementing the iterator
usb_descriptor_header_t* usb_desc_iter_peek(usb_desc_iter_t* iter) {
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
usb_interface_descriptor_t* usb_desc_iter_next_interface(usb_desc_iter_t* iter, bool skip_alt) {
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
usb_endpoint_descriptor_t* usb_desc_iter_next_endpoint(usb_desc_iter_t* iter) {
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

usb_configuration_descriptor_t* usb_desc_iter_get_config_desc(usb_desc_iter_t* iter) {
    return (usb_configuration_descriptor_t *)iter->desc;
}
