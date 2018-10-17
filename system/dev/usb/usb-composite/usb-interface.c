// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/protocol/usb.h>
#include <ddk/protocol/usb-composite.h>
#include <usb/usb-request.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "usb-composite.h"
#include "usb-interface.h"

static zx_status_t usb_interface_get_protocol(void* ctx, uint32_t proto_id, void* out) {
    usb_interface_t* intf = ctx;
    switch (proto_id) {
    case ZX_PROTOCOL_USB: {
        usb_protocol_t* proto = (usb_protocol_t *)out;
        proto->ctx = intf;
        proto->ops = &usb_device_protocol;
        return ZX_OK;
    }
    case ZX_PROTOCOL_USB_COMPOSITE: {
        usb_composite_protocol_t* proto = (usb_composite_protocol_t *)out;
        proto->ctx = intf;
        proto->ops = &usb_composite_device_protocol;
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void usb_interface_unbind(void* ctx) {
    usb_interface_t* intf = ctx;
    device_remove(intf->zxdev);
}

static void usb_interface_release(void* ctx) {
    usb_interface_t* intf = ctx;

    free(intf->descriptor);
    free(intf);
}

zx_protocol_device_t usb_interface_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = usb_interface_get_protocol,
    .unbind = usb_interface_unbind,
    .release = usb_interface_release,
};

#define NEXT_DESCRIPTOR(header) ((usb_descriptor_header_t*)((void*)header + header->bLength))

// for determining index into active_endpoints[]
// bEndpointAddress has 4 lower order bits, plus high bit to signify direction
// shift high bit to bit 4 so index is in range 0 - 31.
#define get_usb_endpoint_index(ep) (((ep)->bEndpointAddress & 0x0F) | ((ep)->bEndpointAddress >> 3))

zx_status_t usb_interface_configure_endpoints(usb_interface_t* intf, uint8_t interface_id,
                                              uint8_t alt_setting) {
    usb_endpoint_descriptor_t* new_endpoints[USB_MAX_EPS] = {};
    bool interface_endpoints[USB_MAX_EPS] = {};
    zx_status_t status = ZX_OK;

    // iterate through our descriptors to find which endpoints should be active
    usb_descriptor_header_t* header = intf->descriptor;
    usb_descriptor_header_t* end = (usb_descriptor_header_t*)((void*)header +
                                                              intf->descriptor_length);
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
                    zx_status_t ret = usb_enable_endpoint(&intf->comp->usb, old_ep, NULL, false);
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
                    zx_status_t ret = usb_enable_endpoint(&intf->comp->usb, new_ep, ss_comp_desc,
                                                          true);
                    if (ret != ZX_OK) {
                        status = ret;
                    }
                }
                intf->active_endpoints[i] = new_ep;
            }
        }
    }
    return status;
}

static zx_status_t usb_interface_control(void* ctx, uint8_t request_type, uint8_t request,
                                         uint16_t value, uint16_t index, void* data,
                                         size_t length, zx_time_t timeout, size_t* out_length) {
    usb_interface_t* intf = ctx;
    return usb_control(&intf->comp->usb, request_type, request, value, index, data, length,
                              timeout, out_length);
}

static void usb_interface_request_queue(void* ctx, usb_request_t* usb_request) {
    usb_interface_t* intf = ctx;
    usb_request_queue(&intf->comp->usb, usb_request);
}

static usb_speed_t usb_interface_get_speed(void* ctx) {
    usb_interface_t* intf = ctx;
    return usb_get_speed(&intf->comp->usb);
}

static zx_status_t usb_interface_set_interface(void* ctx, uint8_t interface_number,
                                               uint8_t alt_setting) {
    usb_interface_t* intf = ctx;
    return usb_composite_set_interface(intf->comp, interface_number, alt_setting);
}

static uint8_t usb_interface_get_configuration(void* ctx) {
    usb_interface_t* intf = ctx;
    return usb_get_configuration(&intf->comp->usb);
}

static zx_status_t usb_interface_set_configuration(void* ctx, uint8_t configuration) {
    usb_interface_t* intf = ctx;
    return usb_set_configuration(&intf->comp->usb, configuration);
}

static zx_status_t usb_interface_enable_endpoint(void* ctx, usb_endpoint_descriptor_t* ep_desc,
                                                 usb_ss_ep_comp_descriptor_t* ss_comp_desc,
                                                 bool enable) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t usb_interface_reset_endpoint(void* ctx, uint8_t ep_address) {
    usb_interface_t* intf = ctx;
    return usb_reset_endpoint(&intf->comp->usb, ep_address);
}

static size_t usb_interface_get_max_transfer_size(void* ctx, uint8_t ep_address) {
    usb_interface_t* intf = ctx;
    return usb_get_max_transfer_size(&intf->comp->usb, ep_address);
}

static uint32_t usb_interface_get_device_id(void* ctx) {
    usb_interface_t* intf = ctx;
    return usb_get_device_id(&intf->comp->usb);
}

static void usb_interface_get_device_descriptor(void* ctx, usb_device_descriptor_t* out_desc) {
    usb_interface_t* intf = ctx;
    return usb_get_device_descriptor(&intf->comp->usb, out_desc);
}

static zx_status_t usb_interface_get_configuration_descriptor(void* ctx, uint8_t configuration,
                                                              usb_configuration_descriptor_t** out,
                                                              size_t* out_length) {
    usb_interface_t* intf = ctx;
    return usb_get_configuration_descriptor(&intf->comp->usb, configuration, out, out_length);
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

    usb_composite_t* comp = intf->comp;
    usb_configuration_descriptor_t* config = comp->config_desc;
    usb_descriptor_header_t* header = NEXT_DESCRIPTOR(config);
    usb_descriptor_header_t* end = (usb_descriptor_header_t*)((void*)config +
                                                              le16toh(config->wTotalLength));

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

zx_status_t usb_interface_get_string_descriptor(void* ctx, uint8_t desc_id, uint16_t lang_id,
                                                uint8_t* buf, size_t buflen, size_t* out_actual,
                                                uint16_t* out_actual_lang_id) {
    usb_interface_t* intf = ctx;
    return usb_get_string_descriptor(&intf->comp->usb, desc_id, lang_id, buf, buflen, out_actual,
                                     out_actual_lang_id);
}

static zx_status_t usb_interface_claim_device_interface(void* ctx,
                                                        usb_interface_descriptor_t* claim_intf,
                                                        size_t claim_length) {
    usb_interface_t* intf = ctx;

    zx_status_t status = usb_composite_do_claim_interface(intf->comp, claim_intf->bInterfaceNumber);
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
    return usb_cancel_all(&intf->comp->usb, ep_address);
}

static uint64_t usb_interface_get_current_frame(void* ctx) {
    usb_interface_t* intf = ctx;
    return usb_get_current_frame(&intf->comp->usb);
}

static size_t usb_interface_get_request_size(void* ctx) {
    usb_interface_t* intf = ctx;
    return usb_get_request_size(&intf->comp->usb);
}

usb_protocol_ops_t usb_device_protocol = {
    .control = usb_interface_control,
    .request_queue = usb_interface_request_queue,
    .get_speed = usb_interface_get_speed,
    .set_interface = usb_interface_set_interface,
    .get_configuration = usb_interface_get_configuration,
    .set_configuration = usb_interface_set_configuration,
    .enable_endpoint = usb_interface_enable_endpoint,
    .reset_endpoint = usb_interface_reset_endpoint,
    .get_max_transfer_size = usb_interface_get_max_transfer_size,
    .get_device_id = usb_interface_get_device_id,
    .get_device_descriptor = usb_interface_get_device_descriptor,
    .get_configuration_descriptor = usb_interface_get_configuration_descriptor,
    .get_descriptor_list = usb_interface_get_descriptor_list,
    .get_string_descriptor = usb_interface_get_string_descriptor,
    .cancel_all = usb_interface_cancel_all,
    .get_current_frame = usb_interface_get_current_frame,
    .get_request_size = usb_interface_get_request_size,
};

usb_composite_protocol_ops_t usb_composite_device_protocol = {
    .get_additional_descriptor_list = usb_interface_get_additional_descriptor_list,
    .claim_interface = usb_interface_claim_device_interface,
};

bool usb_interface_contains_interface(usb_interface_t* intf, uint8_t interface_id) {
    usb_descriptor_header_t* header = intf->descriptor;
    usb_descriptor_header_t* end = (usb_descriptor_header_t*)((void*)header +
                                                              intf->descriptor_length);

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
    if (status != ZX_OK) {
        return status;
    }

    return usb_control(&intf->comp->usb, USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE,
                       USB_REQ_SET_INTERFACE, alt_setting, interface_id, NULL, 0, ZX_TIME_INFINITE,
                       NULL);
}
