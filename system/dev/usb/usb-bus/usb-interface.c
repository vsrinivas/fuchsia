// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/protocol/usb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "usb-bus.h"
#include "usb-device.h"
#include "usb-interface.h"
#include "util.h"

// This thread is for calling the usb request completion callback for requests received from our
// client. We do this on a separate thread because it is unsafe to call out on our own completion
// callback, which is called on the main thread of the USB HCI driver.
static int callback_thread(void* arg) {
    usb_interface_t* intf = (usb_interface_t *)arg;
    bool done = false;

    while (!done) {
        // wait for new usb requests to complete or for signal to exit this thread
        sync_completion_wait(&intf->callback_thread_completion, ZX_TIME_INFINITE);

        mtx_lock(&intf->callback_lock);

        sync_completion_reset(&intf->callback_thread_completion);
        done = intf->callback_thread_stop;

        // copy completed requests to a temp list so we can process them outside of our lock
        list_node_t temp_list = LIST_INITIAL_VALUE(temp_list);
        list_move(&intf->completed_reqs, &temp_list);

        mtx_unlock(&intf->callback_lock);

        // call completion callbacks outside of the lock
        usb_request_t* req;
        while ((req = list_remove_head_type(&temp_list, usb_request_t, node))) {
            usb_request_complete(req, req->response.status, req->response.actual);
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

    sync_completion_signal(&intf->callback_thread_completion);
    thrd_join(intf->callback_thread, NULL);
}

// usb request completion for the requests passed down to the HCI driver
static void request_complete(usb_request_t* req, void* cookie) {
    usb_interface_t* intf = (usb_interface_t *)req->cookie;

    mtx_lock(&intf->callback_lock);
    // move original request to completed_reqs list so it can be completed on the callback_thread
    req->complete_cb = req->saved_complete_cb;
    req->cookie = req->saved_cookie;
    list_add_tail(&intf->completed_reqs, &req->node);
    mtx_unlock(&intf->callback_lock);
    sync_completion_signal(&intf->callback_thread_completion);
}

static void hci_queue(void* ctx, usb_request_t* req) {
    usb_interface_t* intf = ctx;

    req->header.device_id = intf->device_id;
    // save the existing callback and cookie, so we can replace them
    // with our own before passing the request to the HCI driver.
    req->saved_complete_cb = req->complete_cb;
    req->saved_cookie = req->cookie;

    req->complete_cb = request_complete;
    // set intf as the cookie so we can get at it in request_complete()
    req->cookie = intf;

    usb_hci_request_queue(&intf->hci, req);
}

static zx_status_t usb_interface_ioctl(void* ctx, uint32_t op, const void* in_buf,
                                       size_t in_len, void* out_buf, size_t out_len,
                                       size_t* out_actual) {
    usb_interface_t* intf = ctx;

    switch (op) {
    case IOCTL_USB_GET_DEVICE_TYPE: {
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ZX_ERR_BUFFER_TOO_SMALL;
        *reply = USB_DEVICE_TYPE_INTERFACE;
        *out_actual = sizeof(*reply);
        return ZX_OK;
    }
    case IOCTL_USB_GET_DESCRIPTORS_SIZE: {
        int* reply = out_buf;
        if (out_len < sizeof(*reply)) return ZX_ERR_BUFFER_TOO_SMALL;
        *reply = intf->descriptor_length;
        *out_actual = sizeof(*reply);
        return ZX_OK;
    }
    case IOCTL_USB_GET_DESCRIPTORS: {
        void* descriptors = intf->descriptor;
        size_t desc_length = intf->descriptor_length;
        if (out_len < desc_length) return ZX_ERR_BUFFER_TOO_SMALL;
        memcpy(out_buf, descriptors, desc_length);
        *out_actual = desc_length;
        return ZX_OK;
    }
    default:
        // other ioctls are handled by top level device
        return device_ioctl(intf->device->zxdev, op, in_buf, in_len,
                            out_buf, out_len, out_actual);
    }
}

static void usb_interface_unbind(void* ctx) {
    usb_interface_t* intf = ctx;
    device_remove(intf->zxdev);
}

static void usb_interface_release(void* ctx) {
    usb_interface_t* intf = ctx;

    stop_callback_thread(intf);
    free(intf->descriptor);
    free(intf);
}

static zx_protocol_device_t usb_interface_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = usb_interface_ioctl,
    .unbind = usb_interface_unbind,
    .release = usb_interface_release,
};

#define NEXT_DESCRIPTOR(header) ((usb_descriptor_header_t*)((void*)header + header->bLength))

static zx_status_t usb_interface_enable_endpoint(usb_interface_t* intf,
                                                 usb_endpoint_descriptor_t* ep,
                                                 usb_ss_ep_comp_descriptor_t* ss_comp_desc,
                                                 bool enable) {
    zx_status_t status = usb_hci_enable_endpoint(&intf->hci, intf->device_id, ep, ss_comp_desc,
                                                 enable);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_interface_enable_endpoint failed: %d\n", status);
    }
    return status;
}

static zx_status_t usb_interface_configure_endpoints(usb_interface_t* intf, uint8_t interface_id,
                                                     uint8_t alt_setting) {
    usb_endpoint_descriptor_t* new_endpoints[USB_MAX_EPS] = {};
    bool interface_endpoints[USB_MAX_EPS] = {};
    zx_status_t status = ZX_OK;

    // iterate through our descriptors to find which endpoints should be active
    usb_descriptor_header_t* header = intf->descriptor;
    usb_descriptor_header_t* end = (usb_descriptor_header_t*)((void*)header + intf->descriptor_length);
    int cur_interface = -1;

    bool enable_endpoints = false;
    while (header < end) {
        if (header->bDescriptorType == USB_DT_INTERFACE) {
            usb_interface_descriptor_t* intf_desc = (usb_interface_descriptor_t*)header;
            cur_interface = intf_desc->bInterfaceNumber;
            enable_endpoints = (intf_desc->bAlternateSetting == alt_setting);
        } else if (header->bDescriptorType == USB_DT_ENDPOINT && cur_interface == interface_id) {
            usb_endpoint_descriptor_t* ep = (usb_endpoint_descriptor_t*)header;
            int ep_index = get_usb_endpoint_index(ep);
            interface_endpoints[ep_index] = true;
            if (enable_endpoints) {
                new_endpoints[ep_index] = ep;
            }
        }
        header = NEXT_DESCRIPTOR(header);
    }

    // update to new set of endpoints
    // FIXME - how do we recover if we fail half way through processing the endpoints?
    for (size_t i = 0; i < countof(new_endpoints); i++) {
        if (interface_endpoints[i]) {
            usb_endpoint_descriptor_t* old_ep = intf->active_endpoints[i];
            usb_endpoint_descriptor_t* new_ep = new_endpoints[i];
            if (old_ep != new_ep) {
                if (old_ep) {
                    zx_status_t ret = usb_interface_enable_endpoint(intf, old_ep, NULL, false);
                    if (ret != ZX_OK) status = ret;
                }
                if (new_ep) {
                    usb_ss_ep_comp_descriptor_t* ss_comp_desc = NULL;
                    usb_descriptor_header_t* next =
                                    (usb_descriptor_header_t *)((void *)new_ep + new_ep->bLength);
                    if (next + sizeof(*ss_comp_desc) <= end
                        && next->bDescriptorType == USB_DT_SS_EP_COMPANION) {
                        ss_comp_desc = (usb_ss_ep_comp_descriptor_t *)next;
                    }
                    zx_status_t ret = usb_interface_enable_endpoint(intf, new_ep, ss_comp_desc, true);
                    if (ret != ZX_OK) status = ret;
                }
                intf->active_endpoints[i] = new_ep;
            }
        }
    }
    return status;
}

static zx_status_t usb_interface_req_alloc(void* ctx, usb_request_t** out, uint64_t data_size,
                                           uint8_t ep_address) {
    usb_interface_t* intf = ctx;

    return usb_request_alloc(out, intf->device->bus->bti_handle, data_size, ep_address);
}

static zx_status_t usb_interface_req_alloc_vmo(void* ctx, usb_request_t** out,
                                               zx_handle_t vmo_handle, uint64_t vmo_offset,
                                               uint64_t length, uint8_t ep_address) {
    usb_interface_t* intf = ctx;

    return usb_request_alloc_vmo(out, intf->device->bus->bti_handle, vmo_handle, vmo_offset,
                                 length, ep_address);
}

static zx_status_t usb_interface_req_init(void* ctx, usb_request_t* req, zx_handle_t vmo_handle,
                                          uint64_t vmo_offset, uint64_t length,
                                          uint8_t ep_address) {
    usb_interface_t* intf = ctx;

    return usb_request_init(req, intf->device->bus->bti_handle, vmo_handle, vmo_offset, length,
                            ep_address);
}

static ssize_t usb_interface_req_copy_from(void* ctx, usb_request_t* req, void* data,
                                          size_t length, size_t offset) {
    return usb_request_copyfrom(req, data, length, offset);
}

static ssize_t usb_interface_req_copy_to(void* ctx, usb_request_t* req, const void* data,
                                        size_t length, size_t offset) {
    return usb_request_copyto(req, data, length, offset);
}

static zx_status_t usb_interface_req_mmap(void* ctx, usb_request_t* req, void** data) {
    return usb_request_mmap(req, data);
}

static zx_status_t usb_interface_req_cacheop(void* ctx, usb_request_t* req, uint32_t op,
                                             size_t offset, size_t length) {
    return usb_request_cacheop(req, op, offset, length);
}

static zx_status_t usb_interface_req_cache_flush(void* ctx, usb_request_t* req,
                                                 size_t offset, size_t length) {
    return usb_request_cache_flush(req, offset, length);
}

static zx_status_t usb_interface_req_cache_flush_invalidate(void* ctx, usb_request_t* req,
                                                            zx_off_t offset, size_t length) {
    return usb_request_cache_flush_invalidate(req, offset, length);
}

static zx_status_t usb_interface_req_physmap(void* ctx, usb_request_t* req) {
    return usb_request_physmap(req);
}

static void usb_interface_req_release(void* ctx, usb_request_t* req) {
    usb_request_release(req);
}

static void usb_interface_req_complete(void* ctx, usb_request_t* req,
                                       zx_status_t status, zx_off_t actual) {
    usb_request_complete(req, status, actual);
}

static void usb_interface_req_phys_iter_init(void* ctx, phys_iter_t* iter, usb_request_t* req,
                                             size_t max_length) {
    usb_request_phys_iter_init(iter, req, max_length);
}

static void usb_control_complete(usb_request_t* req, void* cookie) {
    sync_completion_signal((sync_completion_t*)cookie);
}

static zx_status_t usb_interface_control(void* ctx, uint8_t request_type, uint8_t request,
                                         uint16_t value, uint16_t index, void* data,
                                         size_t length, zx_time_t timeout, size_t* out_length) {
    usb_interface_t* intf = ctx;

    usb_request_t* req = NULL;
    bool use_free_list = length == 0;
    if (use_free_list) {
        req = usb_request_pool_get(&intf->free_reqs, length);
    }

    if (req == NULL) {
        zx_status_t status = usb_request_alloc(&req, intf->device->bus->bti_handle, length, 0);
        if (status != ZX_OK) {
            return status;
        }
    }

    // fill in protocol data
    usb_setup_t* setup = &req->setup;
    setup->bmRequestType = request_type;
    setup->bRequest = request;
    setup->wValue = value;
    setup->wIndex = index;
    setup->wLength = length;

    bool out = !!((request_type & USB_DIR_MASK) == USB_DIR_OUT);
    if (length > 0 && out) {
        usb_request_copyto(req, data, length, 0);
    }

    sync_completion_t completion = SYNC_COMPLETION_INIT;

    req->header.device_id = intf->device_id;
    req->header.length = length;
    req->complete_cb = usb_control_complete;
    req->cookie = &completion;
    // We call this directly instead of via hci_queue, as it's safe to call our
    // own completion callback, and prevents clients getting into odd deadlocks.
    usb_hci_request_queue(&intf->hci, req);
    zx_status_t status = sync_completion_wait(&completion, timeout);

    if (status == ZX_OK) {
        status = req->response.status;
    } else if (status == ZX_ERR_TIMED_OUT) {
        // cancel transactions and wait for request to be completed
        sync_completion_reset(&completion);
        status = usb_hci_cancel_all(&intf->hci, intf->device_id, 0);
        if (status == ZX_OK) {
            sync_completion_wait(&completion, ZX_TIME_INFINITE);
            status = ZX_ERR_TIMED_OUT;
        }
    }
    if (status == ZX_OK) {
        if (out_length != NULL) {
            *out_length = req->response.actual;
        }

        if (length > 0 && !out) {
            usb_request_copyfrom(req, data, req->response.actual, 0);
        }
    }

    if (use_free_list) {
        usb_request_pool_add(&intf->free_reqs, req);
    } else {
        usb_request_release(req);
    }
    return status;
}

static void usb_interface_request_queue(void* ctx, usb_request_t* usb_request) {
    hci_queue(ctx, usb_request);
}

static usb_speed_t usb_interface_get_speed(void* ctx) {
    usb_interface_t* intf = ctx;
    return intf->device->speed;
}

static zx_status_t usb_interface_set_interface(void* ctx, int interface_number, int alt_setting) {
    usb_interface_t* intf = ctx;
    return usb_device_set_interface(intf->device, interface_number, alt_setting);
}

static zx_status_t usb_interface_set_configuration(void* ctx, int configuration) {
    usb_interface_t* intf = ctx;
    return usb_device_set_configuration(intf->device, configuration);
}

static zx_status_t usb_interface_reset_endpoint(void* ctx, uint8_t ep_address) {
    usb_interface_t* intf = ctx;
    return usb_hci_reset_endpoint(&intf->hci, intf->device_id, ep_address);
}

static size_t usb_interface_get_max_transfer_size(void* ctx, uint8_t ep_address) {
    usb_interface_t* intf = ctx;
    return usb_hci_get_max_transfer_size(&intf->hci, intf->device_id, ep_address);
}

static uint32_t _usb_interface_get_device_id(void* ctx) {
    usb_interface_t* intf = ctx;
    return intf->device_id;
}

static void usb_interface_get_device_descriptor(void* ctx,
                                                usb_device_descriptor_t* out_desc) {
    usb_interface_t* intf = ctx;
    memcpy(out_desc, &intf->device->device_desc, sizeof(usb_device_descriptor_t));
}

static zx_status_t usb_interface_get_descriptor_list(void* ctx, void** out_descriptors,
                                                     size_t* out_length) {
    usb_interface_t* intf = ctx;
    void* descriptors = malloc(intf->descriptor_length);
    if (!descriptors) {
        *out_descriptors = NULL;
        *out_length = 0;
        return ZX_ERR_NO_MEMORY;
    }
    memcpy(descriptors, intf->descriptor, intf->descriptor_length);
    *out_descriptors = descriptors;
    *out_length = intf->descriptor_length;
    return ZX_OK;
}

static zx_status_t usb_interface_get_additional_descriptor_list(void* ctx, void** out_descriptors,
                                                                size_t* out_length) {
    usb_interface_t* intf = ctx;

    usb_device_t* device = intf->device;
    usb_configuration_descriptor_t* config = device->config_descs[device->current_config_index];
    usb_descriptor_header_t* header = NEXT_DESCRIPTOR(config);
    usb_descriptor_header_t* end = (usb_descriptor_header_t*)((void*)config + le16toh(config->wTotalLength));

    usb_interface_descriptor_t* result = NULL;
    while (header < end) {
        if (header->bDescriptorType == USB_DT_INTERFACE) {
            usb_interface_descriptor_t* test_intf = (usb_interface_descriptor_t*)header;
            // We are only interested in descriptors past the last stored descriptor
            // for the current interface.
            if (test_intf->bAlternateSetting == 0 &&
                test_intf->bInterfaceNumber > intf->last_interface_id) {
                result = test_intf;
                break;
            }
        }
        header = NEXT_DESCRIPTOR(header);
    }
    if (!result) {
        *out_descriptors = NULL;
        *out_length = 0;
        return ZX_OK;
    }
    size_t length = (void*)end - (void*)result;
    void* descriptors = malloc(length);
    if (!descriptors) {
        *out_descriptors = NULL;
        *out_length = 0;
        return ZX_ERR_NO_MEMORY;
    }
    memcpy(descriptors, result, length);
    *out_descriptors = descriptors;
    *out_length = length;
    return ZX_OK;
}

zx_status_t usb_interface_get_string_descriptor(void* ctx,
                                                uint8_t desc_id, uint16_t* inout_lang_id,
                                                uint8_t* buf, size_t* inout_buflen) {
    usb_interface_t* intf = ctx;
    return usb_device_get_string_descriptor(intf->device,
                                            desc_id, inout_lang_id, buf, inout_buflen);
}

static zx_status_t usb_interface_claim_device_interface(void* ctx,
                                                        usb_interface_descriptor_t* claim_intf,
                                                        size_t claim_length) {
    usb_interface_t* intf = ctx;

    zx_status_t status = usb_device_claim_interface(intf->device,
                                                    claim_intf->bInterfaceNumber);
    if (status != ZX_OK) {
        return status;
    }
    // Copy claimed interface descriptors to end of descriptor array.
    void* descriptors = realloc(intf->descriptor,
                                intf->descriptor_length + claim_length);
    if (!descriptors) {
        return ZX_ERR_NO_MEMORY;
    }
    memcpy(descriptors + intf->descriptor_length, claim_intf, claim_length);
    intf->descriptor = descriptors;
    intf->descriptor_length += claim_length;

    if (claim_intf->bInterfaceNumber > intf->last_interface_id) {
        intf->last_interface_id = claim_intf->bInterfaceNumber;
    }
    return ZX_OK;
}

static zx_status_t usb_interface_cancel_all(void* ctx, uint8_t ep_address) {
    usb_interface_t* intf = ctx;
    return usb_hci_cancel_all(&intf->hci, intf->device_id, ep_address);
}

static usb_protocol_ops_t _usb_protocol = {
    .req_alloc = usb_interface_req_alloc,
    .req_alloc_vmo = usb_interface_req_alloc_vmo,
    .req_init = usb_interface_req_init,
    .req_copy_from = usb_interface_req_copy_from,
    .req_copy_to = usb_interface_req_copy_to,
    .req_mmap = usb_interface_req_mmap,
    .req_cacheop = usb_interface_req_cacheop,
    .req_cache_flush = usb_interface_req_cache_flush,
    .req_cache_flush_invalidate = usb_interface_req_cache_flush_invalidate,
    .req_physmap = usb_interface_req_physmap,
    .req_release = usb_interface_req_release,
    .req_complete = usb_interface_req_complete,
    .req_phys_iter_init = usb_interface_req_phys_iter_init,
    .control = usb_interface_control,
    .request_queue = usb_interface_request_queue,
    .get_speed = usb_interface_get_speed,
    .set_interface = usb_interface_set_interface,
    .set_configuration = usb_interface_set_configuration,
    .reset_endpoint = usb_interface_reset_endpoint,
    .get_max_transfer_size = usb_interface_get_max_transfer_size,
    .get_device_id = _usb_interface_get_device_id,
    .get_device_descriptor = usb_interface_get_device_descriptor,
    .get_descriptor_list = usb_interface_get_descriptor_list,
    .get_additional_descriptor_list = usb_interface_get_additional_descriptor_list,
    .get_string_descriptor = usb_interface_get_string_descriptor,
    .claim_interface = usb_interface_claim_device_interface,
    .cancel_all = usb_interface_cancel_all,
};

zx_status_t usb_device_add_interface(usb_device_t* device,
                                     usb_device_descriptor_t* device_desc,
                                     usb_interface_descriptor_t* interface_desc,
                                     size_t interface_desc_length) {
    usb_interface_t* intf = calloc(1, sizeof(usb_interface_t));
    if (!intf)
        return ZX_ERR_NO_MEMORY;

    mtx_init(&intf->callback_lock, mtx_plain);
    sync_completion_reset(&intf->callback_thread_completion);
    list_initialize(&intf->completed_reqs);

    usb_request_pool_init(&intf->free_reqs);

    intf->device = device;
    intf->hci_zxdev = device->hci_zxdev;
    memcpy(&intf->hci, &device->hci, sizeof(usb_hci_protocol_t));
    intf->device_id = device->device_id;
    intf->last_interface_id = interface_desc->bInterfaceNumber;
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

    zx_status_t status = usb_interface_configure_endpoints(intf, interface_desc->bInterfaceNumber, 0);
    if (status != ZX_OK) {
        free(intf);
        return status;
    }

    mtx_lock(&device->interface_mutex);
    // need to do this first so usb_device_set_interface() can be called from driver bind
    list_add_head(&device->children, &intf->node);
    mtx_unlock(&device->interface_mutex);

    // callback thread must be started before device_add() since it will recursively
    // bind other drivers to us before it returns.
    start_callback_thread(intf);

    char name[20];
    snprintf(name, sizeof(name), "ifc-%03d", interface_desc->bInterfaceNumber);

    zx_device_prop_t props[] = {
        { BIND_PROTOCOL, 0, ZX_PROTOCOL_USB },
        { BIND_USB_VID, 0, device_desc->idVendor },
        { BIND_USB_PID, 0, device_desc->idProduct },
        { BIND_USB_CLASS, 0, usb_class },
        { BIND_USB_SUBCLASS, 0, usb_subclass },
        { BIND_USB_PROTOCOL, 0, usb_protocol },
    };

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = intf,
        .ops = &usb_interface_proto,
        .proto_id = ZX_PROTOCOL_USB,
        .proto_ops = &_usb_protocol,
        .props = props,
        .prop_count = countof(props),
    };

    status = device_add(device->zxdev, &args, &intf->zxdev);
    if (status != ZX_OK) {
        stop_callback_thread(intf);
        list_delete(&intf->node);
        free(interface_desc);
        free(intf);
    }
    return status;
}

zx_status_t usb_device_add_interface_association(usb_device_t* device,
                                                 usb_device_descriptor_t* device_desc,
                                                 usb_interface_assoc_descriptor_t* assoc_desc,
                                                 size_t assoc_desc_length) {
    usb_interface_t* intf = calloc(1, sizeof(usb_interface_t));
    if (!intf)
        return ZX_ERR_NO_MEMORY;

    mtx_init(&intf->callback_lock, mtx_plain);
    sync_completion_reset(&intf->callback_thread_completion);
    list_initialize(&intf->completed_reqs);

    intf->device = device;
    intf->hci_zxdev = device->hci_zxdev;
    memcpy(&intf->hci, &device->hci, sizeof(usb_hci_protocol_t));
    intf->device_id = device->device_id;
    // Interfaces in an IAD interface collection must be contiguous.
    intf->last_interface_id = assoc_desc->bFirstInterface + assoc_desc->bInterfaceCount - 1;
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

    usb_descriptor_header_t* header = intf->descriptor;
    usb_descriptor_header_t* end = (usb_descriptor_header_t*)((void*)header + intf->descriptor_length);
    while (header < end) {
        if (header->bDescriptorType == USB_DT_INTERFACE) {
            usb_interface_descriptor_t* intf_desc = (usb_interface_descriptor_t*)header;
            if (intf_desc->bAlternateSetting == 0) {
                zx_status_t status = usb_interface_configure_endpoints(intf, intf_desc->bInterfaceNumber, 0);
                if (status != ZX_OK) return status;
            }
        }
        header = NEXT_DESCRIPTOR(header);
    }

    // callback thread must be started before device_add() since it will recursively
    // bind other drivers to us before it returns.
    start_callback_thread(intf);

    mtx_lock(&device->interface_mutex);
    // need to do this first so usb_device_set_interface() can be called from driver bind
    list_add_head(&device->children, &intf->node);
    mtx_unlock(&device->interface_mutex);

    char name[20];
    snprintf(name, sizeof(name), "asc-%03d", assoc_desc->iFunction);

    zx_device_prop_t props[] = {
        { BIND_PROTOCOL, 0, ZX_PROTOCOL_USB },
        { BIND_USB_VID, 0, device_desc->idVendor },
        { BIND_USB_PID, 0, device_desc->idProduct },
        { BIND_USB_CLASS, 0, usb_class },
        { BIND_USB_SUBCLASS, 0, usb_subclass },
        { BIND_USB_PROTOCOL, 0, usb_protocol },
    };

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = intf,
        .ops = &usb_interface_proto,
        .proto_id = ZX_PROTOCOL_USB,
        .proto_ops = &_usb_protocol,
        .props = props,
        .prop_count = countof(props),
    };

    zx_status_t status = device_add(device->zxdev, &args, &intf->zxdev);
    if (status != ZX_OK) {
        stop_callback_thread(intf);
        list_delete(&intf->node);
        free(assoc_desc);
        free(intf);
    }
    return status;
}

bool usb_device_remove_interface_by_id_locked(usb_device_t* device, uint8_t interface_id) {
    usb_interface_t* intf;
    usb_interface_t* tmp;

    list_for_every_entry_safe(&device->children, intf, tmp, usb_interface_t, node) {
        if (usb_interface_contains_interface(intf, interface_id)) {
            list_delete(&intf->node);
            device_remove(intf->zxdev);
            return true;
        }
    }
    return false;
}

zx_status_t usb_interface_get_device_id(zx_device_t* device, uint32_t* out) {
    usb_protocol_t usb;
    if (device_get_protocol(device, ZX_PROTOCOL_USB, &usb) != ZX_OK) {
        return ZX_ERR_INTERNAL;
    }
    *out = usb_get_device_id(&usb);
    return ZX_OK;
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

zx_status_t usb_interface_set_alt_setting(usb_interface_t* intf, uint8_t interface_id,
                                          uint8_t alt_setting) {
    zx_status_t status = usb_interface_configure_endpoints(intf, interface_id, alt_setting);
    if (status != ZX_OK) return status;

    return usb_device_control(intf->device,
                              USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE,
                              USB_REQ_SET_INTERFACE, alt_setting, interface_id, NULL, 0);
}
