// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/usb/usb.h>
#include <zircon/usb/device/c/fidl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "usb-bus.h"
#include "usb-device.h"
#include "util.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

static usb_protocol_ops_t _usb_protocol;

static zx_status_t usb_device_set_interface(void* ctx, uint8_t interface_number,
                                            uint8_t alt_setting);
static zx_status_t usb_device_set_configuration(void* ctx, uint8_t config);

// By default we create devices for the interfaces on the first configuration.
// This table allows us to specify a different configuration for certain devices
// based on their VID and PID.
//
// TODO(voydanoff) Find a better way of handling this. For example, we could query to see
// if any interfaces on the first configuration have drivers that can bind to them.
// If not, then we could try the other configurations automatically instead of having
// this hard coded list of VID/PID pairs
typedef struct {
    uint16_t vid;
    uint16_t pid;
    uint8_t configuration;
} usb_config_override_t;

static const usb_config_override_t config_overrides[] = {
    { 0x0bda, 0x8153, 2 },  // Realtek ethernet dongle has CDC interface on configuration 2
    { 0, 0, 0 },
};

// This thread is for calling the usb request completion callback for requests received from our
// client. We do this on a separate thread because it is unsafe to call out on our own completion
// callback, which is called on the main thread of the USB HCI driver.
static int callback_thread(void* arg) {
    usb_device_t* dev = (usb_device_t *)arg;
    bool done = false;

    while (!done) {
        // wait for new usb requests to complete or for signal to exit this thread
        sync_completion_wait(&dev->callback_thread_completion, ZX_TIME_INFINITE);

        mtx_lock(&dev->callback_lock);

        sync_completion_reset(&dev->callback_thread_completion);
        done = dev->callback_thread_stop;

        // copy completed requests to a temp list so we can process them outside of our lock
        list_node_t temp_list = LIST_INITIAL_VALUE(temp_list);
        list_move(&dev->completed_reqs, &temp_list);

        mtx_unlock(&dev->callback_lock);

        // call completion callbacks outside of the lock
        usb_request_t* req;
        usb_device_req_internal_t* req_int;
        while ((req_int = list_remove_head_type(&temp_list, usb_device_req_internal_t, node))) {
            req = DEV_INTERNAL_TO_USB_REQ(req_int, dev->parent_req_size);
            usb_request_complete(req, req->response.status, req->response.actual, req->complete_cb,
                                 req->cookie);
        }
    }

    return 0;
}

static void start_callback_thread(usb_device_t* dev) {
    // TODO(voydanoff) Once we have a way of knowing when a driver has bound to us, move the thread
    // start there so we don't have to start a thread unless we know we will need it.
    thrd_create_with_name(&dev->callback_thread, callback_thread, dev, "usb-device-callback-thread");
}

static void stop_callback_thread(usb_device_t* dev) {
    mtx_lock(&dev->callback_lock);
    dev->callback_thread_stop = true;
    mtx_unlock(&dev->callback_lock);

    sync_completion_signal(&dev->callback_thread_completion);
    thrd_join(dev->callback_thread, NULL);
}

// usb request completion for the requests passed down to the HCI driver
static void request_complete(usb_request_t* req, void* cookie) {
    usb_device_t* dev = cookie;

    mtx_lock(&dev->callback_lock);
    // move original request to completed_reqs list so it can be completed on the callback_thread
    req->complete_cb = req->saved_complete_cb;
    req->cookie = req->saved_cookie;

    usb_device_req_internal_t* req_int = USB_REQ_TO_DEV_INTERNAL(req, dev->parent_req_size);
    list_add_tail(&dev->completed_reqs, &req_int->node);
    mtx_unlock(&dev->callback_lock);
    sync_completion_signal(&dev->callback_thread_completion);
}

void usb_device_set_hub_interface(usb_device_t* device, usb_hub_interface_t* hub_intf) {
    if (hub_intf) {
        memcpy(&device->hub_intf, hub_intf, sizeof(device->hub_intf));
    } else {
        memset(&device->hub_intf, 0, sizeof(device->hub_intf));
    }
}

static usb_configuration_descriptor_t* get_config_desc(usb_device_t* dev, int config) {
    for (int i = 0; i <  dev->num_configurations; i++) {
        usb_configuration_descriptor_t* desc = dev->config_descs[i];
        if (desc->bConfigurationValue == config) {
            return desc;
        }
    }
    return NULL;
}

static zx_status_t usb_device_get_protocol(void* ctx, uint32_t proto_id, void* protocol) {
    if (proto_id == ZX_PROTOCOL_USB) {
        usb_protocol_t* usb_proto = protocol;
        usb_proto->ctx = ctx;
        usb_proto->ops = &_usb_protocol;
        return ZX_OK;
    } else {
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void usb_device_unbind(void* ctx) {
    usb_device_t* dev = ctx;
    device_remove(dev->zxdev);
}

static void usb_device_release(void* ctx) {
    usb_device_t* dev = ctx;

    stop_callback_thread(dev);

    if (dev->config_descs) {
        for (int i = 0; i <  dev->num_configurations; i++) {
            if (dev->config_descs[i]) free(dev->config_descs[i]);
        }
        free(dev->config_descs);
    }
    free((void*)dev->lang_ids);
    free(dev);
}

static void usb_control_complete(usb_request_t* req, void* cookie) {
    sync_completion_signal((sync_completion_t*)cookie);
}

static zx_status_t usb_device_control(void* ctx, uint8_t request_type, uint8_t request,
                                      uint16_t value, uint16_t index, void* data, size_t length,
                                      zx_time_t timeout, size_t* out_length) {
    usb_device_t* dev = ctx;

    usb_request_t* req = NULL;
    bool use_free_list = length == 0;
    if (use_free_list) {
        req = usb_request_pool_get(&dev->free_reqs, length);
    }

    if (req == NULL) {
        zx_status_t status = usb_request_alloc(&req, length, 0, dev->req_size);
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
        usb_request_copy_to(req, data, length, 0);
    }

    sync_completion_t completion = SYNC_COMPLETION_INIT;

    req->header.device_id = dev->device_id;
    req->header.length = length;
    req->complete_cb = usb_control_complete;
    req->cookie = &completion;
    // We call this directly instead of via hci_queue, as it's safe to call our
    // own completion callback, and prevents clients getting into odd deadlocks.
    usb_hci_request_queue(&dev->hci, req, usb_control_complete, &completion);
    zx_status_t status = sync_completion_wait(&completion, timeout);

    if (status == ZX_OK) {
        status = req->response.status;
    } else if (status == ZX_ERR_TIMED_OUT) {
        // cancel transactions and wait for request to be completed
        sync_completion_reset(&completion);
        status = usb_hci_cancel_all(&dev->hci, dev->device_id, 0);
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
            usb_request_copy_from(req, data, req->response.actual, 0);
        }
    }

    if (use_free_list) {
        if (usb_request_pool_add(&dev->free_reqs, req) != ZX_OK) {
            zxlogf(TRACE, "Unable to add back request to the free pool\n");
            usb_request_release(req);
        }
    } else {
        usb_request_release(req);
    }
    return status;
}

static void usb_device_request_queue(void* ctx, usb_request_t* req) {
    usb_device_t* dev = ctx;

    req->header.device_id = dev->device_id;
    // save the existing callback and cookie, so we can replace them
    // with our own before passing the request to the HCI driver.
    req->saved_complete_cb = req->complete_cb;
    req->saved_cookie = req->cookie;

    req->complete_cb = request_complete;
    // set device as the cookie so we can get at it in request_complete()
    req->cookie = dev;

    usb_hci_request_queue(&dev->hci, req, request_complete, dev);
}


static zx_status_t usb_device_configure_batch_callback(void* ctx, uint8_t ep_address,
                                                       usb_batch_complete_cb cb, void* cookie) {
    // TODO(jocelyndang): implement this.
    return ZX_ERR_NOT_SUPPORTED;
}

static usb_speed_t usb_device_get_speed(void* ctx) {
    usb_device_t* dev = ctx;
    return dev->speed;
}

static zx_status_t usb_device_set_interface(void* ctx, uint8_t interface_number,
                                            uint8_t alt_setting) {
    usb_device_t* dev = ctx;
    return usb_util_control(dev, USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE,
                            USB_REQ_SET_INTERFACE, alt_setting, interface_number, NULL, 0);
}

static uint8_t usb_device_get_configuration(void* ctx) {
    usb_device_t* dev = ctx;
    return dev->config_descs[dev->current_config_index]->bConfigurationValue;
}

static zx_status_t usb_device_set_configuration(void* ctx, uint8_t configuration) {
    usb_device_t* dev = ctx;
    for (uint8_t i = 0; i < dev->num_configurations; i++) {
        usb_configuration_descriptor_t* descriptor = dev->config_descs[i];
        if (descriptor->bConfigurationValue == configuration) {
            zx_status_t status;
            status = usb_util_control(dev, USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                                      USB_REQ_SET_CONFIGURATION, configuration, 0, NULL, 0);
            if (status == ZX_OK) {
                dev->current_config_index = i;
            }
            return status;
        }
    }
    return ZX_ERR_INVALID_ARGS;
}

static zx_status_t usb_device_enable_endpoint(void* ctx, usb_endpoint_descriptor_t* ep_desc,
                                              usb_ss_ep_comp_descriptor_t* ss_comp_desc,
                                              bool enable) {
    usb_device_t* dev = ctx;
    return usb_hci_enable_endpoint(&dev->hci, dev->device_id, ep_desc, ss_comp_desc, enable);
}

static zx_status_t usb_device_reset_endpoint(void* ctx, uint8_t ep_address) {
    usb_device_t* dev = ctx;
    return usb_hci_reset_endpoint(&dev->hci, dev->device_id, ep_address);
}

static size_t usb_device_get_max_transfer_size(void* ctx, uint8_t ep_address) {
    usb_device_t* dev = ctx;
    return usb_hci_get_max_transfer_size(&dev->hci, dev->device_id, ep_address);
}

static uint32_t _usb_device_get_device_id(void* ctx) {
    usb_device_t* dev = ctx;
    return dev->device_id;
}

static void usb_device_get_device_descriptor(void* ctx, usb_device_descriptor_t* out_desc) {
    usb_device_t* dev = ctx;
    memcpy(out_desc, &dev->device_desc, sizeof(usb_device_descriptor_t));
}

static zx_status_t usb_device_get_configuration_descriptor(void* ctx, uint8_t configuration,
                                                           usb_configuration_descriptor_t** out,
                                                           size_t* out_length) {
    usb_device_t* dev = ctx;
    for (int i = 0; i <  dev->num_configurations; i++) {
        usb_configuration_descriptor_t* config_desc = dev->config_descs[i];
        if (config_desc->bConfigurationValue == configuration) {
            size_t length = le16toh(config_desc->wTotalLength);
            usb_configuration_descriptor_t* descriptor = malloc(length);
            if (!descriptor) {
                *out = NULL;
                *out_length = 0;
                return ZX_ERR_NO_MEMORY;
            }
            memcpy(descriptor, config_desc, length);
            *out = descriptor;
            *out_length = length;
            return ZX_OK;
        }
    }
    return ZX_ERR_INVALID_ARGS;
}

static zx_status_t usb_device_get_descriptor_list(void* ctx, void** out_descriptors,
                                                  size_t* out_length) {
    usb_device_t* dev = ctx;
    usb_configuration_descriptor_t* config_desc = dev->config_descs[dev->current_config_index];
    size_t length = le16toh(config_desc->wTotalLength);

    void* descriptors = malloc(length);
    if (!descriptors) {
        *out_descriptors = NULL;
        *out_length = 0;
        return ZX_ERR_NO_MEMORY;
    }
    memcpy(descriptors, config_desc, length);
    *out_descriptors = descriptors;
    *out_length = length;
    return ZX_OK;
}

zx_status_t usb_device_get_string_descriptor(void* ctx, uint8_t desc_id, uint16_t lang_id,
                                             uint8_t* buf, size_t buflen, size_t* out_actual,
                                             uint16_t* out_actual_lang_id) {
    usb_device_t* dev = ctx;
    return usb_util_get_string_descriptor(dev, desc_id, lang_id, buf, buflen,
                                          out_actual, out_actual_lang_id);
}

static zx_status_t usb_device_cancel_all(void* ctx, uint8_t ep_address) {
    usb_device_t* dev = ctx;
    return usb_hci_cancel_all(&dev->hci, dev->device_id, ep_address);
}

static uint64_t usb_device_get_current_frame(void* ctx) {
    usb_device_t* dev = ctx;
    return usb_hci_get_current_frame(&dev->hci);
}

static size_t usb_device_get_request_size(void* ctx) {
    usb_device_t* dev = ctx;
    return dev->req_size;
}

static usb_protocol_ops_t _usb_protocol = {
    .control = usb_device_control,
    .request_queue = usb_device_request_queue,
    .configure_batch_callback = usb_device_configure_batch_callback,
    .get_speed = usb_device_get_speed,
    .set_interface = usb_device_set_interface,
    .get_configuration = usb_device_get_configuration,
    .set_configuration = usb_device_set_configuration,
    .enable_endpoint = usb_device_enable_endpoint,
    .reset_endpoint = usb_device_reset_endpoint,
    .get_max_transfer_size = usb_device_get_max_transfer_size,
    .get_device_id = _usb_device_get_device_id,
    .get_device_descriptor = usb_device_get_device_descriptor,
    .get_configuration_descriptor = usb_device_get_configuration_descriptor,
    .get_descriptor_list = usb_device_get_descriptor_list,
    .get_string_descriptor = usb_device_get_string_descriptor,
    .cancel_all = usb_device_cancel_all,
    .get_current_frame = usb_device_get_current_frame,
    .get_request_size = usb_device_get_request_size,
};

static zx_status_t fidl_GetDeviceSpeed(void* ctx, fidl_txn_t* txn) {
    usb_device_t* dev = ctx;
    return zircon_usb_device_DeviceGetDeviceSpeed_reply(txn, dev->speed);
}

static zx_status_t fidl_GetDeviceDescriptor(void* ctx, fidl_txn_t* txn) {
    usb_device_t* dev = ctx;
    return zircon_usb_device_DeviceGetDeviceDescriptor_reply(txn, (uint8_t*)&dev->device_desc);
}

static zx_status_t fidl_GetConfigurationDescriptorSize(void* ctx, uint8_t config, fidl_txn_t* txn) {
    usb_device_t* dev = ctx;
    usb_configuration_descriptor_t* descriptor = get_config_desc(dev, config);
    if (!descriptor) {
        return zircon_usb_device_DeviceGetConfigurationDescriptorSize_reply(txn,
                                                                            ZX_ERR_INVALID_ARGS, 0);
    }

    size_t length = le16toh(descriptor->wTotalLength);
    return zircon_usb_device_DeviceGetConfigurationDescriptorSize_reply(txn, ZX_OK, length);
}

static zx_status_t fidl_GetConfigurationDescriptor(void* ctx, uint8_t config, fidl_txn_t* txn) {
    usb_device_t* dev = ctx;
    usb_configuration_descriptor_t* descriptor = get_config_desc(dev, config);
    if (!descriptor) {
        return zircon_usb_device_DeviceGetConfigurationDescriptor_reply(txn, ZX_ERR_INVALID_ARGS,
                                                                        NULL, 0);
    }

    size_t length = le16toh(descriptor->wTotalLength);
    return zircon_usb_device_DeviceGetConfigurationDescriptor_reply(txn, ZX_OK,
                                                                    (uint8_t*)descriptor, length);
}

static zx_status_t fidl_GetStringDescriptor(void* ctx, uint8_t desc_id, uint16_t lang_id,
                                            fidl_txn_t* txn) {
    usb_device_t* dev = ctx;
    uint8_t buffer[zircon_usb_device_MAX_STRING_DESC_SIZE];
    size_t actual;
    zx_status_t status = usb_util_get_string_descriptor(dev, desc_id, lang_id, buffer,
                                                        sizeof(buffer), &actual, &lang_id);
    return zircon_usb_device_DeviceGetStringDescriptor_reply(txn, status, buffer, actual, lang_id);
}

static zx_status_t fidl_SetInterface(void* ctx, uint8_t interface_number, uint8_t alt_setting,
                                     fidl_txn_t* txn) {
    usb_device_t* dev = ctx;
    zx_status_t status = usb_device_set_interface(dev, interface_number, alt_setting);
    return zircon_usb_device_DeviceSetInterface_reply(txn, status);
}

static zx_status_t fidl_GetDeviceId(void* ctx, fidl_txn_t* txn) {
    usb_device_t* dev = ctx;
    return zircon_usb_device_DeviceGetDeviceId_reply(txn, dev->device_id);
}

static zx_status_t fidl_GetHubDeviceId(void* ctx, fidl_txn_t* txn) {
    usb_device_t* dev = ctx;
    return zircon_usb_device_DeviceGetHubDeviceId_reply(txn, dev->hub_id);
}

static zx_status_t fidl_GetConfiguration(void* ctx, fidl_txn_t* txn) {
    usb_device_t* dev = ctx;
    usb_configuration_descriptor_t* descriptor = dev->config_descs[dev->current_config_index];
    return zircon_usb_device_DeviceGetConfiguration_reply(txn, descriptor->bConfigurationValue);
}

static zx_status_t fidl_SetConfiguration(void* ctx, uint8_t configuration, fidl_txn_t* txn) {
    usb_device_t* dev = ctx;
    zx_status_t status = usb_device_set_configuration(dev, configuration);
    return zircon_usb_device_DeviceSetConfiguration_reply(txn, status);
}

static zircon_usb_device_Device_ops_t fidl_ops = {
    .GetDeviceSpeed = fidl_GetDeviceSpeed,
    .GetDeviceDescriptor = fidl_GetDeviceDescriptor,
    .GetConfigurationDescriptorSize = fidl_GetConfigurationDescriptorSize,
    .GetConfigurationDescriptor = fidl_GetConfigurationDescriptor,
    .GetStringDescriptor = fidl_GetStringDescriptor,
    .SetInterface = fidl_SetInterface,
    .GetDeviceId = fidl_GetDeviceId,
    .GetHubDeviceId = fidl_GetHubDeviceId,
    .GetConfiguration = fidl_GetConfiguration,
    .SetConfiguration = fidl_SetConfiguration,
};

zx_status_t usb_device_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
    return zircon_usb_device_Device_dispatch(ctx, txn, msg, &fidl_ops);
}

static zx_protocol_device_t usb_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = usb_device_get_protocol,
    .message = usb_device_message,
    .release = usb_device_release,
};

zx_status_t usb_device_add(usb_bus_t* bus, uint32_t device_id, uint32_t hub_id,
                           usb_speed_t speed, usb_device_t** out_device) {

    usb_device_t* dev = calloc(1, sizeof(usb_device_t));
    if (!dev) {
        return ZX_ERR_NO_MEMORY;
    }

    // Needed for usb_util_control requests.
    memcpy(&dev->hci, &bus->hci, sizeof(usb_hci_protocol_t));
    dev->parent_req_size = usb_hci_get_request_size(&dev->hci);
    dev->req_size = dev->parent_req_size + sizeof(usb_device_req_internal_t);

    dev->bus = bus;
    dev->device_id = device_id;
    mtx_init(&dev->callback_lock, mtx_plain);
    sync_completion_reset(&dev->callback_thread_completion);
    list_initialize(&dev->completed_reqs);
    usb_request_pool_init(&dev->free_reqs, dev->parent_req_size +
                          offsetof(usb_device_req_internal_t, node));

    // read device descriptor
    usb_device_descriptor_t* device_desc = &dev->device_desc;
    zx_status_t status = usb_util_get_descriptor(dev, USB_DT_DEVICE, 0, 0, device_desc,
                                                 sizeof(*device_desc));
    if (status != sizeof(*device_desc)) {
        zxlogf(ERROR, "usb_device_add: usb_util_get_descriptor failed\n");
        free(dev);
        return status;
    }

    uint8_t num_configurations = device_desc->bNumConfigurations;
    usb_configuration_descriptor_t** configs = calloc(num_configurations,
                                                      sizeof(usb_configuration_descriptor_t*));
    if (!configs) {
        status = ZX_ERR_NO_MEMORY;
        goto error_exit;
    }

    for (int config = 0; config < num_configurations; config++) {
        // read configuration descriptor header to determine size
        usb_configuration_descriptor_t config_desc_header;
        status = usb_util_get_descriptor(dev, USB_DT_CONFIG, config, 0, &config_desc_header,
                                         sizeof(config_desc_header));
        if (status != sizeof(config_desc_header)) {
            zxlogf(ERROR, "usb_device_add: usb_util_get_descriptor failed\n");
            goto error_exit;
        }
        uint16_t config_desc_size = letoh16(config_desc_header.wTotalLength);
        usb_configuration_descriptor_t* config_desc = malloc(config_desc_size);
        if (!config_desc) {
            status = ZX_ERR_NO_MEMORY;
            goto error_exit;
        }
        configs[config] = config_desc;

        // read full configuration descriptor
        status = usb_util_get_descriptor(dev, USB_DT_CONFIG, config, 0, config_desc,
                                         config_desc_size);
         if (status != config_desc_size) {
            zxlogf(ERROR, "usb_device_add: usb_util_get_descriptor failed\n");
            goto error_exit;
        }
    }

    // we will create devices for interfaces on the first configuration by default
    uint8_t configuration = 1;
    const usb_config_override_t* override = config_overrides;
    while (override->configuration) {
        if (override->vid == le16toh(device_desc->idVendor) &&
            override->pid == le16toh(device_desc->idProduct)) {
            configuration = override->configuration;
            break;
        }
        override++;
    }
    if (configuration > num_configurations) {
        zxlogf(ERROR, "usb_device_add: override configuration number out of range\n");
        return ZX_ERR_INTERNAL;
    }
    dev->current_config_index = configuration - 1;
    dev->num_configurations = num_configurations;

    // set configuration
    status = usb_util_control(dev, USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                              USB_REQ_SET_CONFIGURATION,
                              configs[dev->current_config_index]->bConfigurationValue, 0, NULL, 0);
    if (status < 0) {
        zxlogf(ERROR, "usb_device_set_configuration: USB_REQ_SET_CONFIGURATION failed\n");
        goto error_exit;
    }

    zxlogf(INFO, "* found USB device (0x%04x:0x%04x, USB %x.%x) config %u\n",
            device_desc->idVendor, device_desc->idProduct, device_desc->bcdUSB >> 8,
            device_desc->bcdUSB & 0xff, configuration);

    dev->hci_zxdev = bus->hci_zxdev;
    dev->hub_id = hub_id;
    dev->speed = speed;
    dev->config_descs = configs;

    // callback thread must be started before device_add() since it will recursively
    // bind other drivers to us before it returns.
    start_callback_thread(dev);

    char name[16];
    snprintf(name, sizeof(name), "%03d", device_id);

    zx_device_prop_t props[] = {
        { BIND_USB_VID, 0, device_desc->idVendor },
        { BIND_USB_PID, 0, device_desc->idProduct },
        { BIND_USB_CLASS, 0, device_desc->bDeviceClass },
        { BIND_USB_SUBCLASS, 0, device_desc->bDeviceSubClass },
        { BIND_USB_PROTOCOL, 0, device_desc->bDeviceProtocol },
    };

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = dev,
        .ops = &usb_device_proto,
        .proto_id = ZX_PROTOCOL_USB_DEVICE,
        .proto_ops = &_usb_protocol,
        .props = props,
        .prop_count = countof(props),
    };

    status = device_add(bus->zxdev, &args, &dev->zxdev);
    if (status == ZX_OK) {
        *out_device = dev;
        zx_handle_t bti_handle = bus->bti_handle;
        status = device_add_metadata(dev->zxdev, DEVICE_METADATA_PRIVATE, &bti_handle,
                                     sizeof(bti_handle));
        if (status != ZX_OK) {
            zxlogf(ERROR, "usb_device_add: device_add_metadata failed: %d\n", status);
            return status;
        }
        return ZX_OK;
    } else {
        stop_callback_thread(dev);
        // fall through
    }

error_exit:
    if (configs) {
        for (int i = 0; i < num_configurations; i++) {
            if (configs[i]) free(configs[i]);
        }
        free(configs);
    }
    free(dev);
    return status;
}
