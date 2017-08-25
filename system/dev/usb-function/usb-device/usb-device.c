// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb-dci.h>
#include <ddk/protocol/usb-function.h>
#include <magenta/listnode.h>
#include <magenta/device/usb-device.h>
#include <magenta/hw/usb-cdc.h>
#include <magenta/hw/usb.h>

#define MAX_INTERFACES 32

typedef struct {
    mx_device_t* mxdev;
    mx_device_t* dci_dev;
    struct usb_device* dev;
    list_node_t node;
    usb_function_interface_t interface;
    usb_function_descriptor_t desc;
    usb_descriptor_header_t* descriptors;
    size_t descriptors_length;
    unsigned num_interfaces;
} usb_function_t;

typedef struct usb_device {
    mx_device_t* mxdev;
    mx_device_t* dci_dev;
    usb_dci_protocol_t usb_dci;
    usb_device_descriptor_t device_desc;
    usb_configuration_descriptor_t* config_desc;
    usb_function_t* interface_map[MAX_INTERFACES];
    usb_function_t* endpoint_map[USB_MAX_EPS];
    char* strings[256];
    list_node_t functions;
    mtx_t lock;
    bool functions_bound;
    bool connected;
    uint8_t configuration;
    usb_speed_t speed;
} usb_device_t;

// for mapping bEndpointAddress value to/from index in range 0 - 31
// OUT endpoints are in range 1 - 15, IN endpoints are in range 17 - 31
#define ep_address_to_index(addr) (((addr) & 0xF) | (((addr) & 0x80) >> 3))
#define ep_index_to_address(index) (((index) & 0xF) | (((index) & 0x10) << 3))
#define OUT_EP_START    1
#define OUT_EP_END      15
#define IN_EP_START     17
#define IN_EP_END       31

static mx_status_t usb_device_alloc_string_desc(usb_device_t* dev, const char* string,
                                                uint8_t* out_index) {
    unsigned i;
    for (i = 1; i < countof(dev->strings); i++) {
        if (!dev->strings[i]) {
            break;
        }
    }
    if (i == countof(dev->strings)) {
        return MX_ERR_NO_RESOURCES;
    }

    dev->strings[i] = strdup(string);
    if (!dev->strings[i]) {
        return MX_ERR_NO_MEMORY;
    }
    *out_index = i;
    return MX_OK;
}

static void usb_function_iotxn_queue(void* ctx, iotxn_t* txn) {
    usb_function_t* function = ctx;
    // pass down to the DCI driver
    iotxn_queue(function->dci_dev, txn);
}

static void usb_function_release(void* ctx) {
    dprintf(TRACE, "usb_function_release\n");
    usb_function_t* function = ctx;
    free(function->descriptors);
    free(function);
}

static mx_protocol_device_t function_proto = {
    .version = DEVICE_OPS_VERSION,
    .iotxn_queue = usb_function_iotxn_queue,
    .release = usb_function_release,
};

static mx_status_t usb_device_function_registered(usb_device_t* dev) {
    mtx_lock(&dev->lock);

    if (dev->config_desc) {
        dprintf(ERROR, "usb_device_function_registered: already have configuration descriptor!\n");
        mtx_unlock(&dev->lock);
        return MX_ERR_BAD_STATE;
    }

    // check to see if we have all our functions registered
    // if so, we can build our configuration descriptor and tell the DCI driver we are ready
    usb_function_t* function;
    size_t length = sizeof(usb_configuration_descriptor_t);
    list_for_every_entry(&dev->functions, function, usb_function_t, node) {
        if (function->descriptors) {
            length += function->descriptors_length;
        } else {
            // need to wait for more functions to register
            mtx_unlock(&dev->lock);
            return MX_OK;
        }
    }

    // build our configuration descriptor
    usb_configuration_descriptor_t* config_desc = malloc(length);
    if (!config_desc) {
        mtx_unlock(&dev->lock);
        return MX_ERR_NO_MEMORY;
    }

    config_desc->bLength = sizeof(*config_desc);
    config_desc->bDescriptorType = USB_DT_CONFIG;
    config_desc->wTotalLength = htole16(length);
    config_desc->bNumInterfaces = 0;
    config_desc->bConfigurationValue = 1;
    config_desc->iConfiguration = 0;
    // TODO(voydanoff) add a way to configure bmAttributes and bMaxPower
    config_desc->bmAttributes = USB_CONFIGURATION_SELF_POWERED | USB_CONFIGURATION_RESERVED_7;
    config_desc->bMaxPower = 0;

    void* dest = config_desc + 1;
    list_for_every_entry(&dev->functions, function, usb_function_t, node) {
        memcpy(dest, function->descriptors, function->descriptors_length);
        dest += function->descriptors_length;
        config_desc->bNumInterfaces += function->num_interfaces;
    }
    dev->config_desc = config_desc;

    mtx_unlock(&dev->lock);

// TODO - clean up if this fails?
    return usb_dci_set_enabled(&dev->usb_dci, true);
}

static mx_status_t usb_func_register(void* ctx, usb_function_interface_t* interface) {
    usb_function_t* function = ctx;
    usb_device_t* dev = function->dev;
    usb_function_t** endpoint_map = dev->endpoint_map;

    size_t length;
    const usb_descriptor_header_t* descriptors = usb_function_get_descriptors(interface, &length);

    // validate the descriptor list
    if (!descriptors || length < sizeof(usb_interface_descriptor_t)) {
        return MX_ERR_INVALID_ARGS;
    }

    usb_interface_descriptor_t* intf_desc = (usb_interface_descriptor_t *)descriptors;
    if (intf_desc->bDescriptorType != USB_DT_INTERFACE ||
            intf_desc->bLength != sizeof(usb_interface_descriptor_t)) {
        dprintf(ERROR, "usb_func_register: first descriptor not an interface descriptor\n");
        return MX_ERR_INVALID_ARGS;
    }

    const usb_descriptor_header_t* end = (void *)descriptors + length;
    const usb_descriptor_header_t* header = descriptors;

    while (header < end) {
        if (header->bDescriptorType == USB_DT_INTERFACE) {
            usb_interface_descriptor_t* desc = (usb_interface_descriptor_t *)header;
            if (desc->bInterfaceNumber >= countof(dev->interface_map) ||
                dev->interface_map[desc->bInterfaceNumber] != function) {
                dprintf(ERROR, "usb_func_register: bInterfaceNumber %u\n",
                       desc->bInterfaceNumber);
                return MX_ERR_INVALID_ARGS;
            }
            if (desc->bAlternateSetting == 0) {
                function->num_interfaces++;
            }
        } else if (header->bDescriptorType == USB_DT_ENDPOINT) {
            usb_endpoint_descriptor_t* desc = (usb_endpoint_descriptor_t *)header;
            unsigned index = ep_address_to_index(desc->bEndpointAddress);
            if (index == 0 || index >= countof(dev->endpoint_map) ||
                endpoint_map[index] != function) {
                dprintf(ERROR, "usb_func_register: bad endpoint address 0x%X\n",
                       desc->bEndpointAddress);
                return MX_ERR_INVALID_ARGS;
            }
        }

        if (header->bLength == 0) {
            dprintf(ERROR, "usb_func_register: zero length descriptor\n");
            return MX_ERR_INVALID_ARGS;
        }
        header = (void *)header + header->bLength;
    }

    function->descriptors = malloc(length);
    if (!function->descriptors) {
        return MX_ERR_NO_MEMORY;
    }
    memcpy(function->descriptors, descriptors, length);
    function->descriptors_length = length;
    memcpy(&function->interface, interface, sizeof(function->interface));

    return usb_device_function_registered(function->dev);
}

static mx_status_t usb_func_alloc_interface(void* ctx, uint8_t* out_intf_num) {
    usb_function_t* function = ctx;
    usb_device_t* dev = function->dev;

    for (unsigned i = 0; i < countof(dev->interface_map); i++) {
        if (dev->interface_map[i] == NULL) {
            dev->interface_map[i] = function;
            *out_intf_num = i;
            return MX_OK;
        }
    }
    return MX_ERR_NO_RESOURCES;
}

static mx_status_t usb_func_alloc_ep(void* ctx, uint8_t direction, uint8_t* out_address) {
    unsigned start, end;

    if (direction == USB_DIR_OUT) {
        start = OUT_EP_START;
        end = OUT_EP_END;
    } else if (direction == USB_DIR_IN) {
        start = IN_EP_START;
        end = IN_EP_END;
    } else {
        return MX_ERR_INVALID_ARGS;
    }

    usb_function_t* function = ctx;
    usb_device_t* dev = function->dev;
    usb_function_t** endpoint_map = dev->endpoint_map;

    mtx_lock(&dev->lock);
    for (unsigned index = start; index <= end; index++) {
        if (endpoint_map[index] == NULL) {
            endpoint_map[index] = function;
            mtx_unlock(&dev->lock);
            *out_address = ep_index_to_address(index);
            return MX_OK;
        }
    }

    mtx_unlock(&dev->lock);
    return MX_ERR_NO_RESOURCES;
}

static mx_status_t usb_func_config_ep(void* ctx, usb_endpoint_descriptor_t* ep_desc,
                                      usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
    usb_function_t* function = ctx;
    return usb_dci_config_ep(&function->dev->usb_dci, ep_desc, ss_comp_desc);
}

static mx_status_t usb_func_disable_ep(void* ctx, uint8_t ep_addr) {
    dprintf(TRACE, "usb_func_disable_ep\n");
    usb_function_t* function = ctx;
    return usb_dci_disable_ep(&function->dev->usb_dci, ep_addr);
}

static mx_status_t usb_func_alloc_string_desc(void* ctx, const char* string, uint8_t* out_index) {
    usb_function_t* function = ctx;
    return usb_device_alloc_string_desc(function->dev, string, out_index);
}

static void usb_func_queue(void* ctx, iotxn_t* txn, uint8_t ep_address) {
    usb_function_t* function = ctx;
    txn->protocol = MX_PROTOCOL_USB_FUNCTION;
    usb_function_protocol_data_t* data = iotxn_pdata(txn, usb_function_protocol_data_t);
    data->ep_address = ep_address;
    iotxn_queue(function->dci_dev, txn);
}

static mx_status_t usb_func_ep_set_stall(void* ctx, uint8_t ep_address) {
    usb_function_t* function = ctx;
    return usb_dci_ep_set_stall(&function->dev->usb_dci, ep_address);
}

static mx_status_t usb_func_ep_clear_stall(void* ctx, uint8_t ep_address) {
    usb_function_t* function = ctx;
    return usb_dci_ep_clear_stall(&function->dev->usb_dci, ep_address);
}

usb_function_protocol_ops_t usb_function_proto = {
    .register_func = usb_func_register,
    .alloc_interface = usb_func_alloc_interface,
    .alloc_ep = usb_func_alloc_ep,
    .config_ep = usb_func_config_ep,
    .disable_ep = usb_func_disable_ep,
    .alloc_string_desc = usb_func_alloc_string_desc,
    .queue = usb_func_queue,
    .ep_set_stall = usb_func_ep_set_stall,
    .ep_clear_stall = usb_func_ep_clear_stall,
};

static mx_status_t usb_dev_get_descriptor(usb_device_t* dev, uint8_t request_type,
                                          uint16_t value, uint16_t index, void* buffer,
                                          size_t length, size_t* out_actual) {
    uint8_t type = request_type & USB_TYPE_MASK;

    if (type == USB_TYPE_STANDARD) {
        uint8_t desc_type = value >> 8;
        if (desc_type == USB_DT_DEVICE && index == 0) {
            const usb_device_descriptor_t* desc = &dev->device_desc;
            if (desc->bLength == 0) {
                dprintf(ERROR, "usb_dev_get_descriptor: device descriptor not set\n");
                return MX_ERR_INTERNAL;
            }
            if (length > sizeof(*desc)) length = sizeof(*desc);
            memcpy(buffer, desc, length);
            *out_actual = length;
            return MX_OK;
        } else if (desc_type == USB_DT_CONFIG && index == 0) {
            const usb_configuration_descriptor_t* desc = dev->config_desc;
            if (!desc) {
                dprintf(ERROR, "usb_dev_get_descriptor: configuration descriptor not set\n");
                return MX_ERR_INTERNAL;
            }
            uint16_t desc_length = letoh16(desc->wTotalLength);
            if (length > desc_length) length =desc_length;
            memcpy(buffer, desc, length);
            *out_actual = length;
            return MX_OK;
        }
        else if (value >> 8 == USB_DT_STRING) {
            uint8_t desc[255];
            usb_descriptor_header_t* header = (usb_descriptor_header_t *)desc;
            header->bDescriptorType = USB_DT_STRING;

            uint8_t string_index = value & 0xFF;
            if (string_index == 0) {
                // special case - return language list
                header->bLength = 4;
                desc[2] = 0x09;     // language ID
                desc[3] = 0x04;
            } else {
                char* string = dev->strings[string_index];
                if (!string) {
                    return MX_ERR_INVALID_ARGS;
                }
                unsigned index = 2;

                // convert ASCII to Unicode
                if (string) {
                    while (*string && index < sizeof(desc) - 2) {
                        desc[index++] = *string++;
                        desc[index++] = 0;
                    }
                }
                // zero terminate
                desc[index++] = 0;
                desc[index++] = 0;
                header->bLength = index;
            }

            if (header->bLength < length) length = header->bLength;
            memcpy(buffer, desc, length);
            *out_actual = length;
            return MX_OK;
        }
    }

    dprintf(ERROR, "usb_device_get_descriptor unsupported value: %d index: %d\n", value, index);
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t usb_dev_set_configuration(usb_device_t* dev, uint8_t configuration) {
    mx_status_t status = MX_OK;
    bool configured = configuration > 0;

    mtx_lock(&dev->lock);

    usb_function_t* function;
    list_for_every_entry(&dev->functions, function, usb_function_t, node) {
        if (function->interface.ops) {
            status = usb_function_set_configured(&function->interface, configured, dev->speed);
            if (status != MX_OK && configured) {
                goto fail;
            }
        }
    }

    dev->configuration = configuration;

fail:
    mtx_unlock(&dev->lock);
    return status;
}

static mx_status_t usb_dev_set_interface(usb_device_t* dev, unsigned interface,
                                         unsigned alt_setting) {
    usb_function_t* function = dev->interface_map[interface];
    if (function && function->interface.ops) {
        return usb_function_set_interface(&function->interface, interface, alt_setting);
    }
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t usb_dev_control(void* ctx, const usb_setup_t* setup, void* buffer,
                                   size_t buffer_length, size_t* out_actual) {
    usb_device_t* dev = ctx;
    uint8_t request_type = setup->bmRequestType;
    uint8_t request = setup->bRequest;
    uint16_t value = le16toh(setup->wValue);
    uint16_t index = le16toh(setup->wIndex);
    uint16_t length = le16toh(setup->wLength);
    if (length > buffer_length) length = buffer_length;

    dprintf(TRACE, "usb_dev_control type: 0x%02X req: %d value: %d index: %d length: %d\n",
            request_type, request, value, index, length);

    switch (request_type & USB_RECIP_MASK) {
    case USB_RECIP_DEVICE:
        // handle standard device requests
        if ((request_type & (USB_DIR_MASK | USB_TYPE_MASK)) == (USB_DIR_IN | USB_TYPE_STANDARD) &&
            request == USB_REQ_GET_DESCRIPTOR) {
            return usb_dev_get_descriptor(dev, request_type, value, index, buffer, length,
                                             out_actual);
        } else if (request_type == (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE) &&
                   request == USB_REQ_SET_CONFIGURATION && length == 0) {
            return usb_dev_set_configuration(dev, value);
        } else if (request_type == (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE) &&
                   request == USB_REQ_GET_CONFIGURATION && length > 0) {
            *((uint8_t *)buffer) = dev->configuration;
            *out_actual = sizeof(uint8_t);
            return MX_OK;
        }
        break;
    case USB_RECIP_INTERFACE: {
        if (request_type == (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) &&
                   request == USB_REQ_SET_INTERFACE && length == 0) {
            return usb_dev_set_interface(dev, index, value);
        } else {
            // delegate to the function driver for the interface
            usb_function_t* function = dev->interface_map[index];
            if (function && function->interface.ops) {
                return usb_function_control(&function->interface, setup, buffer, buffer_length,
                                            out_actual);
            }
        }
        break;
    }
    case USB_RECIP_ENDPOINT: {
        // delegate to the function driver for the endpoint
        index = ep_address_to_index(index);
        if (index == 0 || index >= USB_MAX_EPS) {
            return MX_ERR_INVALID_ARGS;
        }
        usb_function_t* function = dev->endpoint_map[index];
        if (function && function->interface.ops) {
            return usb_function_control(&function->interface, setup, buffer, buffer_length,
                                        out_actual);
        }
        break;
    }
    case USB_RECIP_OTHER:
        // TODO(voydanoff) - how to handle this?
    default:
        break;
    }

    return MX_ERR_NOT_SUPPORTED;
}

static void usb_dev_set_connected(void* ctx, bool connected) {
    usb_device_t* dev = ctx;
    if (dev->connected != connected) {
        if (!connected) {
            usb_function_t* function;
            list_for_every_entry(&dev->functions, function, usb_function_t, node) {
                if (function->interface.ops) {
                    usb_function_set_configured(&function->interface, false, USB_SPEED_UNDEFINED);
                }
            }
        }

        dev->connected = connected;
    }
}

static void usb_dev_set_speed(void* ctx, usb_speed_t speed) {
    usb_device_t* dev = ctx;
    dev->speed = speed;
}

usb_dci_interface_ops_t dci_ops = {
    .control = usb_dev_control,
    .set_connected = usb_dev_set_connected,
    .set_speed = usb_dev_set_speed,
};

static mx_status_t usb_dev_set_device_desc(usb_device_t* dev, const void* in_buf, size_t in_len) {
    if (in_len != sizeof(dev->device_desc)) {
        return MX_ERR_INVALID_ARGS;
    }
    const usb_device_descriptor_t* desc = in_buf;
    if (desc->bLength != sizeof(*desc) ||
        desc->bDescriptorType != USB_DT_DEVICE) {
        return MX_ERR_INVALID_ARGS;
    }
    if (desc->bNumConfigurations != 1) {
        dprintf(ERROR, "usb_device_ioctl: bNumConfigurations: %u, only 1 supported\n",
                desc->bNumConfigurations);
        return MX_ERR_INVALID_ARGS;
    }
    memcpy(&dev->device_desc, desc, sizeof(dev->device_desc));
    return MX_OK;
}

static mx_status_t usb_dev_alloc_string_desc(usb_device_t* dev, const void* in_buf, size_t in_len,
                                             void* out_buf, size_t out_len, size_t* out_actual) {
    if (in_len < 2 || out_len < sizeof(uint8_t)) {
        return MX_ERR_INVALID_ARGS;
    }

    // make sure string is zero terminated
    *((char *)in_buf + in_len - 1) = 0;

    uint8_t index;
    mx_status_t status = usb_device_alloc_string_desc(dev, in_buf, &index);
    if (status != MX_OK) {
        return status;
     }

    *((uint8_t *)out_buf) = index;
    *out_actual = sizeof(index);
    return MX_OK;
}

static mx_status_t usb_dev_add_function(usb_device_t* dev, const void* in_buf, size_t in_len) {
    if (in_len != sizeof(usb_function_descriptor_t)) {
        return MX_ERR_INVALID_ARGS;
    }
    if (dev->functions_bound) {
        return MX_ERR_BAD_STATE;
    }

    usb_function_t* function = calloc(1, sizeof(usb_function_t));
    if (!function) {
        return MX_ERR_NO_MEMORY;
    }
    function->dci_dev = dev->dci_dev;
    function->dev = dev;
    memcpy(&function->desc, in_buf, sizeof(function->desc));
    list_add_tail(&dev->functions, &function->node);

    return MX_OK;
}

static mx_status_t usb_dev_bind_functions(usb_device_t* dev) {
    if (dev->functions_bound) {
        dprintf(ERROR, "usb_dev_bind_functions: already bound!\n");
        return MX_ERR_BAD_STATE;
    }

    usb_device_descriptor_t* device_desc = &dev->device_desc;
    if (device_desc->bLength == 0) {
        dprintf(ERROR, "usb_dev_bind_functions: device descriptor not set\n");
        return MX_ERR_BAD_STATE;
    }
    if (list_is_empty(&dev->functions)) {
        dprintf(ERROR, "usb_dev_bind_functions: no functions to bind\n");
        return MX_ERR_BAD_STATE;
    }

    int index = 0;
    usb_function_t* function;
    list_for_every_entry(&dev->functions, function, usb_function_t, node) {
        char name[16];
        snprintf(name, sizeof(name), "function-%03d", index);

        usb_function_descriptor_t* desc = &function->desc;

        mx_device_prop_t props[] = {
            { BIND_PROTOCOL, 0, MX_PROTOCOL_USB_FUNCTION },
            { BIND_USB_CLASS, 0, desc->interface_class },
            { BIND_USB_SUBCLASS, 0, desc->interface_subclass },
            { BIND_USB_PROTOCOL, 0, desc->interface_protocol },
            { BIND_USB_VID, 0, device_desc->idVendor },
            { BIND_USB_PID, 0, device_desc->idProduct },
        };

        device_add_args_t args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = name,
            .ctx = function,
            .ops = &function_proto,
            .proto_id = MX_PROTOCOL_USB_FUNCTION,
            .proto_ops = &usb_function_proto,
            .props = props,
            .prop_count = countof(props),
        };

        mx_status_t status = device_add(dev->mxdev, &args, &function->mxdev);
        if (status != MX_OK) {
            dprintf(ERROR, "usb_dev_bind_functions add_device failed %d\n", status);
            return status;
        }

        index++;
    }

    dev->functions_bound = true;

    return MX_OK;
}

static mx_status_t usb_dev_clear_functions(usb_device_t* dev) {
    usb_function_t* function;
    while ((function = list_remove_head_type(&dev->functions, usb_function_t, node)) != NULL) {
        device_remove(function->mxdev);
    }
    free(dev->config_desc);
    dev->config_desc = NULL;
    dev->functions_bound = false;
    memset(dev->interface_map, 0, sizeof(dev->interface_map));
    memset(dev->endpoint_map, 0, sizeof(dev->endpoint_map));
    for (unsigned i = 0; i < countof(dev->strings); i++) {
        free(dev->strings[i]);
        dev->strings[i] = NULL;
    }
    return MX_OK;
}

static mx_status_t usb_dev_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                 void* out_buf, size_t out_len, size_t* out_actual) {
    dprintf(TRACE, "usb_dev_ioctl %x\n", op);
    usb_device_t* dev = ctx;

    switch (op) {
    case IOCTL_USB_DEVICE_SET_DEVICE_DESC:
        return usb_dev_set_device_desc(dev, in_buf, in_len);
    case IOCTL_USB_DEVICE_ALLOC_STRING_DESC:
        return usb_dev_alloc_string_desc(dev, in_buf, in_len, out_buf, out_len, out_actual);
    case IOCTL_USB_DEVICE_ADD_FUNCTION:
        return usb_dev_add_function(dev, in_buf, in_len);
    case IOCTL_USB_DEVICE_BIND_FUNCTIONS:
        return usb_dev_bind_functions(dev);
    case IOCTL_USB_DEVICE_CLEAR_FUNCTIONS:
        return usb_dev_clear_functions(dev);
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}

static void usb_dev_unbind(void* ctx) {
    dprintf(TRACE, "usb_dev_unbind\n");
    usb_device_t* dev = ctx;
    usb_dev_clear_functions(dev);
    device_remove(dev->mxdev);
}

static void usb_dev_release(void* ctx) {
    dprintf(TRACE, "usb_dev_release\n");
    usb_device_t* dev = ctx;
    free(dev->config_desc);
    for (unsigned i = 0; i < countof(dev->strings); i++) {
        free(dev->strings[i]);
    }
    free(dev);
}

static mx_protocol_device_t device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = usb_dev_ioctl,
    .unbind = usb_dev_unbind,
    .release = usb_dev_release,
};

#if defined(USB_DEVICE_VID) && defined(USB_DEVICE_PID) && defined(USB_DEVICE_FUNCTIONS)
static mx_status_t usb_dev_set_default_config(usb_device_t* dev) {
    usb_device_descriptor_t device_desc = {
        .bLength = sizeof(usb_device_descriptor_t),
        .bDescriptorType = USB_DT_DEVICE,
        .bcdUSB = htole16(0x0200),
        .bDeviceClass = 0,
        .bDeviceSubClass = 0,
        .bDeviceProtocol = 0,
        .bMaxPacketSize0 = 64,
        .idVendor = htole16(USB_DEVICE_VID),
        .idProduct = htole16(USB_DEVICE_PID),
        .bcdDevice = htole16(0x0100),
        .bNumConfigurations = 1,
    };

    mx_status_t status = MX_OK;

#ifdef USB_DEVICE_MANUFACTURER
    status = usb_device_alloc_string_desc(dev, USB_DEVICE_MANUFACTURER, &device_desc.iManufacturer);
    if (status != MX_OK) return status;
#endif
#ifdef USB_DEVICE_PRODUCT
    usb_device_alloc_string_desc(dev, USB_DEVICE_PRODUCT, &device_desc.iProduct);
    if (status != MX_OK) return status;
#endif
#ifdef USB_DEVICE_SERIAL
    usb_device_alloc_string_desc(dev, USB_DEVICE_SERIAL, &device_desc.iSerialNumber);
    if (status != MX_OK) return status;
#endif

    status = usb_dev_set_device_desc(dev, &device_desc, sizeof(device_desc));
    if (status != MX_OK) return status;

    usb_function_descriptor_t function_desc;
    if (strcasecmp(USB_DEVICE_FUNCTIONS, "cdc") == 0) {
        function_desc.interface_class = USB_CLASS_COMM;
        function_desc.interface_subclass = USB_CDC_SUBCLASS_ETHERNET;
        function_desc.interface_protocol = 0;
    } else if (strcasecmp(USB_DEVICE_FUNCTIONS, "ums") == 0) {
        function_desc.interface_class = USB_CLASS_MSC;
        function_desc.interface_subclass = USB_SUBCLASS_MSC_SCSI;
        function_desc.interface_protocol = USB_PROTOCOL_MSC_BULK_ONLY;
    } else {
        dprintf(ERROR, "usb_dev_set_default_config: unknown function %s\n", USB_DEVICE_FUNCTIONS);
        return MX_ERR_INVALID_ARGS;
    }

    status = usb_dev_add_function(dev, &function_desc, sizeof(function_desc));
    if (status != MX_OK) return status;

    return usb_dev_bind_functions(dev);
}
#endif // defined(USB_DEVICE_VID) && defined(USB_DEVICE_PID) && defined(USB_DEVICE_FUNCTIONS)

mx_status_t usb_dev_bind(void* ctx, mx_device_t* parent, void** cookie) {
    dprintf(INFO, "usb_dev_bind\n");

    usb_device_t* dev = calloc(1, sizeof(usb_device_t));
    if (!dev) {
        return MX_ERR_NO_MEMORY;
    }
    list_initialize(&dev->functions);
    mtx_init(&dev->lock, mtx_plain);
    dev->dci_dev = parent;

    if (device_get_protocol(parent, MX_PROTOCOL_USB_DCI, &dev->usb_dci)) {
        free(dev);
        return MX_ERR_NOT_SUPPORTED;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "usb-device",
        .ctx = dev,
        .ops = &device_proto,
        .proto_id = MX_PROTOCOL_USB_DEVICE,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    mx_status_t status = device_add(parent, &args, &dev->mxdev);
    if (status != MX_OK) {
        dprintf(ERROR, "usb_device_bind add_device failed %d\n", status);
        free(dev);
        return status;
    }

    usb_dci_interface_t intf = {
        .ops = &dci_ops,
        .ctx = dev,
    };
    usb_dci_set_interface(&dev->usb_dci, &intf);

#if defined(USB_DEVICE_VID) && defined(USB_DEVICE_PID) && defined(USB_DEVICE_FUNCTIONS)
    // set compile time configuration, if we have one
    usb_dev_set_default_config(dev);
#endif

    return MX_OK;
}

static mx_driver_ops_t usb_device_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_dev_bind,
};

// clang-format off
MAGENTA_DRIVER_BEGIN(usb_device, usb_device_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_USB_DCI),
MAGENTA_DRIVER_END(usb_device)
