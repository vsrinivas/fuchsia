// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/common/usb.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb-bus.h>
#include <magenta/hw/usb-hub.h>
#include <sync/completion.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <magenta/listnode.h>
#include <threads.h>
#include <unistd.h>

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
    mx_device_t* mxdev;

    // Underlying USB device
    mx_device_t* usb_device;

    mx_device_t* bus_device;
    usb_bus_protocol_t* bus_protocol;

    usb_speed_t hub_speed;
    int num_ports;
    // delay after port power in microseconds
    mx_time_t power_on_delay;

    iotxn_t* status_request;
    completion_t completion;

    thrd_t thread;
    bool thread_done;

    // bit field indicating which ports are enabled
    uint8_t enabled_ports[128 / 8];
} usb_hub_t;

inline bool usb_hub_is_port_enabled(usb_hub_t* hub, int port) {
    return (hub->enabled_ports[port / 8] & (1 << (port % 8))) != 0;
}

inline void usb_hub_set_port_enabled(usb_hub_t* hub, int port, bool enabled) {
    if (enabled) {
        hub->enabled_ports[port / 8] |= (1 << (port % 8));
    } else {
        hub->enabled_ports[port / 8] &= ~(1 << (port % 8));
    }
}

static mx_status_t usb_hub_get_port_status(usb_hub_t* hub, int port, usb_port_status_t* status) {
    mx_status_t result = usb_get_status(hub->usb_device, USB_RECIP_PORT, port, status, sizeof(*status));
    if (result == sizeof(*status)) {
        xprintf("usb_hub_get_port_status port %d ", port);
        if (status->wPortChange & USB_C_PORT_CONNECTION) {
            xprintf("USB_C_PORT_CONNECTION ");
            usb_clear_feature(hub->usb_device, USB_RECIP_PORT, USB_FEATURE_C_PORT_CONNECTION, port);
        }
        if (status->wPortChange & USB_C_PORT_ENABLE) {
            xprintf("USB_C_PORT_ENABLE ");
            usb_clear_feature(hub->usb_device, USB_RECIP_PORT, USB_FEATURE_C_PORT_ENABLE, port);
        }
        if (status->wPortChange & USB_C_PORT_SUSPEND) {
            xprintf("USB_C_PORT_SUSPEND ");
            usb_clear_feature(hub->usb_device, USB_RECIP_PORT, USB_FEATURE_C_PORT_SUSPEND, port);
        }
        if (status->wPortChange & USB_C_PORT_OVER_CURRENT) {
            xprintf("USB_C_PORT_OVER_CURRENT ");
            usb_clear_feature(hub->usb_device, USB_RECIP_PORT, USB_FEATURE_C_PORT_OVER_CURRENT, port);
        }
        if (status->wPortChange & USB_C_PORT_RESET) {
            xprintf("USB_C_PORT_RESET");
            usb_clear_feature(hub->usb_device, USB_RECIP_PORT, USB_FEATURE_C_PORT_RESET, port);
        }
        if (status->wPortChange & USB_C_BH_PORT_RESET) {
            xprintf("USB_C_BH_PORT_RESET");
            usb_clear_feature(hub->usb_device, USB_RECIP_PORT, USB_FEATURE_C_BH_PORT_RESET, port);
        }
        if (status->wPortChange & USB_C_PORT_LINK_STATE) {
            xprintf("USB_C_PORT_LINK_STATE");
            usb_clear_feature(hub->usb_device, USB_RECIP_PORT, USB_FEATURE_C_PORT_LINK_STATE, port);
        }
        if (status->wPortChange & USB_C_PORT_CONFIG_ERROR) {
            xprintf("USB_C_PORT_CONFIG_ERROR");
            usb_clear_feature(hub->usb_device, USB_RECIP_PORT, USB_FEATURE_C_PORT_CONFIG_ERROR, port);
        }
        xprintf("\n");

        return NO_ERROR;
    } else {
        return -1;
    }
}

static mx_status_t usb_hub_wait_for_port(usb_hub_t* hub, int port, usb_port_status_t* status,
                                         uint16_t status_bits, uint16_t status_mask,
                                         mx_time_t stable_time) {
    const mx_time_t timeout = MX_SEC(2);        // 2 second total timeout
    const mx_time_t poll_delay = MX_MSEC(25);   // poll every 25 milliseconds
    mx_time_t total = 0;
    mx_time_t stable = 0;

    while (total < timeout) {
        mx_nanosleep(mx_deadline_after(poll_delay));
        total += poll_delay;

        mx_status_t result = usb_hub_get_port_status(hub, port, status);
        if (result != NO_ERROR) {
            return result;
        }

        if ((status->wPortStatus & status_mask) == status_bits) {
            stable += poll_delay;
            if (stable >= stable_time) {
                return NO_ERROR;
            }
        } else {
            stable = 0;
        }
    }

    return ERR_TIMED_OUT;
}

static void usb_hub_interrupt_complete(iotxn_t* txn, void* cookie) {
    xprintf("usb_hub_interrupt_complete got %d %" PRIu64 "\n", txn->status, txn->actual);
    usb_hub_t* hub = (usb_hub_t*)cookie;
    completion_signal(&hub->completion);
}

static void usb_hub_enable_port(usb_hub_t* hub, int port) {
    usb_set_feature(hub->usb_device, USB_RECIP_PORT, USB_FEATURE_PORT_POWER, port);
    usleep(hub->power_on_delay);
}

static void usb_hub_port_enabled(usb_hub_t* hub, int port) {
    usb_port_status_t status;

    xprintf("port %d usb_hub_port_enabled\n", port);

    // USB 2.0 spec section 9.1.2 recommends 100ms delay before enumerating
    // wait for USB_PORT_ENABLE == 1 and USB_PORT_RESET == 0
    if (usb_hub_wait_for_port(hub, port, &status, USB_PORT_ENABLE, USB_PORT_ENABLE | USB_PORT_RESET,
                              MX_MSEC(100)) != NO_ERROR) {
        printf("usb_hub_wait_for_port USB_PORT_RESET failed for USB hub, port %d\n", port);
        return;
    }

    usb_speed_t speed;
    if (hub->hub_speed == USB_SPEED_SUPER) {
        speed = USB_SPEED_SUPER;
    } else if (status.wPortStatus & USB_PORT_LOW_SPEED) {
        speed = USB_SPEED_LOW;
    } else if (status.wPortStatus & USB_PORT_HIGH_SPEED) {
        speed = USB_SPEED_HIGH;
    } else {
        speed = USB_SPEED_FULL;
    }

    xprintf("call hub_device_added for port %d\n", port);
    hub->bus_protocol->hub_device_added(hub->bus_device, hub->usb_device, port, speed);
    usb_hub_set_port_enabled(hub, port, true);
}

static void usb_hub_port_connected(usb_hub_t* hub, int port) {
    usb_port_status_t status;

    xprintf("port %d usb_hub_port_connected\n", port);

    // USB 2.0 spec section 7.1.7.3 recommends 100ms between connect and reset
    if (usb_hub_wait_for_port(hub, port, &status, USB_PORT_CONNECTION, USB_PORT_CONNECTION,
                              MX_MSEC(100)) != NO_ERROR) {
        printf("usb_hub_wait_for_port USB_PORT_CONNECTION failed for USB hub, port %d\n", port);
        return;
    }

    usb_set_feature(hub->usb_device, USB_RECIP_PORT, USB_FEATURE_PORT_RESET, port);
    usb_hub_port_enabled(hub, port);
}

static void usb_hub_port_disconnected(usb_hub_t* hub, int port) {
    xprintf("port %d usb_hub_port_disconnected\n", port);
    hub->bus_protocol->hub_device_removed(hub->bus_device, hub->usb_device, port);
    usb_hub_set_port_enabled(hub, port, false);
}

static void usb_hub_handle_port_status(usb_hub_t* hub, int port, usb_port_status_t* status) {
    xprintf("usb_hub_handle_port_status port: %d status: %04X change: %04X\n", port,
            status->wPortStatus, status->wPortChange);

    if (status->wPortChange & USB_C_PORT_CONNECTION) {
        // Handle race condition where device is quickly disconnected and reconnected.
        // This happens when Android devices switch USB configurations.
        // In this case, any change to the connect state should trigger a disconnect
        // before handling a connect event.
        if (usb_hub_is_port_enabled(hub, port)) {
            usb_hub_port_disconnected(hub, port);
        }
        if (status->wPortStatus & USB_PORT_CONNECTION) {
            usb_hub_port_connected(hub, port);
        }
    } else if (status->wPortStatus & USB_PORT_ENABLE && !usb_hub_is_port_enabled(hub, port)) {
        usb_hub_port_enabled(hub, port);
    }
}

static void usb_hub_unbind(void* ctx) {
    usb_hub_t* hub = ctx;

    for (int i = 1; i <= hub->num_ports; i++) {
        if (usb_hub_is_port_enabled(hub, i)) {
            usb_hub_port_disconnected(hub, i);
        }
    }
    device_remove(hub->mxdev);
}

static mx_status_t usb_hub_free(usb_hub_t* hub) {
    iotxn_release(hub->status_request);
    free(hub);
    return NO_ERROR;
}

static void usb_hub_release(void* ctx) {
    usb_hub_t* hub = ctx;

    hub->thread_done = true;
    completion_signal(&hub->completion);
    thrd_join(hub->thread, NULL);
    usb_hub_free(hub);
}

static mx_protocol_device_t usb_hub_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = usb_hub_unbind,
    .release = usb_hub_release,
};

static int usb_hub_thread(void* arg) {
    usb_hub_t* hub = (usb_hub_t*)arg;
    iotxn_t* txn = hub->status_request;

    usb_hub_descriptor_t desc;
    int desc_type = (hub->hub_speed == USB_SPEED_SUPER ? USB_HUB_DESC_TYPE_SS : USB_HUB_DESC_TYPE);
    mx_status_t result = usb_get_descriptor(hub->usb_device, USB_TYPE_CLASS | USB_RECIP_DEVICE,
                                            desc_type, 0, &desc, sizeof(desc));
    if (result < 0) {
        printf("get hub descriptor failed: %d\n", result);
        return result;
    }

    result = hub->bus_protocol->configure_hub(hub->bus_device, hub->usb_device, hub->hub_speed, &desc);
    if (result < 0) {
        printf("configure_hub failed: %d\n", result);
        return result;
    }

    int num_ports = desc.bNbrPorts;
    hub->num_ports = num_ports;
    // power on delay in microseconds
    hub->power_on_delay = desc.bPowerOn2PwrGood * 2 * 1000;
    if (hub->power_on_delay < 100 * 1000) {
        // USB 2.0 spec section 9.1.2 recommends atleast 100ms delay after power on
        hub->power_on_delay = 100 * 1000;
    }

    for (int i = 1; i <= num_ports; i++) {
        usb_hub_enable_port(hub, i);
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "usb-hub",
        .ctx = hub,
        .ops = &usb_hub_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    result = device_add(hub->usb_device, &args, &hub->mxdev);
    if (result != NO_ERROR) {
        usb_hub_free(hub);
        return result;
    }

    // bit field for port status bits
    uint8_t status_buf[128 / 8];
    memset(status_buf, 0, sizeof(status_buf));

    // This loop handles events from our interrupt endpoint
    while (1) {
        completion_reset(&hub->completion);
        iotxn_queue(hub->usb_device, txn);
        completion_wait(&hub->completion, MX_TIME_INFINITE);
        if (txn->status != NO_ERROR || hub->thread_done) {
            break;
        }

        iotxn_copyfrom(txn, status_buf, txn->actual, 0);
        uint8_t* bitmap = status_buf;
        uint8_t* bitmap_end = bitmap + txn->actual;

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
    }

    return NO_ERROR;
}

static mx_status_t usb_hub_bind(void* ctx, mx_device_t* device, void** cookie) {
    // search for the bus device
    mx_device_t* bus_device = device_get_parent(device);
    usb_bus_protocol_t* bus_protocol = NULL;
    while (bus_device != NULL && bus_protocol == NULL) {
        if (device_op_get_protocol(bus_device, MX_PROTOCOL_USB_BUS, (void**)&bus_protocol) == NO_ERROR) {
            break;
        }
        bus_device = device_get_parent(bus_device);
    }
    if (!bus_device || !bus_protocol) {
        printf("usb_hub_bind could not find bus device\n");
        return ERR_NOT_SUPPORTED;
    }

    // find our interrupt endpoint
    usb_desc_iter_t iter;
    mx_status_t result = usb_desc_iter_init(device, &iter);
    if (result < 0) return result;

    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
    if (!intf || intf->bNumEndpoints != 1) {
        usb_desc_iter_release(&iter);
        return ERR_NOT_SUPPORTED;
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
        return ERR_NOT_SUPPORTED;
    }

    usb_hub_t* hub = calloc(1, sizeof(usb_hub_t));
    if (!hub) {
        printf("Not enough memory for usb_hub_t.\n");
        return ERR_NO_MEMORY;
    }

    hub->usb_device = device;
    hub->hub_speed = usb_get_speed(device);
    hub->bus_device = bus_device;
    hub->bus_protocol = bus_protocol;

    mx_status_t status = NO_ERROR;
    iotxn_t* txn = usb_alloc_iotxn(ep_addr, max_packet_size);
    if (!txn) {
        status = ERR_NO_MEMORY;
        goto fail;
    }
    txn->length = max_packet_size;
    txn->complete_cb = usb_hub_interrupt_complete;
    txn->cookie = hub;
    hub->status_request = txn;

    int ret = thrd_create_with_name(&hub->thread, usb_hub_thread, hub, "usb_hub_thread");
    if (ret != thrd_success) {
        status = ERR_NO_MEMORY;
        goto fail;
    }
    return NO_ERROR;

fail:
    usb_hub_free(hub);
    return status;
}

static mx_driver_ops_t usb_hub_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_hub_bind,
};

MAGENTA_DRIVER_BEGIN(usb_hub, usb_hub_driver_ops, "magenta", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB),
    BI_MATCH_IF(EQ, BIND_USB_CLASS, USB_CLASS_HUB),
MAGENTA_DRIVER_END(usb_hub)
