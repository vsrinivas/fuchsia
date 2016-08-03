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

#include "usb-hid.h"

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/input.h>
#include <ddk/protocol/usb-device.h>

#include <hw/usb.h>

#include <ddk/hexdump.h>
#include <magenta/types.h>
#include <runtime/mutex.h>
#include <system/listnode.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USB_HID_SUBCLASS_BOOT   0x01
#define USB_HID_PROTOCOL_KBD    0x01
#define USB_HID_DESC_REPORT     0x22

#define HID_DEAD 1

#define to_hid_dev(d) containerof(d, usb_hid_dev_t, dev)

static mx_status_t usb_hid_get_protocol(mx_device_t* dev, void* out_buf, size_t out_len) {
    usb_hid_dev_t* hid = to_hid_dev(dev);
    if (out_len < sizeof(int)) return ERR_INVALID_ARGS;

    int* reply = out_buf;
    *reply = hid->proto;
    return sizeof(*reply);
}

static mx_status_t usb_hid_get_hid_desc_size(mx_device_t* dev, void* out_buf, size_t out_len) {
    usb_hid_dev_t* hid = to_hid_dev(dev);
    if (out_len < sizeof(size_t)) return ERR_INVALID_ARGS;

    size_t* reply = out_buf;
    *reply = hid->hid_report_desc_len;
    return sizeof(*reply);
}

static mx_status_t usb_hid_get_hid_desc(mx_device_t* dev, void* out_buf, size_t out_len) {
    usb_hid_dev_t* hid = to_hid_dev(dev);
    if (out_len < hid->hid_report_desc_len) return ERR_INVALID_ARGS;

    memcpy(out_buf, hid->hid_report_desc, hid->hid_report_desc_len);
    return hid->hid_report_desc_len;
}

static mx_status_t usb_hid_get_num_reports(mx_device_t* dev, void* out_buf, size_t out_len) {
    usb_hid_dev_t* hid = to_hid_dev(dev);
    if (out_len < sizeof(size_t)) return ERR_INVALID_ARGS;

    size_t* reply = out_buf;
    *reply = hid->num_reports;
    return sizeof(*reply);
}

static mx_status_t usb_hid_get_report_ids(mx_device_t* dev, void* out_buf, size_t out_len) {
    usb_hid_dev_t* hid = to_hid_dev(dev);
    if (out_len < hid->num_reports * sizeof(input_report_id_t))
        return ERR_INVALID_ARGS;

    input_report_id_t* reply = out_buf;
    for (size_t i = 0; i < hid->num_reports; i++) {
        assert(hid->sizes[i].id >= 0);
        *reply++ = (input_report_id_t)hid->sizes[i].id;
    }
    return hid->num_reports * sizeof(input_report_id_t);
}

static mx_status_t usb_hid_get_report_size(mx_device_t* dev, const void* in_buf, size_t in_len,
                                           void* out_buf, size_t out_len) {
    usb_hid_dev_t* hid = to_hid_dev(dev);
    if (in_len < sizeof(input_report_id_t)) return ERR_INVALID_ARGS;
    if (out_len < sizeof(input_report_size_t)) return ERR_INVALID_ARGS;

    const uint8_t* report_id = in_buf;
    input_report_size_t* reply = out_buf;
    *reply = 0;
    for (size_t i = 0; i < hid->num_reports; i++) {
        if (hid->sizes[i].id < 0) break;
        if (hid->sizes[i].id == *report_id) {
            *reply = hid->sizes[i].in_size;
        }
    }
    if (*reply == 0)
        return ERR_INVALID_ARGS;
    return sizeof(*reply);
}

static mx_status_t usb_hid_get_max_reportsize(mx_device_t* dev, void* out_buf, size_t out_len) {
    if (out_len < sizeof(int)) return ERR_INVALID_ARGS;
    usb_hid_dev_t* hid = to_hid_dev(dev);

    int* reply = out_buf;
    *reply = ((int)hid_max_report_size(hid) + 7) / 8 + 1;
    return sizeof(*reply);
}

static ssize_t usb_hid_read(mx_device_t* dev, void* buf, size_t count, mx_off_t off) {
    usb_hid_dev_t* hid = to_hid_dev(dev);

    if (hid->flags & HID_DEAD) {
        return ERR_CHANNEL_CLOSED;
    }

    size_t left;
    mxr_mutex_lock(&hid->fifo.lock);
    ssize_t r = mx_hid_fifo_read(&hid->fifo, buf, count);
    left = mx_hid_fifo_size(&hid->fifo);
    if (left == 0) {
        device_state_clr(&hid->dev, DEV_STATE_READABLE);
    }
    mxr_mutex_unlock(&hid->fifo.lock);

    return r;
}

static ssize_t usb_hid_write(mx_device_t* dev, const void* buf, size_t count, mx_off_t off) {
    printf("usb_hid_write not supported\n");
    // TODO: output reports
    return ERR_NOT_SUPPORTED;
}

static ssize_t usb_hid_ioctl(mx_device_t* dev, uint32_t op, const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len) {
    switch (op) {
    case INPUT_IOCTL_GET_PROTOCOL:
        return usb_hid_get_protocol(dev, out_buf, out_len);
    case INPUT_IOCTL_GET_REPORT_DESC_SIZE:
        return usb_hid_get_hid_desc_size(dev, out_buf, out_len);
    case INPUT_IOCTL_GET_REPORT_DESC:
        return usb_hid_get_hid_desc(dev, out_buf, out_len);
    case INPUT_IOCTL_GET_NUM_REPORTS:
        return usb_hid_get_num_reports(dev, out_buf, out_len);
    case INPUT_IOCTL_GET_REPORT_IDS:
        return usb_hid_get_report_ids(dev, out_buf, out_len);
    case INPUT_IOCTL_GET_REPORT_SIZE:
        return usb_hid_get_report_size(dev, in_buf, in_len, out_buf, out_len);
    case INPUT_IOCTL_GET_MAX_REPORTSIZE:
        return usb_hid_get_max_reportsize(dev, out_buf, out_len);
    }
    return ERR_NOT_SUPPORTED;
}

static void usb_hid_cleanup(usb_hid_dev_t* hid) {
    if (hid->req) {
        hid->usb->free_request(hid->usbdev, hid->req);
    }
    if (hid->hid_report_desc) {
        free(hid->hid_report_desc);
    }
    free(hid);
}

static mx_status_t usb_hid_release(mx_device_t* dev) {
    usb_hid_dev_t* hid = to_hid_dev(dev);
    usb_hid_cleanup(hid);

    return NO_ERROR;
}

static mx_protocol_device_t usb_hid_device_proto = {
    .read = usb_hid_read,
    .write = usb_hid_write,
    .ioctl = usb_hid_ioctl,
    .release = usb_hid_release,
};

static void usb_hid_int_cb(usb_request_t* request) {
    usb_hid_dev_t* hid = (usb_hid_dev_t*)request->client_data;
#ifdef USB_HID_DEBUG
    printf("usb-hid: callback request status %d\n", request->status);
    hexdump(request->buffer, request->buffer_length);
#endif

    if (request->status == ERR_CHANNEL_CLOSED) {
        device_state_set(&hid->dev, DEV_STATE_READABLE);
        hid->flags |= HID_DEAD;
        device_remove(&hid->dev);
        return;
    } else if (request->status == NO_ERROR) {
        mxr_mutex_lock(&hid->fifo.lock);
#ifdef USB_HID_DEBUG
        mx_hid_fifo_dump(&hid->fifo);
#endif
        bool was_empty = mx_hid_fifo_size(&hid->fifo) == 0;
        ssize_t wrote = 0;
        // Add the report id if it's omitted from the device. This happens if
        // there's only one report and its id is zero.
        if (hid->num_reports == 1 && hid->sizes[0].id == 0) {
            wrote = mx_hid_fifo_write(&hid->fifo, (uint8_t*)&wrote, 1);
            if (wrote <= 0) {
                printf("could not write report id to usb-hid fifo (ret=%lu)\n", wrote);
                mxr_mutex_unlock(&hid->fifo.lock);
                goto next_request;
            }
        }
        wrote = mx_hid_fifo_write(&hid->fifo, request->buffer, request->buffer_length);
        if (wrote <= 0) {
            printf("could not write to usb-hid fifo (ret=%lu)\n", wrote);
        } else {
            if (was_empty) {
                device_state_set(&hid->dev, DEV_STATE_READABLE);
            }
#ifdef USB_HID_DEBUG
            mx_hid_fifo_dump(&hid->fifo);
#endif
        }
        mxr_mutex_unlock(&hid->fifo.lock);
    }

next_request:
    request->transfer_length = request->buffer_length;
    hid->usb->queue_request(hid->usbdev, request);
}

static mx_status_t usb_hid_load_hid_report_desc(usb_interface_t* intf, usb_hid_dev_t* hid) {
    for (int i = 0; i < hid->hid_desc->bNumDescriptors; i++) {
        if (hid->hid_desc->descriptors[i].bDescriptorType != USB_HID_DESC_REPORT) continue;
        const size_t len = hid->hid_desc->descriptors[i].wDescriptorLength;
        uint8_t* buf = malloc(len);
        mx_status_t status = hid->usb->control(hid->usbdev, (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE),
                USB_REQ_GET_DESCRIPTOR, USB_HID_DESC_REPORT << 8, hid->interface, buf, len);
        if (status < 0) {
            printf("usb_hid error reading report desc: %d\n", status);
            free(buf);
            return status;
        } else {
            hid_read_report_sizes(buf, len, hid);
            hid->hid_report_desc_len = len;
            hid->hid_report_desc = buf;
#if USB_HID_DEBUG
            printf("usb-hid: dev %p HID descriptor\n", hid);
            hexdump(hid->hid_desc, hid->hid_desc->bLength);
            printf("usb-hid: HID report descriptor\n");
            for (size_t c = 0; c < len; c++) {
                printf("%02x ", buf[c]);
                if (c % 16 == 15) printf("\n");
            }
            printf("\n");
#endif
        }
    }
    return NO_ERROR;
}

static mx_status_t usb_hid_dev_create(usb_hid_dev_t** hid) {
    *hid = calloc(1, sizeof(usb_hid_dev_t));
    if (hid == NULL) {
        return ERR_NO_MEMORY;
    }
    hid_init_report_sizes(*hid);
    mx_hid_fifo_init(&(*hid)->fifo);

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
        // Do not bind to usb keyboards until the usb-keyboard driver can be
        // replaced with this one.
        if (desc->bInterfaceSubClass == USB_HID_SUBCLASS_BOOT &&
            desc->bInterfaceProtocol == USB_HID_PROTOCOL_KBD) {
            printf("usb-hid: skipping USB keyboard\n");
            return ERR_NOT_SUPPORTED;
        }

        usb_hid_dev_t* hid = NULL;
        mx_status_t status = usb_hid_dev_create(&hid);
        if (hid == NULL) {
            return ERR_NO_MEMORY;
        }

        status = device_init(&hid->dev, drv, "usb-hid", &usb_hid_device_proto);
        if (status != NO_ERROR) {
            usb_hid_cleanup(hid);
            return status;
        }

        hid->usbdev = dev;
        hid->usb = usb;
        hid->endpt = endpt;
        hid->interface = desc->bInterfaceNumber;

        if (desc->bInterfaceSubClass == USB_HID_SUBCLASS_BOOT) {
            // Use the non-boot protocol
            hid->usb->control(hid->usbdev, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
                    USB_HID_SET_PROTOCOL, 1, i, NULL, 0);
            hid->proto = desc->bInterfaceProtocol;
        }

        hid->req = hid->usb->alloc_request(hid->usbdev, hid->endpt, hid->endpt->maxpacketsize);
        if (hid->req == NULL) {
            usb_hid_cleanup(hid);
            return ERR_NO_MEMORY;
        }
        hid->req->complete_cb = usb_hid_int_cb;
        hid->req->client_data = hid;

        usb_class_descriptor_t* class_desc = NULL;
        list_for_every_entry(&intf->class_descriptors, class_desc,
                usb_class_descriptor_t, node) {
            if (class_desc->header->bDescriptorType == USB_DT_HID) {
                hid->hid_desc = (usb_hid_descriptor_t*)class_desc->header;
                if (usb_hid_load_hid_report_desc(intf, hid) != NO_ERROR) {
                    hid->hid_desc = NULL;
                    break;
                }
            }
        }
        if (hid->hid_desc == NULL) {
            usb_hid_cleanup(hid);
            return ERR_NOT_SUPPORTED;
        }

        hid->dev.protocol_id = MX_PROTOCOL_INPUT;
        status = device_add(&hid->dev, dev);
        if (status != NO_ERROR) {
            usb_hid_cleanup(hid);
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
