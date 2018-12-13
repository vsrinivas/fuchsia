// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/usb/usb.h>
#include <ddk/metadata.h>
#include <zircon/hw/usb-audio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "usb-composite.h"
#include "usb-interface.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define NEXT_DESCRIPTOR(header) ((usb_descriptor_header_t*)((void*)header + header->bLength))

// returns whether the interface with the given id was removed.
static bool usb_composite_remove_interface_by_id_locked(usb_composite_t* comp,
                                                        uint8_t interface_id) {
    usb_interface_t* intf;
    usb_interface_t* tmp;

    list_for_every_entry_safe(&comp->children, intf, tmp, usb_interface_t, node) {
        if (usb_interface_contains_interface(intf, interface_id)) {
            list_delete(&intf->node);
            device_remove(intf->zxdev);
            return true;
        }
    }
    return false;
}

static zx_status_t usb_composite_add_interface(usb_composite_t* comp,
                                               usb_interface_descriptor_t* interface_desc,
                                               size_t interface_desc_length) {
    usb_device_descriptor_t* device_desc = &comp->device_desc;
    usb_interface_t* intf = calloc(1, sizeof(usb_interface_t));
    if (!intf) {
        free(interface_desc);
        return ZX_ERR_NO_MEMORY;
    }

    intf->comp = comp;
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

    zx_status_t status = usb_interface_configure_endpoints(intf, interface_desc->bInterfaceNumber,
                                                           0);
    if (status != ZX_OK) {
        free(interface_desc);
        free(intf);
        return status;
    }

    mtx_lock(&comp->interface_mutex);
    // need to do this first so usb_composite_set_interface() can be called from driver bind
    list_add_head(&comp->children, &intf->node);
    mtx_unlock(&comp->interface_mutex);

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
        .proto_ops = &usb_device_protocol,
        .props = props,
        .prop_count = countof(props),
    };

    status = device_add(comp->zxdev, &args, &intf->zxdev);
    if (status != ZX_OK) {
        list_delete(&intf->node);
        free(interface_desc);
        free(intf);
    }
    return status;
}

static zx_status_t usb_composite_add_interface_assoc(usb_composite_t* comp,
                                                     usb_interface_assoc_descriptor_t* assoc_desc,
                                                     size_t assoc_desc_length) {
    usb_device_descriptor_t* device_desc = &comp->device_desc;
    usb_interface_t* intf = calloc(1, sizeof(usb_interface_t));
    if (!intf) {
        return ZX_ERR_NO_MEMORY;
    }

    intf->comp = comp;
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
    usb_descriptor_header_t* end = (usb_descriptor_header_t*)((void*)header +
                                                              intf->descriptor_length);
    while (header < end) {
        if (header->bDescriptorType == USB_DT_INTERFACE) {
            usb_interface_descriptor_t* intf_desc = (usb_interface_descriptor_t*)header;
            if (intf_desc->bAlternateSetting == 0) {
                zx_status_t status = usb_interface_configure_endpoints(intf,
                                                                       intf_desc->bInterfaceNumber,
                                                                       0);
                if (status != ZX_OK) {
                    return status;
                }
            }
        }
        header = NEXT_DESCRIPTOR(header);
    }

    mtx_lock(&comp->interface_mutex);
    // need to do this first so usb_composite_set_interface() can be called from driver bind
    list_add_head(&comp->children, &intf->node);
    mtx_unlock(&comp->interface_mutex);

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
        .proto_ops = &usb_device_protocol,
        .props = props,
        .prop_count = countof(props),
    };

    zx_status_t status = device_add(comp->zxdev, &args, &intf->zxdev);
    if (status != ZX_OK) {
        list_delete(&intf->node);
        free(assoc_desc);
        free(intf);
    }
    return status;
}

static zx_status_t usb_composite_add_interfaces(usb_composite_t* comp) {
    usb_configuration_descriptor_t* config = comp->config_desc;

    comp->interface_statuses = calloc(config->bNumInterfaces, sizeof(interface_status_t));
    if (!comp->interface_statuses) {
        return ZX_ERR_NO_MEMORY;
    }

    // Iterate through interfaces in first configuration and create devices for them
    usb_descriptor_header_t* header = NEXT_DESCRIPTOR(config);
    usb_descriptor_header_t* end = (usb_descriptor_header_t*)((void*)config +
                                                                le16toh(config->wTotalLength));

    zx_status_t result = ZX_OK;

    while (header < end) {
        if (header->bDescriptorType == USB_DT_INTERFACE_ASSOCIATION) {
            usb_interface_assoc_descriptor_t* assoc_desc =
                                                    (usb_interface_assoc_descriptor_t*)header;
            int interface_count = assoc_desc->bInterfaceCount;

            // find end of this interface association
            usb_descriptor_header_t* next = NEXT_DESCRIPTOR(assoc_desc);
            while (next < end) {
                if (next->bDescriptorType == USB_DT_INTERFACE_ASSOCIATION) {
                    break;
                } else if (next->bDescriptorType == USB_DT_INTERFACE) {
                    usb_interface_descriptor_t* test_intf = (usb_interface_descriptor_t*)next;

                    if (test_intf->bAlternateSetting == 0) {
                        if (interface_count == 0) {
                            break;
                        }
                        interface_count--;
                    }
                }
                next = NEXT_DESCRIPTOR(next);
            }

            size_t length = (void *)next - (void *)assoc_desc;
            usb_interface_assoc_descriptor_t* assoc_copy = malloc(length);
            if (!assoc_copy) return ZX_ERR_NO_MEMORY;
            memcpy(assoc_copy, assoc_desc, length);

            zx_status_t status = usb_composite_add_interface_assoc(comp, assoc_copy, length);
            if (status != ZX_OK) {
                result = status;
            }

            header = next;
        } else if (header->bDescriptorType == USB_DT_INTERFACE) {
            usb_interface_descriptor_t* intf_desc = (usb_interface_descriptor_t*)header;
            // find end of current interface descriptor
            usb_descriptor_header_t* next = NEXT_DESCRIPTOR(intf_desc);
            while (next < end) {
                if (next->bDescriptorType == USB_DT_INTERFACE) {
                    usb_interface_descriptor_t* test_intf = (usb_interface_descriptor_t*)next;
                    // Iterate until we find the next top-level interface
                    // Include alternate interfaces in the current interface
                    if (test_intf->bAlternateSetting == 0) {
                        break;
                    }
                }
                next = NEXT_DESCRIPTOR(next);
            }

            // Only create a child device if no child interface has claimed this interface.
            mtx_lock(&comp->interface_mutex);
            interface_status_t intf_status = comp->interface_statuses[intf_desc->bInterfaceNumber];
            mtx_unlock(&comp->interface_mutex);

            size_t length = (void *)next - (void *)intf_desc;
            if (intf_status == AVAILABLE) {
                usb_interface_descriptor_t* intf_copy = malloc(length);
                if (!intf_copy) return ZX_ERR_NO_MEMORY;
                memcpy(intf_copy, intf_desc, length);
                zx_status_t status = usb_composite_add_interface(comp, intf_copy, length);
                if (status != ZX_OK) {
                    result = status;
                }
                // The interface may have been claimed in the meanwhile, so we need to
                // check the interface status again.
                mtx_lock(&comp->interface_mutex);
                if (comp->interface_statuses[intf_desc->bInterfaceNumber] == CLAIMED) {
                    bool removed = usb_composite_remove_interface_by_id_locked(comp,
                                                                    intf_desc->bInterfaceNumber);
                    if (!removed) {
                        mtx_unlock(&comp->interface_mutex);
                        return ZX_ERR_BAD_STATE;
                    }
                } else {
                    comp->interface_statuses[intf_desc->bInterfaceNumber] = CHILD_DEVICE;
                }
                mtx_unlock(&comp->interface_mutex);
            }
            header = next;
        } else {
            header = NEXT_DESCRIPTOR(header);
        }
    }

    return result;
}

static void usb_composite_remove_interfaces(usb_composite_t* comp) {
    mtx_lock(&comp->interface_mutex);

    usb_interface_t* intf;
    while ((intf = list_remove_head_type(&comp->children, usb_interface_t, node)) != NULL) {
        device_remove(intf->zxdev);
    }
    free(comp->interface_statuses);
    comp->interface_statuses = NULL;

    mtx_unlock(&comp->interface_mutex);
}

zx_status_t usb_composite_do_claim_interface(usb_composite_t* comp, uint8_t interface_id) {
    mtx_lock(&comp->interface_mutex);

    interface_status_t status = comp->interface_statuses[interface_id];
    if (status == CLAIMED) {
        // The interface has already been claimed by a different interface.
        mtx_unlock(&comp->interface_mutex);
        return ZX_ERR_ALREADY_BOUND;
    } else if (status == CHILD_DEVICE) {
        bool removed = usb_composite_remove_interface_by_id_locked(comp, interface_id);
        if (!removed) {
            mtx_unlock(&comp->interface_mutex);
            return ZX_ERR_BAD_STATE;
        }
    }
    comp->interface_statuses[interface_id] = CLAIMED;

    mtx_unlock(&comp->interface_mutex);

    return ZX_OK;
}

zx_status_t usb_composite_set_interface(usb_composite_t* comp, uint8_t interface_id,
                                        uint8_t alt_setting) {
    mtx_lock(&comp->interface_mutex);
    usb_interface_t* intf;
    list_for_every_entry(&comp->children, intf, usb_interface_t, node) {
        if (usb_interface_contains_interface(intf, interface_id)) {
            mtx_unlock(&comp->interface_mutex);
            return usb_interface_set_alt_setting(intf, interface_id, alt_setting);
        }
    }
    mtx_unlock(&comp->interface_mutex);
    return ZX_ERR_INVALID_ARGS;
}

static void usb_composite_unbind(void* ctx) {
    usb_composite_t* comp = ctx;
    usb_composite_remove_interfaces(comp);
    device_remove(comp->zxdev);
}

static void usb_composite_release(void* ctx) {
    usb_composite_t* comp = ctx;

    free(comp->config_desc);
    free(comp->interface_statuses);
    free(comp);
}

static zx_protocol_device_t usb_composite_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = usb_composite_unbind,
    .release = usb_composite_release,
};

static zx_status_t usb_composite_bind(void* ctx, zx_device_t* parent) {
    usb_protocol_t usb;
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_USB, &usb);
    if (status != ZX_OK) {
        return status;
    }

    usb_composite_t* comp = calloc(1, sizeof(usb_composite_t));
    if (!comp) {
        return ZX_ERR_NO_MEMORY;
    }
    memcpy(&comp->usb, &usb, sizeof(comp->usb));

    list_initialize(&comp->children);

    mtx_init(&comp->interface_mutex, mtx_plain);

    usb_get_device_descriptor(&usb, &comp->device_desc);

    size_t config_length;
    status = usb_get_configuration_descriptor(&comp->usb, usb_get_configuration(&comp->usb),
                                              &comp->config_desc, &config_length);
    if (status != ZX_OK) {
        goto error_exit;
    }

    char name[16];
    snprintf(name, sizeof(name), "%03d", usb_get_device_id(&usb));

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = comp,
        .ops = &usb_composite_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, &comp->zxdev);
    if (status == ZX_OK) {
        return usb_composite_add_interfaces(comp);
    }

error_exit:
    free(comp->config_desc);
    free(comp);
    return status;
}

static zx_driver_ops_t usb_composite_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_composite_bind,
};

// The '*' in the version string is important. This marks this driver as a fallback,
// to allow other drivers to bind against ZX_PROTOCOL_USB_DEVICE to handle more specific cases.
ZIRCON_DRIVER_BEGIN(usb_composite, usb_composite_driver_ops, "zircon", "*0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_USB_DEVICE),
ZIRCON_DRIVER_END(usb_composite)
