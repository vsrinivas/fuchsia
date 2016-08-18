// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/completion.h>
#include <ddk/common/usb.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb-device.h>
#include <hw/usb-hub.h>
#include <runtime/thread.h>
#include <stdio.h>
#include <stdlib.h>
#include <system/listnode.h>

//#define TRACE 1
#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

typedef struct usb_hub {
    // the device we are publishing
    mx_device_t device;

    // Underlying USB device
    mx_device_t* usb_device;
    usb_device_protocol_t* device_protocol;

    usb_speed_t hub_speed;
    int num_ports;

    usb_request_t* status_request;
    completion_t completion;
} usb_hub_t;
#define get_hub(dev) containerof(dev, usb_hub_t, device)

static mx_status_t queue_status_request(usb_hub_t* hub) {
    usb_request_t* request = hub->status_request;
    request->transfer_length = request->buffer_length;
    return hub->device_protocol->queue_request(hub->usb_device, request);
}

static void free_interrupt_request(usb_hub_t* hub, usb_request_t* request) {
    hub->device_protocol->free_request(hub->usb_device, request);
}

static mx_status_t usb_hub_get_port_status(usb_hub_t* hub, int port, usb_port_status_t* status) {
    mx_status_t result = usb_get_status(hub->usb_device, USB_RECIP_PORT, port, status, sizeof(*status));
    if (result == sizeof(*status)) {
        return NO_ERROR;
    } else {
        return -1;
    }
}

static usb_speed_t usb_hub_get_port_speed(usb_hub_t* hub, int port) {
    if (hub->hub_speed == USB_SPEED_SUPER)
        return USB_SPEED_SUPER;

    usb_port_status_t status;
    if (usb_hub_get_port_status(hub, port, &status) == NO_ERROR) {
        if (status.wPortStatus & USB_PORT_LOW_SPEED)
            return USB_SPEED_LOW;
        if (status.wPortStatus & USB_PORT_HIGH_SPEED)
            return USB_SPEED_HIGH;
        return USB_SPEED_FULL;
    } else {
        return USB_SPEED_UNDEFINED;
    }
}

static void usb_hub_interrupt_complete(usb_request_t* request) {
    xprintf("usb_hub_interrupt_complete got %d %d\n", request->status, request->transfer_length);
    usb_hub_t* hub = (usb_hub_t*)request->client_data;
    completion_signal(&hub->completion);
}

static void usb_hub_enable_port(usb_hub_t* hub, int port) {
    usb_set_feature(hub->usb_device, USB_RECIP_PORT, USB_FEATURE_PORT_POWER, port);
}

static void usb_hub_port_connected(usb_hub_t* hub, int port) {
    xprintf("port %d usb_hub_port_connected\n", port);
    usb_set_feature(hub->usb_device, USB_RECIP_PORT, USB_FEATURE_PORT_RESET, port);
}

static void usb_hub_port_disconnected(usb_hub_t* hub, int port) {
    xprintf("port %d usb_hub_port_disconnected\n", port);
    hub->device_protocol->hub_device_removed(hub->usb_device, port);
}

static void usb_hub_port_reset(usb_hub_t* hub, int port) {
    xprintf("port %d usb_hub_port_reset\n", port);
    usb_speed_t speed = usb_hub_get_port_speed(hub, port);
    if (speed != USB_SPEED_UNDEFINED) {
        xprintf("calling hub_device_added(%d %d)\n", port, speed);
        hub->device_protocol->hub_device_added(hub->usb_device, port, speed);
    }
}

static void usb_hub_handle_port_status(usb_hub_t* hub, int port, usb_port_status_t* status) {
    if (status->wPortChange & USB_PORT_CONNECTION) {
        if (status->wPortStatus & USB_PORT_CONNECTION) {
            usb_hub_port_connected(hub, port);
        } else {
            usb_hub_port_disconnected(hub, port);
        }
        usb_clear_feature(hub->usb_device, USB_RECIP_PORT, USB_FEATURE_C_PORT_CONNECTION, port);
    }
    if (status->wPortChange & USB_PORT_ENABLE) {
        xprintf("USB_PORT_ENABLE\n");
        usb_clear_feature(hub->usb_device, USB_RECIP_PORT, USB_FEATURE_C_PORT_ENABLE, port);
    }
    if (status->wPortChange & USB_PORT_SUSPEND) {
        xprintf("USB_PORT_SUSPEND\n");
        usb_clear_feature(hub->usb_device, USB_RECIP_PORT, USB_FEATURE_C_PORT_SUSPEND, port);
    }
    if (status->wPortChange & USB_PORT_OVER_CURRENT) {
        xprintf("USB_PORT_OVER_CURRENT\n");
        usb_clear_feature(hub->usb_device, USB_RECIP_PORT, USB_FEATURE_C_PORT_OVER_CURRENT, port);
    }
    if (status->wPortChange & USB_PORT_RESET) {
        if (!(status->wPortStatus & USB_PORT_RESET)) {
            usb_hub_port_reset(hub, port);
        }
        usb_clear_feature(hub->usb_device, USB_RECIP_PORT, USB_FEATURE_C_PORT_RESET, port);
    }
}

static mx_status_t usb_hub_release(mx_device_t* device) {
    usb_hub_t* hub = get_hub(device);
    hub->device_protocol->free_request(hub->usb_device, hub->status_request);
    free(hub);
    return NO_ERROR;
}

static mx_protocol_device_t usb_hub_device_proto = {
    .release = usb_hub_release,
};

static int usb_hub_thread(void* arg) {
    usb_hub_t* hub = (usb_hub_t*)arg;
    usb_request_t* request = hub->status_request;

    usb_hub_descriptor_t desc;
    int desc_type = (hub->hub_speed == USB_SPEED_SUPER ? USB_HUB_DESC_TYPE_SS : USB_HUB_DESC_TYPE);
    mx_status_t result = usb_get_descriptor(hub->usb_device, USB_TYPE_CLASS | USB_RECIP_DEVICE,
                                            desc_type, 0, &desc, sizeof(desc));
    if (result < 0) {
        printf("get hub descriptor failed: %d\n", result);
        return result;
    }

    result = hub->device_protocol->configure_hub(hub->usb_device, hub->hub_speed, &desc);
    if (result < 0) {
        printf("configure_hub failed: %d\n", result);
        return result;
    }

    int num_ports = desc.bNbrPorts;

    for (int i = 1; i <= num_ports; i++) {
        usb_hub_enable_port(hub, i);
    }

    device_set_bindable(&hub->device, false);
    result = device_add(&hub->device, hub->usb_device);
    if (result != NO_ERROR) {
        usb_hub_release(&hub->device);
        return result;
    }

    queue_status_request(hub);

    // This loop handles events from our interrupt endpoint
    while (1) {
        completion_wait(&hub->completion, MX_TIME_INFINITE);
        if (request->status != NO_ERROR) {
            break;
        }

        uint8_t* bitmap = request->buffer;
        uint8_t* bitmap_end = bitmap + request->transfer_length;

        // bit zero is hub status
        if (bitmap[0] & 1) {
            // what to do here?
            printf("usb_hub_interrupt_complete hub status changed\n");
        }

        int port = 1;
        int bit = 1;
        while (bitmap < bitmap_end && port <= num_ports) {
            if (*bitmap & (1 << bit)) {
                usb_port_status_t status;
                mx_status_t result = usb_hub_get_port_status(hub, port, &status);
                if (result == NO_ERROR) {
                    usb_hub_handle_port_status(hub, port, &status);
                }
            }
            port++;
            if (++bit == 8) {
                bitmap++;
                bit = 0;
            }
        }

        completion_reset(&hub->completion);
        queue_status_request(hub);
    }

    return NO_ERROR;
}

static mx_status_t usb_hub_bind(mx_driver_t* driver, mx_device_t* device) {
    usb_device_protocol_t* device_protocol = NULL;
    usb_request_t* req = NULL;

    if (device_get_protocol(device, MX_PROTOCOL_USB_DEVICE, (void**)&device_protocol)) {
        return ERR_NOT_SUPPORTED;
    }

    usb_device_config_t* device_config;
    mx_status_t status = device_protocol->get_config(device, &device_config);
    if (status < 0)
        return status;

    // find our interrupt endpoint
    usb_configuration_t* config = &device_config->configurations[0];
    usb_interface_t* intf = &config->interfaces[0];
    if (intf->num_endpoints != 1) {
        printf("usb_hub_bind wrong number of endpoints: %d\n", intf->num_endpoints);
        return ERR_NOT_SUPPORTED;
    }
    usb_endpoint_t* endp = &intf->endpoints[0];
    if (endp->type != USB_ENDPOINT_INTERRUPT) {
        return ERR_NOT_SUPPORTED;
    }

    usb_hub_t* hub = calloc(1, sizeof(usb_hub_t));
    if (!hub) {
        printf("Not enough memory for usb_hub_t.\n");
        return ERR_NO_MEMORY;
    }

    status = device_init(&hub->device, driver, "usb-hub", &usb_hub_device_proto);
    if (status != NO_ERROR) {
        goto fail;
    }

    hub->usb_device = device;
    hub->device_protocol = device_protocol;
    hub->hub_speed = device_protocol->get_speed(device);

    req = device_protocol->alloc_request(device, endp, endp->maxpacketsize);
    if (!req) {
        status = ERR_NO_MEMORY;
        goto fail;
    }
    req->complete_cb = usb_hub_interrupt_complete;
    req->client_data = hub;
    hub->status_request = req;

    mxr_thread_t* thread;
    status = mxr_thread_create(usb_hub_thread, hub, "usb_hub_thread", &thread);
    if (status != NO_ERROR) {
        goto fail;
    }
    mxr_thread_detach(thread);
    return NO_ERROR;

fail:
    if (req) {
        device_protocol->free_request(device, req);
    }
    free(hub);
    return status;
}

static mx_status_t usb_hub_unbind(mx_driver_t* drv, mx_device_t* dev) {
    //TODO: should avoid using dev->childern
    mx_device_t* child = NULL;
    mx_device_t* temp = NULL;
    list_for_every_entry_safe (&dev->children, child, temp, mx_device_t, node) {
        device_remove(child);
    }
    return NO_ERROR;
}

static mx_bind_inst_t binding[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB_DEVICE),
    BI_MATCH_IF(EQ, BIND_USB_CLASS, USB_CLASS_HUB),
};

mx_driver_t _driver_usb_hub BUILTIN_DRIVER = {
    .name = "usb-hub",
    .ops = {
        .bind = usb_hub_bind,
        .unbind = usb_hub_unbind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};
