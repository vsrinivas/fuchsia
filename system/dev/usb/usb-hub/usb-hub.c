// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb.h>
#include <ddk/protocol/usb-bus.h>
#include <ddk/protocol/usb-hub.h>
#include <ddk/usb/usb.h>
#include <ddk/usb-request/usb-request.h>
#include <zircon/hw/usb-hub.h>
#include <lib/sync/completion.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/listnode.h>
#include <threads.h>
#include <unistd.h>

// usb_port_status_t.wPortStatus
typedef uint16_t port_status_t;

typedef struct usb_hub {
    // the device we are publishing
    zx_device_t* zxdev;

    // Underlying USB device
    zx_device_t* usb_device;
    usb_protocol_t usb;

    zx_device_t* bus_device;
    usb_bus_protocol_t bus;

    usb_speed_t hub_speed;
    int num_ports;
    // delay after port power in microseconds
    zx_time_t power_on_delay;

    usb_request_t* status_request;
    sync_completion_t completion;

    thrd_t thread;
    atomic_bool thread_done;

    // port status values for our ports
    // length is num_ports
    port_status_t* port_status;

    // bit field indicating which ports have devices attached
    uint8_t attached_ports[128 / 8];
} usb_hub_t;

bool usb_hub_is_port_attached(usb_hub_t* hub, int port) {
    return (hub->attached_ports[port / 8] & (1 << (port % 8))) != 0;
}

void usb_hub_set_port_attached(usb_hub_t* hub, int port, bool enabled) {
    if (enabled) {
        hub->attached_ports[port / 8] |= (1 << (port % 8));
    } else {
        hub->attached_ports[port / 8] &= ~(1 << (port % 8));
    }
}

static zx_status_t usb_hub_get_port_status(usb_hub_t* hub, int port, port_status_t* out_status) {
    usb_port_status_t status;

    size_t out_length;
    zx_status_t result = usb_get_status(&hub->usb, USB_RECIP_PORT, port, &status, sizeof(status),
                                        ZX_TIME_INFINITE, &out_length);
    if (result != ZX_OK) {
        return result;
    }
    if (out_length != sizeof(status)) {
        return ZX_ERR_BAD_STATE;
    }

    zxlogf(TRACE, "usb_hub_get_port_status port %d ", port);

    uint16_t port_change = status.wPortChange;
    if (port_change & USB_C_PORT_CONNECTION) {
        zxlogf(TRACE, "USB_C_PORT_CONNECTION ");
        usb_clear_feature(&hub->usb, USB_RECIP_PORT, USB_FEATURE_C_PORT_CONNECTION, port,
                          ZX_TIME_INFINITE);
    }
    if (port_change & USB_C_PORT_ENABLE) {
        zxlogf(TRACE, "USB_C_PORT_ENABLE ");
        usb_clear_feature(&hub->usb, USB_RECIP_PORT, USB_FEATURE_C_PORT_ENABLE, port,
                          ZX_TIME_INFINITE);
    }
    if (port_change & USB_C_PORT_SUSPEND) {
        zxlogf(TRACE, "USB_C_PORT_SUSPEND ");
        usb_clear_feature(&hub->usb, USB_RECIP_PORT, USB_FEATURE_C_PORT_SUSPEND, port,
                          ZX_TIME_INFINITE);
    }
    if (port_change & USB_C_PORT_OVER_CURRENT) {
        zxlogf(TRACE, "USB_C_PORT_OVER_CURRENT ");
        usb_clear_feature(&hub->usb, USB_RECIP_PORT, USB_FEATURE_C_PORT_OVER_CURRENT, port,
                          ZX_TIME_INFINITE);
    }
    if (port_change & USB_C_PORT_RESET) {
        zxlogf(TRACE, "USB_C_PORT_RESET");
        usb_clear_feature(&hub->usb, USB_RECIP_PORT, USB_FEATURE_C_PORT_RESET, port,
                          ZX_TIME_INFINITE);
    }
    if (port_change & USB_C_BH_PORT_RESET) {
        zxlogf(TRACE, "USB_C_BH_PORT_RESET");
        usb_clear_feature(&hub->usb, USB_RECIP_PORT, USB_FEATURE_C_BH_PORT_RESET, port,
                          ZX_TIME_INFINITE);
    }
    if (port_change & USB_C_PORT_LINK_STATE) {
        zxlogf(TRACE, "USB_C_PORT_LINK_STATE");
        usb_clear_feature(&hub->usb, USB_RECIP_PORT, USB_FEATURE_C_PORT_LINK_STATE, port,
                          ZX_TIME_INFINITE);
    }
    if (port_change & USB_C_PORT_CONFIG_ERROR) {
        zxlogf(TRACE, "USB_C_PORT_CONFIG_ERROR");
        usb_clear_feature(&hub->usb, USB_RECIP_PORT, USB_FEATURE_C_PORT_CONFIG_ERROR, port,
                          ZX_TIME_INFINITE);
    }
    zxlogf(TRACE, "\n");

    *out_status = status.wPortStatus;
    return ZX_OK;
}

static zx_status_t usb_hub_wait_for_port(usb_hub_t* hub, int port, port_status_t* out_status,
                                         port_status_t status_bits, port_status_t status_mask,
                                         zx_time_t stable_time) {
    const zx_time_t timeout = ZX_SEC(2);        // 2 second total timeout
    const zx_time_t poll_delay = ZX_MSEC(25);   // poll every 25 milliseconds
    zx_time_t total = 0;
    zx_time_t stable = 0;

    while (total < timeout) {
        zx_nanosleep(zx_deadline_after(poll_delay));
        total += poll_delay;

        zx_status_t result = usb_hub_get_port_status(hub, port, out_status);
        if (result != ZX_OK) {
            return result;
        }
        hub->port_status[port] = *out_status;

        if ((*out_status & status_mask) == status_bits) {
            stable += poll_delay;
            if (stable >= stable_time) {
                return ZX_OK;
            }
        } else {
            stable = 0;
        }
    }

    return ZX_ERR_TIMED_OUT;
}

static void usb_hub_interrupt_complete(usb_request_t* req, void* cookie) {
    zxlogf(TRACE, "usb_hub_interrupt_complete got %d %" PRIu64 "\n", req->response.status, req->response.actual);
    usb_hub_t* hub = (usb_hub_t*)cookie;
    sync_completion_signal(&hub->completion);
}

static void usb_hub_power_on_port(usb_hub_t* hub, int port) {
    usb_set_feature(&hub->usb, USB_RECIP_PORT, USB_FEATURE_PORT_POWER, port, ZX_TIME_INFINITE);
    usleep(hub->power_on_delay);
}

static void usb_hub_port_enabled(usb_hub_t* hub, int port) {
    port_status_t status;

    zxlogf(TRACE, "port %d usb_hub_port_enabled\n", port);

    // USB 2.0 spec section 9.1.2 recommends 100ms delay before enumerating
    // wait for USB_PORT_ENABLE == 1 and USB_PORT_RESET == 0
    if (usb_hub_wait_for_port(hub, port, &status, USB_PORT_ENABLE, USB_PORT_ENABLE | USB_PORT_RESET,
                              ZX_MSEC(100)) != ZX_OK) {
        zxlogf(ERROR, "usb_hub_wait_for_port USB_PORT_RESET failed for USB hub, port %d\n", port);
        return;
    }

    usb_speed_t speed;
    if (hub->hub_speed == USB_SPEED_SUPER) {
        speed = USB_SPEED_SUPER;
    } else if (status & USB_PORT_LOW_SPEED) {
        speed = USB_SPEED_LOW;
    } else if (status & USB_PORT_HIGH_SPEED) {
        speed = USB_SPEED_HIGH;
    } else {
        speed = USB_SPEED_FULL;
    }

    zxlogf(TRACE, "call hub_device_added for port %d\n", port);
    usb_bus_hub_device_added(&hub->bus, hub->usb_device, port, speed);
    usb_hub_set_port_attached(hub, port, true);
}

static void usb_hub_port_connected(usb_hub_t* hub, int port) {
    port_status_t status;

    zxlogf(TRACE, "port %d usb_hub_port_connected\n", port);

    // USB 2.0 spec section 7.1.7.3 recommends 100ms between connect and reset
    if (usb_hub_wait_for_port(hub, port, &status, USB_PORT_CONNECTION, USB_PORT_CONNECTION,
                              ZX_MSEC(100)) != ZX_OK) {
        zxlogf(ERROR, "usb_hub_wait_for_port USB_PORT_CONNECTION failed for USB hub, port %d\n", port);
        return;
    }

    usb_set_feature(&hub->usb, USB_RECIP_PORT, USB_FEATURE_PORT_RESET, port, ZX_TIME_INFINITE);
    usb_hub_port_enabled(hub, port);
}

static void usb_hub_port_reset(void* ctx, uint32_t port) {
    port_status_t status;
    usb_hub_t* hub = ctx;

    usb_set_feature(&hub->usb, USB_RECIP_PORT, USB_FEATURE_PORT_RESET, port, ZX_TIME_INFINITE);
    if (usb_hub_wait_for_port(hub, port, &status, USB_PORT_ENABLE, USB_PORT_ENABLE | USB_PORT_RESET,
                              ZX_MSEC(100)) != ZX_OK) {
        zxlogf(ERROR, "usb_hub_wait_for_port USB_PORT_RESET failed for USB hub, port %d\n", port);
    }
}

static usb_hub_interface_ops_t _hub_interface = {
    .reset_port = usb_hub_port_reset,
};

static void usb_hub_port_disconnected(usb_hub_t* hub, int port) {
    zxlogf(TRACE, "port %d usb_hub_port_disconnected\n", port);
    usb_bus_hub_device_removed(&hub->bus, hub->usb_device, port);
    usb_hub_set_port_attached(hub, port, false);
}

static void usb_hub_handle_port_status(usb_hub_t* hub, int port, port_status_t status) {
    port_status_t old_status = hub->port_status[port];

    zxlogf(TRACE, "usb_hub_handle_port_status port: %d status: %04X old_status: %04X\n", port, status,
            old_status);

    hub->port_status[port] = status;

    if ((status & USB_PORT_CONNECTION) && !(status & USB_PORT_ENABLE)) {
        // Handle race condition where device is quickly disconnected and reconnected.
        // This happens when Android devices switch USB configurations.
        // In this case, any change to the connect state should trigger a disconnect
        // before handling a connect event.
        if (usb_hub_is_port_attached(hub, port)) {
            usb_hub_port_disconnected(hub, port);
            old_status &= ~USB_PORT_CONNECTION;
        }
    }
    if ((status & USB_PORT_CONNECTION) && !(old_status & USB_PORT_CONNECTION)) {
        usb_hub_port_connected(hub, port);
    } else if (!(status & USB_PORT_CONNECTION) && (old_status & USB_PORT_CONNECTION)) {
        usb_hub_port_disconnected(hub, port);
    } else if ((status & USB_PORT_ENABLE) && !(old_status & USB_PORT_ENABLE)) {
        usb_hub_port_enabled(hub, port);
    }
}

static void usb_hub_unbind(void* ctx) {
    usb_hub_t* hub = ctx;
    for (int port = 1; port <= hub->num_ports; port++) {
        if (usb_hub_is_port_attached(hub, port)) {
            usb_hub_port_disconnected(hub, port);
        }
    }

    atomic_store(&hub->thread_done, true);
    sync_completion_signal(&hub->completion);
    thrd_join(hub->thread, NULL);

    device_remove(hub->zxdev);
}

static zx_status_t usb_hub_free(usb_hub_t* hub) {
    usb_request_release(hub->status_request);
    free(hub->port_status);
    free(hub);
    return ZX_OK;
}

static void usb_hub_release(void* ctx) {
    usb_hub_t* hub = ctx;
    usb_hub_free(hub);
}

static zx_protocol_device_t usb_hub_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = usb_hub_unbind,
    .release = usb_hub_release,
};

static int usb_hub_thread(void* arg) {
    usb_hub_t* hub = (usb_hub_t*)arg;
    usb_request_t* req = hub->status_request;

    usb_hub_descriptor_t desc;
    size_t out_length;
    int desc_type = (hub->hub_speed == USB_SPEED_SUPER ? USB_HUB_DESC_TYPE_SS : USB_HUB_DESC_TYPE);
    zx_status_t result = usb_get_descriptor(&hub->usb, USB_TYPE_CLASS | USB_RECIP_DEVICE,
                                            desc_type, 0, &desc, sizeof(desc), ZX_TIME_INFINITE,
                                            &out_length);
    if (result < 0) {
        zxlogf(ERROR, "get hub descriptor failed: %d\n", result);
        goto fail;
    }
    // The length of the descriptor varies depending on whether it is USB 2.0 or 3.0,
    // and how many ports it has.
    size_t min_length = 7;
    size_t max_length = sizeof(desc);
    if (out_length < min_length || out_length > max_length) {
        zxlogf(ERROR, "get hub descriptor got length %lu, want length between %lu and %lu\n",
                out_length, min_length, max_length);
        result = ZX_ERR_BAD_STATE;
        goto fail;
    }

    result = usb_bus_configure_hub(&hub->bus, hub->usb_device, hub->hub_speed, &desc);
    if (result < 0) {
        zxlogf(ERROR, "configure_hub failed: %d\n", result);
        goto fail;
    }

    int num_ports = desc.bNbrPorts;
    hub->num_ports = num_ports;
    hub->port_status = calloc(num_ports + 1, sizeof(port_status_t));
    if (!hub->port_status) {
        result = ZX_ERR_NO_MEMORY;
        goto fail;
    }

    // power on delay in microseconds
    hub->power_on_delay = desc.bPowerOn2PwrGood * 2 * 1000;
    if (hub->power_on_delay < 100 * 1000) {
        // USB 2.0 spec section 9.1.2 recommends atleast 100ms delay after power on
        hub->power_on_delay = 100 * 1000;
    }

    for (int i = 1; i <= num_ports; i++) {
        usb_hub_power_on_port(hub, i);
    }

    device_make_visible(hub->zxdev);

    // bit field for port status bits
    uint8_t status_buf[128 / 8];
    memset(status_buf, 0, sizeof(status_buf));

    // This loop handles events from our interrupt endpoint
    while (1) {
        sync_completion_reset(&hub->completion);
        usb_request_queue(&hub->usb, req);
        sync_completion_wait(&hub->completion, ZX_TIME_INFINITE);
        if (req->response.status != ZX_OK || atomic_load(&hub->thread_done)) {
            break;
        }

        usb_request_copyfrom(req, status_buf, req->response.actual, 0);
        uint8_t* bitmap = status_buf;
        uint8_t* bitmap_end = bitmap + req->response.actual;

        // bit zero is hub status
        if (bitmap[0] & 1) {
            // what to do here?
            zxlogf(ERROR, "usb_hub_interrupt_complete hub status changed\n");
        }

        int port = 1;
        int bit = 1;
        while (bitmap < bitmap_end && port <= num_ports) {
            if (*bitmap & (1 << bit)) {
                port_status_t status;
                zx_status_t result = usb_hub_get_port_status(hub, port, &status);
                if (result == ZX_OK) {
                    usb_hub_handle_port_status(hub, port, status);
                }
            }
            port++;
            if (++bit == 8) {
                bitmap++;
                bit = 0;
            }
        }
    }

    return ZX_OK;

fail:
    device_remove(hub->zxdev);
    return result;
}

static zx_status_t usb_hub_bind(void* ctx, zx_device_t* device) {
    usb_protocol_t usb;
    zx_status_t status = device_get_protocol(device, ZX_PROTOCOL_USB, &usb);
    if (status != ZX_OK) {
        return status;
    }

    // search for the bus device
    zx_device_t* bus_device = device_get_parent(device);
    usb_bus_protocol_t bus = { NULL, NULL };
    while (bus_device != NULL && bus.ops == NULL) {
        if (device_get_protocol(bus_device, ZX_PROTOCOL_USB_BUS, &bus) == ZX_OK) {
            break;
        }
        bus_device = device_get_parent(bus_device);
    }
    if (!bus_device || !bus.ops) {
        zxlogf(ERROR, "usb_hub_bind could not find bus device\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    // find our interrupt endpoint
    usb_desc_iter_t iter;
    status = usb_desc_iter_init(&usb, &iter);
    if (status < 0) return status;

    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
    if (!intf || intf->bNumEndpoints != 1) {
        usb_desc_iter_release(&iter);
        return ZX_ERR_NOT_SUPPORTED;
    }

    uint8_t ep_addr = 0;
    uint16_t max_packet_size = 0;
    usb_endpoint_descriptor_t* endp = usb_desc_iter_next_endpoint(&iter);
    if (endp && usb_ep_type(endp) == USB_ENDPOINT_INTERRUPT) {
        ep_addr = endp->bEndpointAddress;
        max_packet_size = usb_ep_max_packet(endp);
    }
    usb_desc_iter_release(&iter);

    if (!ep_addr) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    usb_hub_t* hub = calloc(1, sizeof(usb_hub_t));
    if (!hub) {
        zxlogf(ERROR, "Not enough memory for usb_hub_t.\n");
        return ZX_ERR_NO_MEMORY;
    }
    atomic_init(&hub->thread_done, false);

    hub->usb_device = device;
    hub->hub_speed = usb_get_speed(&usb);
    hub->bus_device = bus_device;
    memcpy(&hub->usb, &usb, sizeof(usb_protocol_t));
    memcpy(&hub->bus, &bus, sizeof(usb_bus_protocol_t));

    usb_request_t* req;
    status = usb_req_alloc(&usb, &req, max_packet_size, ep_addr);
    if (status != ZX_OK) {
        usb_hub_free(hub);
        return status;
    }
    req->complete_cb = usb_hub_interrupt_complete;
    req->cookie = hub;
    hub->status_request = req;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "usb-hub",
        .ctx = hub,
        .ops = &usb_hub_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE | DEVICE_ADD_INVISIBLE,
    };

    status = device_add(hub->usb_device, &args, &hub->zxdev);
    if (status != ZX_OK) {
        usb_hub_free(hub);
        return status;
    }

    static usb_hub_interface_t hub_intf;
    hub_intf.ops = &_hub_interface;
    hub_intf.ctx = hub;
    usb_bus_set_hub_interface(&bus, hub->usb_device, &hub_intf);

    int ret = thrd_create_with_name(&hub->thread, usb_hub_thread, hub, "usb_hub_thread");
    if (ret != thrd_success) {
        device_remove(hub->zxdev);
        return ZX_ERR_NO_MEMORY;
    }

    return ZX_OK;
}

static zx_driver_ops_t usb_hub_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_hub_bind,
};

ZIRCON_DRIVER_BEGIN(usb_hub, usb_hub_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_MATCH_IF(EQ, BIND_USB_CLASS, USB_CLASS_HUB),
ZIRCON_DRIVER_END(usb_hub)
