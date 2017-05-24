// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/common/usb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "usb-device.h"
#include "usb-interface.h"
#include "util.h"

// This thread is for calling the iotxn completion callback for iotxns received from our client.
// We do this on a separate thread because it is unsafe to call out on our own completion callback,
// which is called on the main thread of the USB HCI driver.
static int callback_thread(void* arg) {
    usb_interface_t* intf = (usb_interface_t *)arg;
    bool done = false;

    while (!done) {
        // wait for new txns to complete or for signal to exit this thread
        completion_wait(&intf->callback_thread_completion, MX_TIME_INFINITE);

        mtx_lock(&intf->callback_lock);

        completion_reset(&intf->callback_thread_completion);
        done = intf->callback_thread_stop;

        // copy completed txns to a temp list so we can process them outside of our lock
        list_node_t temp_list = LIST_INITIAL_VALUE(temp_list);
        list_node_t* temp;
        while ((temp = list_remove_head(&intf->completed_txns))) {
            list_add_tail(&temp_list, temp);
        }

        mtx_unlock(&intf->callback_lock);

        // call completion callbacks outside of the lock
        iotxn_t* txn;
        iotxn_t* temp_txn;
        list_for_every_entry_safe(&temp_list, txn, temp_txn, iotxn_t, node) {
            iotxn_complete(txn, txn->status, txn->actual);
        }
    }

    return 0;
}

static void start_callback_thread(usb_interface_t* intf) {
    // TODO(voydanoff) Once we have a way of knowing when a driver has bound to us, move the thread
    // start there so we don't have to start a thread unless we know we will need it.
    thrd_create_with_name(&intf->callback_thread, callback_thread, intf, "usb-interface-callback-thread");
}

static void stop_callback_thread(usb_interface_t* intf) {
    mtx_lock(&intf->callback_lock);
    intf->callback_thread_stop = true;
    mtx_unlock(&intf->callback_lock);

    completion_signal(&intf->callback_thread_completion);
    thrd_join(intf->callback_thread, NULL);
}

// iotxn completion for the cloned txns passed down to the HCI driver
static void clone_complete(iotxn_t* clone, void* cookie) {
    iotxn_t* txn = (iotxn_t *)cookie;
    usb_interface_t* intf = (usb_interface_t *)txn->context;

    mtx_lock(&intf->callback_lock);
    // move original txn to completed_txns list so it can be completed on the callback_thread
    txn->status = clone->status;
    txn->actual = clone->actual;
    list_add_tail(&intf->completed_txns, &txn->node);
    mtx_unlock(&intf->callback_lock);
    completion_signal(&intf->callback_thread_completion);

    iotxn_release(clone);
}

static void usb_interface_iotxn_queue(void* ctx, iotxn_t* txn) {
    usb_interface_t* intf = ctx;

    // clone the txn and pass it down to the HCI driver
    iotxn_t* clone = NULL;
    mx_status_t status = iotxn_clone(txn, &clone);
    if (status != NO_ERROR) {
        iotxn_complete(txn, status, 0);
        return;
    }
    usb_protocol_data_t* dest_data = iotxn_pdata(clone, usb_protocol_data_t);
    dest_data->device_id = intf->device_id;

    // stash intf in txn->context so we can get at it in clone_complete()
    txn->context = intf;
    clone->complete_cb = clone_complete;
    clone->cookie = txn;
    iotxn_queue(intf->hci_mxdev, clone);
}

static mx_status_t usb_interface_ioctl(void* ctx, uint32_t op, const void* in_buf,
                                       size_t in_len, void* out_buf, size_t out_len,
                                       size_t* out_actual) {
    usb_interface_t* intf = ctx;

    switch (op) {
    case IOCTL_USB_GET_DEVICE_TYPE: {
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = USB_DEVICE_TYPE_INTERFACE;
        *out_actual = sizeof(*reply);
        return NO_ERROR;
    }
    case IOCTL_USB_GET_DESCRIPTORS_SIZE: {
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ERR_BUFFER_TOO_SMALL;
        *reply = intf->descriptor_length;
        *out_actual = sizeof(*reply);
        return NO_ERROR;
    }
    case IOCTL_USB_GET_DESCRIPTORS: {
        void* descriptors = intf->descriptor;
        size_t desc_length = intf->descriptor_length;
        if (out_len < desc_length) return ERR_BUFFER_TOO_SMALL;
        memcpy(out_buf, descriptors, desc_length);
        *out_actual = desc_length;
        return NO_ERROR;
    }
    default:
        // other ioctls are handled by top level device
        return device_op_ioctl(intf->device->mxdev, op, in_buf, in_len, out_buf, out_len,
                               out_actual);
    }
}

static void usb_interface_release(void* ctx) {
    usb_interface_t* intf = ctx;

    stop_callback_thread(intf);
    free(intf->descriptor);
    free(intf);
}

static mx_protocol_device_t usb_interface_proto = {
    .version = DEVICE_OPS_VERSION,
    .iotxn_queue = usb_interface_iotxn_queue,
    .ioctl = usb_interface_ioctl,
    .release = usb_interface_release,
};

#define NEXT_DESCRIPTOR(header) ((usb_descriptor_header_t*)((void*)header + header->bLength))

static mx_status_t usb_interface_enable_endpoint(usb_interface_t* intf,
                                                 usb_endpoint_descriptor_t* ep,
                                                 bool enable) {
    mx_status_t status = intf->hci_protocol->enable_endpoint(intf->hci_mxdev, intf->device_id, ep,
                                                             enable);
    if (status != NO_ERROR) {
        printf("usb_interface_enable_endpoint failed\n");
    }
    return status;
}

static mx_status_t usb_interface_configure_endpoints(usb_interface_t* intf, uint8_t interface_id,
                                                     uint8_t alt_setting) {
    usb_endpoint_descriptor_t* new_endpoints[USB_MAX_EPS];
    memset(new_endpoints, 0, sizeof(new_endpoints));
    mx_status_t status = NO_ERROR;

    // iterate through our descriptors to find which endpoints should be active
    usb_descriptor_header_t* header = intf->descriptor;
    usb_descriptor_header_t* end = (usb_descriptor_header_t*)((void*)header + intf->descriptor_length);

    bool enable_endpoints = false;
    while (header < end) {
        if (header->bDescriptorType == USB_DT_INTERFACE) {
            usb_interface_descriptor_t* intf_desc = (usb_interface_descriptor_t*)header;
            enable_endpoints = (intf_desc->bAlternateSetting == alt_setting);
        } else if (header->bDescriptorType == USB_DT_ENDPOINT && enable_endpoints) {
            usb_endpoint_descriptor_t* ep = (usb_endpoint_descriptor_t*)header;
            new_endpoints[get_usb_endpoint_index(ep)] = ep;
        }
        header = NEXT_DESCRIPTOR(header);
    }

    // update to new set of endpoints
    // FIXME - how do we recover if we fail half way through processing the endpoints?
    for (size_t i = 0; i < countof(new_endpoints); i++) {
        usb_endpoint_descriptor_t* old_ep = intf->active_endpoints[i];
        usb_endpoint_descriptor_t* new_ep = new_endpoints[i];
        if (old_ep != new_ep) {
            if (old_ep) {
                mx_status_t ret = usb_interface_enable_endpoint(intf, old_ep, false);
                if (ret != NO_ERROR) status = ret;
            }
            if (new_ep) {
                mx_status_t ret = usb_interface_enable_endpoint(intf, new_ep, true);
                if (ret != NO_ERROR) status = ret;
            }
            intf->active_endpoints[i] = new_ep;
        }
    }
    return status;
}

mx_status_t usb_interface_reset_endpoint(mx_device_t* device, uint8_t ep_address) {
    usb_interface_t* intf = device->ctx;
    return intf->hci_protocol->reset_endpoint(intf->hci_mxdev, intf->device_id, ep_address);
}

size_t usb_interface_get_max_transfer_size(mx_device_t* device, uint8_t ep_address) {
    usb_interface_t* intf = device->ctx;
    return intf->hci_protocol->get_max_transfer_size(intf->hci_mxdev, intf->device_id, ep_address);
}

static usb_protocol_t _usb_protocol = {
    .reset_endpoint = usb_interface_reset_endpoint,
    .get_max_transfer_size = usb_interface_get_max_transfer_size,
};

mx_status_t usb_device_add_interface(usb_device_t* device,
                                     usb_device_descriptor_t* device_desc,
                                     usb_interface_descriptor_t* interface_desc,
                                     size_t interface_desc_length) {
    usb_interface_t* intf = calloc(1, sizeof(usb_interface_t));
    if (!intf)
        return ERR_NO_MEMORY;

    mtx_init(&intf->callback_lock, mtx_plain);
    completion_reset(&intf->callback_thread_completion);
    list_initialize(&intf->completed_txns);

    intf->device = device;
    intf->hci_mxdev = device->hci_mxdev;
    intf->hci_protocol = device->hci_protocol;
    intf->device_id = device->device_id;
    intf->descriptor = (usb_descriptor_header_t *)interface_desc;
    intf->descriptor_length = interface_desc_length;

    uint8_t usb_class, usb_subclass, usb_protocol;
    if (interface_desc->bInterfaceClass == 0) {
        usb_class = device_desc->bDeviceClass;
        usb_subclass = device_desc->bDeviceSubClass;
        usb_protocol = device_desc->bDeviceProtocol;
    } else {
        // class/subclass/protocol defined per-interface
        usb_class = interface_desc->bInterfaceClass;
        usb_subclass = interface_desc->bInterfaceSubClass;
        usb_protocol = interface_desc->bInterfaceProtocol;
   }

    int prop_count = 0;
    intf->props[prop_count++] = (mx_device_prop_t){ BIND_PROTOCOL, 0, MX_PROTOCOL_USB };
    intf->props[prop_count++] = (mx_device_prop_t){ BIND_USB_VID, 0, device_desc->idVendor };
    intf->props[prop_count++] = (mx_device_prop_t){ BIND_USB_PID, 0, device_desc->idProduct };
    intf->props[prop_count++] = (mx_device_prop_t){ BIND_USB_CLASS, 0, usb_class };
    intf->props[prop_count++] = (mx_device_prop_t){ BIND_USB_SUBCLASS, 0, usb_subclass };
    intf->props[prop_count++] = (mx_device_prop_t){ BIND_USB_PROTOCOL, 0, usb_protocol };

    mx_status_t status = usb_interface_configure_endpoints(intf, interface_desc->bInterfaceNumber, 0);
    if (status != NO_ERROR) {
        free(intf);
        return status;
    }

    // need to do this first so usb_device_set_interface() can be called from driver bind
    list_add_head(&device->children, &intf->node);

    // callback thread must be started before device_add() since it will recursively
    // bind other drivers to us before it returns.
    start_callback_thread(intf);

    char name[20];
    snprintf(name, sizeof(name), "usb-dev-%03d-i-%d", device->device_id,
             interface_desc->bInterfaceNumber);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = intf,
        .ops = &usb_interface_proto,
        .proto_id = MX_PROTOCOL_USB,
        .proto_ops = &_usb_protocol,
        .props = intf->props,
        .prop_count = prop_count,
    };

    status = device_add(device->mxdev, &args, &intf->mxdev);
    if (status != NO_ERROR) {
        stop_callback_thread(intf);
        list_delete(&intf->node);
        free(interface_desc);
        free(intf);
    }
    return status;
}

mx_status_t usb_device_add_interface_association(usb_device_t* device,
                                                 usb_device_descriptor_t* device_desc,
                                                 usb_interface_assoc_descriptor_t* assoc_desc,
                                                 size_t assoc_desc_length) {
    usb_interface_t* intf = calloc(1, sizeof(usb_interface_t));
    if (!intf)
        return ERR_NO_MEMORY;

    intf->hci_mxdev = device->hci_mxdev;
    intf->hci_protocol = device->hci_protocol;
    intf->device_id = device->device_id;
    intf->descriptor = (usb_descriptor_header_t *)assoc_desc;
    intf->descriptor_length = assoc_desc_length;

    uint8_t usb_class, usb_subclass, usb_protocol;
    if (assoc_desc->bFunctionClass == 0) {
        usb_class = device_desc->bDeviceClass;
        usb_subclass = device_desc->bDeviceSubClass;
        usb_protocol = device_desc->bDeviceProtocol;
    } else {
        // class/subclass/protocol defined per-interface
        usb_class = assoc_desc->bFunctionClass;
        usb_subclass = assoc_desc->bFunctionSubClass;
        usb_protocol = assoc_desc->bFunctionProtocol;
   }

    int prop_count = 0;
    intf->props[prop_count++] = (mx_device_prop_t){ BIND_PROTOCOL, 0, MX_PROTOCOL_USB };
    intf->props[prop_count++] = (mx_device_prop_t){ BIND_USB_VID, 0, device_desc->idVendor };
    intf->props[prop_count++] = (mx_device_prop_t){ BIND_USB_PID, 0, device_desc->idProduct };
    intf->props[prop_count++] = (mx_device_prop_t){ BIND_USB_CLASS, 0, usb_class };
    intf->props[prop_count++] = (mx_device_prop_t){ BIND_USB_SUBCLASS, 0, usb_subclass };
    intf->props[prop_count++] = (mx_device_prop_t){ BIND_USB_PROTOCOL, 0, usb_protocol };

    usb_descriptor_header_t* header = intf->descriptor;
    usb_descriptor_header_t* end = (usb_descriptor_header_t*)((void*)header + intf->descriptor_length);
    while (header < end) {
        if (header->bDescriptorType == USB_DT_INTERFACE) {
            usb_interface_descriptor_t* intf_desc = (usb_interface_descriptor_t*)header;
            if (intf_desc->bAlternateSetting == 0) {
                mx_status_t status = usb_interface_configure_endpoints(intf, intf_desc->bInterfaceNumber, 0);
                if (status != NO_ERROR) return status;
            }
        }
        header = NEXT_DESCRIPTOR(header);
    }

    // callback thread must be started before device_add() since it will recursively
    // bind other drivers to us before it returns.
    start_callback_thread(intf);

    // need to do this first so usb_device_set_interface() can be called from driver bind
    list_add_head(&device->children, &intf->node);

    char name[20];
    snprintf(name, sizeof(name), "usb-dev-%03d-ia-%d", device->device_id, assoc_desc->iFunction);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = intf,
        .ops = &usb_interface_proto,
        .proto_id = MX_PROTOCOL_USB,
        .proto_ops = &_usb_protocol,
        .props = intf->props,
        .prop_count = prop_count,
    };

    mx_status_t status = device_add(device->mxdev, &args, &intf->mxdev);
    if (status != NO_ERROR) {
        stop_callback_thread(intf);
        list_delete(&intf->node);
        free(assoc_desc);
        free(intf);
    }
    return status;
}


void usb_device_remove_interfaces(usb_device_t* device) {
    usb_interface_t* intf;
    while ((intf = list_remove_head_type(&device->children, usb_interface_t, node)) != NULL) {
        device_remove(intf->mxdev);
    }
}

uint32_t usb_interface_get_device_id(mx_device_t* device) {
    usb_interface_t* intf = device->ctx;
    return intf->device_id;
}

bool usb_interface_contains_interface(usb_interface_t* intf, uint8_t interface_id) {
    usb_descriptor_header_t* header = intf->descriptor;
    usb_descriptor_header_t* end = (usb_descriptor_header_t*)((void*)header + intf->descriptor_length);

    while (header < end) {
        if (header->bDescriptorType == USB_DT_INTERFACE) {
            usb_interface_descriptor_t* intf_desc = (usb_interface_descriptor_t*)header;
            if (intf_desc->bInterfaceNumber == interface_id) {
                return true;
            }
        }
        header = NEXT_DESCRIPTOR(header);
    }
    return false;
}

mx_status_t usb_interface_set_alt_setting(usb_interface_t* intf, uint8_t interface_id,
                                          uint8_t alt_setting) {
    mx_status_t status = usb_interface_configure_endpoints(intf, interface_id, alt_setting);
    if (status != NO_ERROR) return status;

    return usb_device_control(intf->hci_mxdev, intf->device_id,
                              USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE,
                              USB_REQ_SET_INTERFACE, alt_setting, interface_id, NULL, 0);
}
