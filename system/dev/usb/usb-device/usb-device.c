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
#include <ddk/metadata.h>
#include <ddk/protocol/usb-dci.h>
#include <ddk/protocol/usb-function.h>
#include <ddk/protocol/usb-mode-switch.h>
#include <ddk/usb-request/usb-request.h>
#include <zircon/listnode.h>
#include <zircon/device/usb-device.h>
#include <zircon/hw/usb-cdc.h>
#include <zircon/hw/usb.h>

/*
    THEORY OF OPERATION

    This driver is responsible for USB in the peripheral role, that is,
    acting as a USB device to a USB host.
    It serves as the central point of coordination for the peripheral role.
    It is configured via ioctls in the ZX_PROTOCOL_USB_DEVICE protocol
    (which is used by the usbctl command line program).
    Based on this configuration, it creates one or more DDK devices with protocol
    ZX_PROTOCOL_USB_FUNCTION. These devices are bind points for USB function drivers,
    which implement USB interfaces for particular functions (like USB ethernet or mass storage).
    This driver also binds to a device with protocol ZX_PROTOCOL_USB_DCI
    (Device Controller Interface) which is implemented by a driver for the actual
    USB controller hardware for the peripheral role.

    There are several steps needed to initialize and start USB in the peripheral role.
    The first step is setting up the USB configuration via ioctls.
    ioctl_usb_device_set_device_desc() sets the USB device descriptor to be presented
    to the host during enumeration.
    Next, ioctl_usb_device_add_function() can be called one or more times to add
    descriptors for the USB functions to be included in the USB configuration.
    Finally after all the functions have been added, ioctl_usb_device_bind_functions()
    tells this driver that configuration is complete and it is now possible to build
    the configuration descriptor. Once we get to this point, usb_device_t.functions_bound
    is set to true.

    Independent of this configuration process, ioctl_usb_device_set_mode() can be used
    to configure the role of the USB controller. If the role is set to USB_MODE_DEVICE
    and "functions_bound" is true, then we are ready to start USB in peripheral role.
    At this point, we create DDK devices for our list of functions.
    When the function drivers bind to these functions, they register an interface of type
    usb_function_interface_t with this driver via the usb_function_register() API.
    Once all of the function drivers have registered themselves this way,
    usb_device_t.functions_registered is set to true.

    if the usb mode is set to USB_MODE_DEVICE and "functions_registered" is true,
    we are now finally ready to operate in the peripheral role.
    At this point we can inform the DCI driver to start running in peripheral role
    by calling usb_mode_switch_set_mode(USB_MODE_DEVICE) on its ZX_PROTOCOL_USB_MODE_SWITCH
    interface. Now the USB controller hardware is up and running as a USB peripheral.

    Teardown of the peripheral role one of two ways:
    First, ioctl_usb_device_clear_functions() will reset this device's list of USB functions.
    Second, the USB mode can be set to something other than USB_MODE_DEVICE.
    In this second case, we will remove the DDK devices for the USB functions
    so the function drivers will unbind, but the USB configuration remains ready to go
    for when the USB mode is switched back to USB_MODE_DEVICE.
*/

#define MAX_INTERFACES 32

typedef struct {
    zx_device_t* zxdev;
    zx_device_t* dci_dev;
    struct usb_device* dev;
    list_node_t node;
    usb_function_interface_t interface;
    usb_function_descriptor_t desc;
    usb_descriptor_header_t* descriptors;
    size_t descriptors_length;
    unsigned num_interfaces;
} usb_function_t;

typedef struct usb_device {
    // the device we publish
    zx_device_t* zxdev;
    // our parent device
    zx_device_t* dci_dev;
    // our parent's DCI protocol
    usb_dci_protocol_t usb_dci;
    // our parent's USB switch protocol
    usb_mode_switch_protocol_t usb_mode_switch;
    // USB device descriptor set via ioctl_usb_device_set_device_desc()
    usb_device_descriptor_t device_desc;
    // USB configuration descriptor, synthesized from our functions' descriptors
    usb_configuration_descriptor_t* config_desc;
    // map from interface number to function
    usb_function_t* interface_map[MAX_INTERFACES];
    // map from endpoint index to function
    usb_function_t* endpoint_map[USB_MAX_EPS];
    // BTI handle shared from DCI layer
    zx_handle_t bti_handle;
    // strings for USB string descriptors
    char* strings[256];
    // list of usb_function_t
    list_node_t functions;
    // mutex for protecting our state
    mtx_t lock;
    // current USB mode set via ioctl_usb_device_set_mode()
    usb_mode_t usb_mode;
    // our parent's USB mode
     usb_mode_t dci_usb_mode;
    // set if ioctl_usb_device_bind_functions() has been called
    // and we have a complete list of our function.
    bool functions_bound;
    // set if all our functions have registered their usb_function_interface_t
    bool functions_registered;
    // true if we have added child devices for our functions
    bool function_devs_added;
    // true if we are connected to a host
    bool connected;
    // current configuration number selected via USB_REQ_SET_CONFIGURATION
    // (will be 0 or 1 since we currently do not support multiple configurations)
    uint8_t configuration;
    // USB connection speed
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

static zx_status_t usb_dev_state_changed_locked(usb_device_t* dev);

static zx_status_t usb_device_alloc_string_desc(usb_device_t* dev, const char* string,
                                                uint8_t* out_index) {

    mtx_lock(&dev->lock);

    unsigned i;
    for (i = 1; i < countof(dev->strings); i++) {
        if (!dev->strings[i]) {
            break;
        }
    }
    if (i == countof(dev->strings)) {
        mtx_unlock(&dev->lock);
        return ZX_ERR_NO_RESOURCES;
    }

    dev->strings[i] = strdup(string);
    if (!dev->strings[i]) {
        mtx_unlock(&dev->lock);
        return ZX_ERR_NO_MEMORY;
    }

    mtx_unlock(&dev->lock);

    *out_index = i;
    return ZX_OK;
}

static zx_protocol_device_t function_proto = {
    .version = DEVICE_OPS_VERSION,
    // Note that we purposely do not have a release callback for USB functions.
    // The functions are kept on a list when not active so they can be re-added
    // when reentering device mode.
};

static zx_status_t usb_device_function_registered(usb_device_t* dev) {
    mtx_lock(&dev->lock);

    if (dev->config_desc) {
        zxlogf(ERROR, "usb_device_function_registered: already have configuration descriptor!\n");
        mtx_unlock(&dev->lock);
        return ZX_ERR_BAD_STATE;
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
            return ZX_OK;
        }
    }

    // build our configuration descriptor
    usb_configuration_descriptor_t* config_desc = malloc(length);
    if (!config_desc) {
        mtx_unlock(&dev->lock);
        return ZX_ERR_NO_MEMORY;
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

    zxlogf(TRACE, "usb_device_function_registered functions_registered = true\n");
    dev->functions_registered = true;

    zx_status_t status = usb_dev_state_changed_locked(dev);
    mtx_unlock(&dev->lock);
    return status;
}

static zx_status_t usb_func_req_alloc(void* ctx, usb_request_t** out, uint64_t data_size,
                                     uint8_t ep_address) {
    usb_function_t* function = ctx;
    usb_device_t* dev = function->dev;

    return usb_request_alloc(out, dev->bti_handle, data_size, ep_address);
}

static zx_status_t usb_func_req_alloc_vmo(void* ctx, usb_request_t** out, zx_handle_t vmo_handle,
                                          uint64_t vmo_offset, uint64_t length,
                                          uint8_t ep_address) {
    usb_function_t* function = ctx;
    usb_device_t* dev = function->dev;

    return usb_request_alloc_vmo(out, dev->bti_handle, vmo_handle, vmo_offset, length, ep_address);
}

static zx_status_t usb_func_req_init(void* ctx, usb_request_t* req, zx_handle_t vmo_handle,
                                     uint64_t vmo_offset, uint64_t length, uint8_t ep_address) {
    usb_function_t* function = ctx;
    usb_device_t* dev = function->dev;

    return usb_request_init(req, dev->bti_handle, vmo_handle, vmo_offset, length, ep_address);
}


static ssize_t usb_func_req_copy_from(void* ctx, usb_request_t* req, void* data,
                                          size_t length, size_t offset) {
    return usb_request_copyfrom(req, data, length, offset);
}

static ssize_t usb_func_req_copy_to(void* ctx, usb_request_t* req, const void* data,
                                        size_t length, size_t offset) {
    return usb_request_copyto(req, data, length, offset);
}

static zx_status_t usb_func_req_mmap(void* ctx, usb_request_t* req, void** data) {
    return usb_request_mmap(req, data);
}

static zx_status_t usb_func_req_cacheop(void* ctx, usb_request_t* req, uint32_t op,
                                        size_t offset, size_t length) {
    return usb_request_cacheop(req, op, offset, length);
}

static zx_status_t usb_func_req_cache_flush(void* ctx, usb_request_t* req, size_t offset,
                                            size_t length) {
    return usb_request_cache_flush(req, offset, length);
}

static zx_status_t usb_func_req_cache_flush_invalidate(void* ctx, usb_request_t* req,
                                                            zx_off_t offset, size_t length) {
    return usb_request_cache_flush_invalidate(req, offset, length);
}

static zx_status_t usb_func_req_physmap(void* ctx, usb_request_t* req) {
    return usb_request_physmap(req);
}

static void usb_func_req_release(void* ctx, usb_request_t* req) {
    usb_request_release(req);
}

static void usb_func_req_complete(void* ctx, usb_request_t* req,
                                       zx_status_t status, zx_off_t actual) {
    usb_request_complete(req, status, actual);
}

static void usb_func_req_phys_iter_init(void* ctx, phys_iter_t* iter, usb_request_t* req,
                                             size_t max_length) {
    usb_request_phys_iter_init(iter, req, max_length);
}

static zx_status_t usb_func_register(void* ctx, usb_function_interface_t* interface) {
    usb_function_t* function = ctx;
    usb_device_t* dev = function->dev;
    usb_function_t** endpoint_map = dev->endpoint_map;

    size_t length;
    const usb_descriptor_header_t* descriptors = usb_function_get_descriptors(interface, &length);

    // validate the descriptor list
    if (!descriptors || length < sizeof(usb_interface_descriptor_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    usb_interface_descriptor_t* intf_desc = (usb_interface_descriptor_t *)descriptors;
    if (intf_desc->bDescriptorType != USB_DT_INTERFACE ||
            intf_desc->bLength != sizeof(usb_interface_descriptor_t)) {
        zxlogf(ERROR, "usb_func_register: first descriptor not an interface descriptor\n");
        return ZX_ERR_INVALID_ARGS;
    }

    const usb_descriptor_header_t* end = (void *)descriptors + length;
    const usb_descriptor_header_t* header = descriptors;

    while (header < end) {
        if (header->bDescriptorType == USB_DT_INTERFACE) {
            usb_interface_descriptor_t* desc = (usb_interface_descriptor_t *)header;
            if (desc->bInterfaceNumber >= countof(dev->interface_map) ||
                dev->interface_map[desc->bInterfaceNumber] != function) {
                zxlogf(ERROR, "usb_func_register: bInterfaceNumber %u\n",
                       desc->bInterfaceNumber);
                return ZX_ERR_INVALID_ARGS;
            }
            if (desc->bAlternateSetting == 0) {
                function->num_interfaces++;
            }
        } else if (header->bDescriptorType == USB_DT_ENDPOINT) {
            usb_endpoint_descriptor_t* desc = (usb_endpoint_descriptor_t *)header;
            unsigned index = ep_address_to_index(desc->bEndpointAddress);
            if (index == 0 || index >= countof(dev->endpoint_map) ||
                endpoint_map[index] != function) {
                zxlogf(ERROR, "usb_func_register: bad endpoint address 0x%X\n",
                       desc->bEndpointAddress);
                return ZX_ERR_INVALID_ARGS;
            }
        }

        if (header->bLength == 0) {
            zxlogf(ERROR, "usb_func_register: zero length descriptor\n");
            return ZX_ERR_INVALID_ARGS;
        }
        header = (void *)header + header->bLength;
    }

    function->descriptors = malloc(length);
    if (!function->descriptors) {
        return ZX_ERR_NO_MEMORY;
    }
    memcpy(function->descriptors, descriptors, length);
    function->descriptors_length = length;
    memcpy(&function->interface, interface, sizeof(function->interface));

    return usb_device_function_registered(function->dev);
}

static zx_status_t usb_func_alloc_interface(void* ctx, uint8_t* out_intf_num) {
    usb_function_t* function = ctx;
    usb_device_t* dev = function->dev;

    mtx_lock(&dev->lock);

    for (unsigned i = 0; i < countof(dev->interface_map); i++) {
        if (dev->interface_map[i] == NULL) {
            dev->interface_map[i] = function;
            mtx_unlock(&dev->lock);
            *out_intf_num = i;
            return ZX_OK;
        }
    }

    mtx_unlock(&dev->lock);
    return ZX_ERR_NO_RESOURCES;
}

static zx_status_t usb_func_alloc_ep(void* ctx, uint8_t direction, uint8_t* out_address) {
    unsigned start, end;

    if (direction == USB_DIR_OUT) {
        start = OUT_EP_START;
        end = OUT_EP_END;
    } else if (direction == USB_DIR_IN) {
        start = IN_EP_START;
        end = IN_EP_END;
    } else {
        return ZX_ERR_INVALID_ARGS;
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
            return ZX_OK;
        }
    }

    mtx_unlock(&dev->lock);
    return ZX_ERR_NO_RESOURCES;
}

static zx_status_t usb_func_config_ep(void* ctx, usb_endpoint_descriptor_t* ep_desc,
                                      usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
    usb_function_t* function = ctx;
    return usb_dci_config_ep(&function->dev->usb_dci, ep_desc, ss_comp_desc);
}

static zx_status_t usb_func_disable_ep(void* ctx, uint8_t ep_addr) {
    zxlogf(TRACE, "usb_func_disable_ep\n");
    usb_function_t* function = ctx;
    return usb_dci_disable_ep(&function->dev->usb_dci, ep_addr);
}

static zx_status_t usb_func_alloc_string_desc(void* ctx, const char* string, uint8_t* out_index) {
    usb_function_t* function = ctx;
    return usb_device_alloc_string_desc(function->dev, string, out_index);
}

static void usb_func_queue(void* ctx, usb_request_t* req) {
    usb_function_t* function = ctx;
    usb_dci_request_queue(&function->dev->usb_dci, req);
}

static zx_status_t usb_func_ep_set_stall(void* ctx, uint8_t ep_address) {
    usb_function_t* function = ctx;
    return usb_dci_ep_set_stall(&function->dev->usb_dci, ep_address);
}

static zx_status_t usb_func_ep_clear_stall(void* ctx, uint8_t ep_address) {
    usb_function_t* function = ctx;
    return usb_dci_ep_clear_stall(&function->dev->usb_dci, ep_address);
}

usb_function_protocol_ops_t usb_function_proto = {
    .req_alloc = usb_func_req_alloc,
    .req_alloc_vmo = usb_func_req_alloc_vmo,
    .req_init = usb_func_req_init,
    .req_copy_from = usb_func_req_copy_from,
    .req_copy_to = usb_func_req_copy_to,
    .req_mmap = usb_func_req_mmap,
    .req_cacheop = usb_func_req_cacheop,
    .req_cache_flush = usb_func_req_cache_flush,
    .req_cache_flush_invalidate = usb_func_req_cache_flush_invalidate,
    .req_physmap = usb_func_req_physmap,
    .req_release = usb_func_req_release,
    .req_complete = usb_func_req_complete,
    .req_phys_iter_init = usb_func_req_phys_iter_init,
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

static zx_status_t usb_dev_get_descriptor(usb_device_t* dev, uint8_t request_type,
                                          uint16_t value, uint16_t index, void* buffer,
                                          size_t length, size_t* out_actual) {
    uint8_t type = request_type & USB_TYPE_MASK;

    if (type == USB_TYPE_STANDARD) {
        uint8_t desc_type = value >> 8;
        if (desc_type == USB_DT_DEVICE && index == 0) {
            const usb_device_descriptor_t* desc = &dev->device_desc;
            if (desc->bLength == 0) {
                zxlogf(ERROR, "usb_dev_get_descriptor: device descriptor not set\n");
                return ZX_ERR_INTERNAL;
            }
            if (length > sizeof(*desc)) length = sizeof(*desc);
            memcpy(buffer, desc, length);
            *out_actual = length;
            return ZX_OK;
        } else if (desc_type == USB_DT_CONFIG && index == 0) {
            const usb_configuration_descriptor_t* desc = dev->config_desc;
            if (!desc) {
                zxlogf(ERROR, "usb_dev_get_descriptor: configuration descriptor not set\n");
                return ZX_ERR_INTERNAL;
            }
            uint16_t desc_length = letoh16(desc->wTotalLength);
            if (length > desc_length) length =desc_length;
            memcpy(buffer, desc, length);
            *out_actual = length;
            return ZX_OK;
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
                    return ZX_ERR_INVALID_ARGS;
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
            return ZX_OK;
        }
    }

    zxlogf(ERROR, "usb_device_get_descriptor unsupported value: %d index: %d\n", value, index);
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t usb_dev_set_configuration(usb_device_t* dev, uint8_t configuration) {
    zx_status_t status = ZX_OK;
    bool configured = configuration > 0;

    mtx_lock(&dev->lock);

    usb_function_t* function;
    list_for_every_entry(&dev->functions, function, usb_function_t, node) {
        if (function->interface.ops) {
            status = usb_function_set_configured(&function->interface, configured, dev->speed);
            if (status != ZX_OK && configured) {
                goto fail;
            }
        }
    }

    dev->configuration = configuration;

fail:
    mtx_unlock(&dev->lock);
    return status;
}

static zx_status_t usb_dev_set_interface(usb_device_t* dev, unsigned interface,
                                         unsigned alt_setting) {
    usb_function_t* function = dev->interface_map[interface];
    if (function && function->interface.ops) {
        return usb_function_set_interface(&function->interface, interface, alt_setting);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t usb_dev_control(void* ctx, const usb_setup_t* setup, void* buffer,
                                   size_t buffer_length, size_t* out_actual) {
    usb_device_t* dev = ctx;
    uint8_t request_type = setup->bmRequestType;
    uint8_t request = setup->bRequest;
    uint16_t value = le16toh(setup->wValue);
    uint16_t index = le16toh(setup->wIndex);
    uint16_t length = le16toh(setup->wLength);
    if (length > buffer_length) length = buffer_length;

    zxlogf(TRACE, "usb_dev_control type: 0x%02X req: %d value: %d index: %d length: %d\n",
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
            return ZX_OK;
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
            return ZX_ERR_INVALID_ARGS;
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

    return ZX_ERR_NOT_SUPPORTED;
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

static zx_status_t usb_dev_set_device_desc(usb_device_t* dev, const void* in_buf, size_t in_len) {
    if (in_len != sizeof(dev->device_desc)) {
        return ZX_ERR_INVALID_ARGS;
    }
    const usb_device_descriptor_t* desc = in_buf;
    if (desc->bLength != sizeof(*desc) ||
        desc->bDescriptorType != USB_DT_DEVICE) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (desc->bNumConfigurations != 1) {
        zxlogf(ERROR, "usb_device_ioctl: bNumConfigurations: %u, only 1 supported\n",
                desc->bNumConfigurations);
        return ZX_ERR_INVALID_ARGS;
    }
    memcpy(&dev->device_desc, desc, sizeof(dev->device_desc));
    return ZX_OK;
}

static zx_status_t usb_dev_alloc_string_desc(usb_device_t* dev, const void* in_buf, size_t in_len,
                                             void* out_buf, size_t out_len, size_t* out_actual) {
    if (in_len < 2 || out_len < sizeof(uint8_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    // make sure string is zero terminated
    *((char *)in_buf + in_len - 1) = 0;

    uint8_t index;
    zx_status_t status = usb_device_alloc_string_desc(dev, in_buf, &index);
    if (status != ZX_OK) {
        return status;
     }

    *((uint8_t *)out_buf) = index;
    *out_actual = sizeof(index);
    return ZX_OK;
}

static zx_status_t usb_dev_add_function(usb_device_t* dev, const void* in_buf, size_t in_len) {
    if (in_len != sizeof(usb_function_descriptor_t)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (dev->functions_bound) {
        return ZX_ERR_BAD_STATE;
    }

    usb_function_t* function = calloc(1, sizeof(usb_function_t));
    if (!function) {
        return ZX_ERR_NO_MEMORY;
    }
    function->dci_dev = dev->dci_dev;
    function->dev = dev;
    memcpy(&function->desc, in_buf, sizeof(function->desc));
    list_add_tail(&dev->functions, &function->node);

    return ZX_OK;
}

static void usb_dev_remove_function_devices_locked(usb_device_t* dev) {
    zxlogf(TRACE, "usb_dev_remove_function_devices_locked\n");

    usb_function_t* function;
    list_for_every_entry(&dev->functions, function, usb_function_t, node) {
        if (function->zxdev) {
            // here we remove the function from the DDK device tree,
            // but the storage for the function remains on our function list.
            device_remove(function->zxdev);
            function->zxdev = NULL;
        }
    }

    free(dev->config_desc);
    dev->config_desc = NULL;
    dev->functions_registered = false;
    dev->function_devs_added = false;
}

static zx_status_t usb_dev_add_function_devices_locked(usb_device_t* dev) {
    zxlogf(TRACE, "usb_dev_add_function_devices_locked\n");
    if (dev->function_devs_added) {
        return ZX_OK;
    }

    usb_device_descriptor_t* device_desc = &dev->device_desc;
    int index = 0;
    usb_function_t* function;
    list_for_every_entry(&dev->functions, function, usb_function_t, node) {
        char name[16];
        snprintf(name, sizeof(name), "function-%03d", index);

        usb_function_descriptor_t* desc = &function->desc;

        zx_device_prop_t props[] = {
            { BIND_PROTOCOL, 0, ZX_PROTOCOL_USB_FUNCTION },
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
            .proto_id = ZX_PROTOCOL_USB_FUNCTION,
            .proto_ops = &usb_function_proto,
            .props = props,
            .prop_count = countof(props),
        };

        zx_status_t status = device_add(dev->zxdev, &args, &function->zxdev);
        if (status != ZX_OK) {
            zxlogf(ERROR, "usb_dev_bind_functions add_device failed %d\n", status);
            return status;
        }

        index++;
    }

    dev->function_devs_added = true;
    return ZX_OK;
}

static zx_status_t usb_dev_state_changed_locked(usb_device_t* dev) {
    zxlogf(TRACE, "usb_dev_state_changed_locked usb_mode: %d dci_usb_mode: %d\n", dev->usb_mode,
            dev->dci_usb_mode);

    usb_mode_t new_dci_usb_mode = dev->dci_usb_mode;
    bool add_function_devs = (dev->usb_mode == USB_MODE_DEVICE && dev->functions_bound);
    zx_status_t status = ZX_OK;

    if (dev->usb_mode == USB_MODE_DEVICE) {
        if (dev->functions_registered) {
            // switch DCI to device mode
            new_dci_usb_mode = USB_MODE_DEVICE;
        } else {
            new_dci_usb_mode = USB_MODE_NONE;
        }
    } else {
        new_dci_usb_mode = dev->usb_mode;
    }

    if (add_function_devs) {
        // publish child devices if necessary
        if (!dev->function_devs_added) {
            status = usb_dev_add_function_devices_locked(dev);
            if (status != ZX_OK) {
                return status;
            }
        }
    }

    if (dev->dci_usb_mode != new_dci_usb_mode) {
        zxlogf(TRACE, "usb_dev_state_changed_locked set DCI mode %d\n", new_dci_usb_mode);
        status = usb_mode_switch_set_mode(&dev->usb_mode_switch, new_dci_usb_mode);
        if (status != ZX_OK) {
            usb_mode_switch_set_mode(&dev->usb_mode_switch, USB_MODE_NONE);
            new_dci_usb_mode = USB_MODE_NONE;
        }
        dev->dci_usb_mode = new_dci_usb_mode;
    }

    if (!add_function_devs && dev->function_devs_added) {
        usb_dev_remove_function_devices_locked(dev);
    }

    return status;
}

static zx_status_t usb_dev_bind_functions(usb_device_t* dev) {
    mtx_lock(&dev->lock);

    if (dev->functions_bound) {
        zxlogf(ERROR, "usb_dev_bind_functions: already bound!\n");
        mtx_unlock(&dev->lock);
        return ZX_ERR_BAD_STATE;
    }

    usb_device_descriptor_t* device_desc = &dev->device_desc;
    if (device_desc->bLength == 0) {
        zxlogf(ERROR, "usb_dev_bind_functions: device descriptor not set\n");
        mtx_unlock(&dev->lock);
        return ZX_ERR_BAD_STATE;
    }
    if (list_is_empty(&dev->functions)) {
        zxlogf(ERROR, "usb_dev_bind_functions: no functions to bind\n");
        mtx_unlock(&dev->lock);
        return ZX_ERR_BAD_STATE;
    }

    zxlogf(TRACE, "usb_dev_bind_functions functions_bound = true\n");
    dev->functions_bound = true;
    zx_status_t status = usb_dev_state_changed_locked(dev);
    mtx_unlock(&dev->lock);
    return status;
}

static zx_status_t usb_dev_clear_functions(usb_device_t* dev) {
    zxlogf(TRACE, "usb_dev_clear_functions\n");
    mtx_lock(&dev->lock);

    usb_function_t* function;
    while ((function = list_remove_head_type(&dev->functions, usb_function_t, node)) != NULL) {
        if (function->zxdev) {
            device_remove(function->zxdev);
            // device_remove will not actually free the function, so we free it here
            free(function->descriptors);
            free(function);
        }
    }
    free(dev->config_desc);
    dev->config_desc = NULL;
    dev->functions_bound = false;
    dev->functions_registered = false;

    memset(dev->interface_map, 0, sizeof(dev->interface_map));
    memset(dev->endpoint_map, 0, sizeof(dev->endpoint_map));
    for (unsigned i = 0; i < countof(dev->strings); i++) {
        free(dev->strings[i]);
        dev->strings[i] = NULL;
    }

    zx_status_t status = usb_dev_state_changed_locked(dev);
    mtx_unlock(&dev->lock);
    return status;
}

static zx_status_t usb_dev_get_mode(usb_device_t* dev, void* out_buf, size_t out_len,
                                    size_t* out_actual) {
    if (out_len < sizeof(usb_mode_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    *((usb_mode_t *)out_buf) = dev->usb_mode;
    *out_actual = sizeof(usb_mode_t);
    return ZX_OK;
}

static zx_status_t usb_dev_set_mode(usb_device_t* dev, const void* in_buf, size_t in_len) {
    if (in_len < sizeof(usb_mode_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    mtx_lock(&dev->lock);
    dev->usb_mode = *((usb_mode_t *)in_buf);
    zx_status_t status = usb_dev_state_changed_locked(dev);
    mtx_unlock(&dev->lock);
    return status;
}

static zx_status_t usb_dev_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                 void* out_buf, size_t out_len, size_t* out_actual) {
    zxlogf(TRACE, "usb_dev_ioctl %x\n", op);
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
    case IOCTL_USB_DEVICE_GET_MODE:
        return usb_dev_get_mode(dev, out_buf, out_len, out_actual);
    case IOCTL_USB_DEVICE_SET_MODE:
        return usb_dev_set_mode(dev, in_buf, in_len);
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void usb_dev_unbind(void* ctx) {
    zxlogf(TRACE, "usb_dev_unbind\n");
    usb_device_t* dev = ctx;
    usb_dev_clear_functions(dev);
    device_remove(dev->zxdev);
}

static void usb_dev_release(void* ctx) {
    zxlogf(TRACE, "usb_dev_release\n");
    usb_device_t* dev = ctx;
    free(dev->config_desc);
    for (unsigned i = 0; i < countof(dev->strings); i++) {
        free(dev->strings[i]);
    }
    free(dev);
}

static zx_protocol_device_t device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = usb_dev_ioctl,
    .unbind = usb_dev_unbind,
    .release = usb_dev_release,
};

#if defined(USB_DEVICE_VID) && defined(USB_DEVICE_PID) && defined(USB_DEVICE_FUNCTIONS)
static zx_status_t usb_dev_set_default_config(usb_device_t* dev) {
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

    zx_status_t status = ZX_OK;

#ifdef USB_DEVICE_MANUFACTURER
    status = usb_device_alloc_string_desc(dev, USB_DEVICE_MANUFACTURER, &device_desc.iManufacturer);
    if (status != ZX_OK) return status;
#endif
#ifdef USB_DEVICE_PRODUCT
    usb_device_alloc_string_desc(dev, USB_DEVICE_PRODUCT, &device_desc.iProduct);
    if (status != ZX_OK) return status;
#endif
#ifdef USB_DEVICE_SERIAL
    usb_device_alloc_string_desc(dev, USB_DEVICE_SERIAL, &device_desc.iSerialNumber);
    if (status != ZX_OK) return status;
#endif

    status = usb_dev_set_device_desc(dev, &device_desc, sizeof(device_desc));
    if (status != ZX_OK) return status;

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
        zxlogf(ERROR, "usb_dev_set_default_config: unknown function %s\n", USB_DEVICE_FUNCTIONS);
        return ZX_ERR_INVALID_ARGS;
    }

    status = usb_dev_add_function(dev, &function_desc, sizeof(function_desc));
    if (status != ZX_OK) return status;

    return usb_dev_bind_functions(dev);
}
#endif // defined(USB_DEVICE_VID) && defined(USB_DEVICE_PID) && defined(USB_DEVICE_FUNCTIONS)

zx_status_t usb_dev_bind(void* ctx, zx_device_t* parent) {
    zxlogf(INFO, "usb_dev_bind\n");

    usb_device_t* dev = calloc(1, sizeof(usb_device_t));
    if (!dev) {
        return ZX_ERR_NO_MEMORY;
    }
    list_initialize(&dev->functions);
    mtx_init(&dev->lock, mtx_plain);
    dev->dci_dev = parent;

    if (device_get_protocol(parent, ZX_PROTOCOL_USB_DCI, &dev->usb_dci)) {
        free(dev);
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = usb_dci_get_bti(&dev->usb_dci, &dev->bti_handle);
    if (status != ZX_OK) {
        free(dev);
        return status;
    }

    if (device_get_protocol(parent, ZX_PROTOCOL_USB_MODE_SWITCH, &dev->usb_mode_switch)) {
        free(dev);
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Starting USB mode is determined from device metadata.
    // We read initial value and store it in dev->usb_mode, but do not actually
    // enable it until after all of our functions have bound.
    size_t actual;
    status = device_get_metadata(parent, DEVICE_METADATA_USB_MODE,
                                 &dev->usb_mode, sizeof(dev->usb_mode), &actual);
    if (status != ZX_OK || actual != sizeof(dev->usb_mode)) {
        zxlogf(ERROR, "usb_dev_bind: DEVICE_METADATA_USB_MODE not found\n");
        free(dev);
        return status;
    }
    // Set DCI mode to USB_MODE_NONE until we are ready
    usb_mode_switch_set_mode(&dev->usb_mode_switch, USB_MODE_NONE);
    dev->dci_usb_mode = USB_MODE_NONE;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "usb-device",
        .ctx = dev,
        .ops = &device_proto,
        .proto_id = ZX_PROTOCOL_USB_DEVICE,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, &dev->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_device_bind add_device failed %d\n", status);
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

    return ZX_OK;
}

static zx_driver_ops_t usb_device_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_dev_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(usb_device, usb_device_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_USB_DCI),
ZIRCON_DRIVER_END(usb_device)
