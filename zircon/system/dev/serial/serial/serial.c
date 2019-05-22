// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/serial.h>
#include <ddk/protocol/serialimpl.h>
#include <fuchsia/hardware/serial/c/fidl.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

typedef struct {
    serial_impl_protocol_t serial;
    zx_device_t* zxdev;
    zx_handle_t socket; // socket used for communicating with our client
    zx_handle_t event;  // event for signaling serial driver state changes
    thrd_t thread;
    mtx_t lock;
    uint32_t serial_class;
    bool open;
} serial_port_t;

enum {
    WAIT_ITEM_SOCKET,
    WAIT_ITEM_EVENT,
};

#define UART_BUFFER_SIZE    1024

#define EVENT_READABLE_SIGNAL ZX_USER_SIGNAL_0
#define EVENT_WRITABLE_SIGNAL ZX_USER_SIGNAL_1
#define EVENT_CANCEL_SIGNAL ZX_USER_SIGNAL_2

const serial_notify_t kNoCallback = {NULL, NULL};

// This thread handles data transfer in both directions
static int platform_serial_thread(void* arg) {
    serial_port_t* port = arg;
    uint8_t in_buffer[UART_BUFFER_SIZE];
    uint8_t out_buffer[UART_BUFFER_SIZE];
    size_t in_buffer_offset = 0;    // offset of first byte in in_buffer (if any)
    size_t out_buffer_offset = 0;   // offset of first byte in out_buffer (if any)
    size_t in_buffer_count = 0;     // number of bytes in in_buffer
    size_t out_buffer_count = 0;    // number of bytes in out_buffer
    zx_wait_item_t items[2];

    items[WAIT_ITEM_SOCKET].handle = port->socket;
    items[WAIT_ITEM_EVENT].handle = port->event;
    bool peer_closed = false;

    // loop until client socket is closed and we have no more data to write
    while (!peer_closed || out_buffer_count > 0) {
        // attempt pending socket write
        if (in_buffer_count > 0) {
            size_t actual;
            zx_status_t status = zx_socket_write(port->socket, 0, in_buffer + in_buffer_offset,
                                                 in_buffer_count, &actual);
            if (status == ZX_OK) {
                in_buffer_count -= actual;
                if (in_buffer_count > 0) {
                    in_buffer_offset += actual;
                } else {
                    in_buffer_offset = 0;
                }
            } else if (status != ZX_ERR_SHOULD_WAIT && status != ZX_ERR_PEER_CLOSED) {
                zxlogf(ERROR, "platform_serial_thread: zx_socket_write returned %d\n", status);
                break;
            }
        }

        // attempt pending serial write
        if (out_buffer_count > 0) {
            size_t actual;
            zx_status_t status = serial_impl_write(&port->serial, out_buffer + out_buffer_offset,
                                                   out_buffer_count, &actual);
            if (status == ZX_OK) {
                out_buffer_count -= actual;
                if (out_buffer_count > 0) {
                    out_buffer_offset += actual;
                } else {
                    // out_buffer empty now, reset to beginning
                    out_buffer_offset = 0;
                }
            } else if (status != ZX_ERR_SHOULD_WAIT && status != ZX_ERR_PEER_CLOSED) {
                zxlogf(ERROR, "platform_serial_thread: serial_impl_write returned %d\n", status);
                break;
            }
        }

        // wait for serial or socket to be readable
        items[WAIT_ITEM_SOCKET].waitfor = ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED;
        items[WAIT_ITEM_EVENT].waitfor = EVENT_READABLE_SIGNAL | EVENT_CANCEL_SIGNAL;
        // also wait for writability if we have pending data to write
        if (in_buffer_count > 0) {
            items[WAIT_ITEM_SOCKET].waitfor |= ZX_SOCKET_WRITABLE;
        }
        if (out_buffer_count > 0) {
            items[WAIT_ITEM_EVENT].waitfor |= EVENT_WRITABLE_SIGNAL;
        }

        zx_status_t status = zx_object_wait_many(items, countof(items), ZX_TIME_INFINITE);
        if (status != ZX_OK) {
            zxlogf(ERROR, "platform_serial_thread: zx_object_wait_many returned %d\n", status);
            break;
        }

        if (items[WAIT_ITEM_EVENT].pending & EVENT_READABLE_SIGNAL) {
            size_t length;
            status = serial_impl_read(&port->serial, in_buffer + in_buffer_count,
                                      sizeof(in_buffer) - in_buffer_count, &length);

            if (status != ZX_OK) {
                zxlogf(ERROR, "platform_serial_thread: serial_impl_read returned %d\n", status);
                break;
            }
            in_buffer_count += length;
        }

        if (items[WAIT_ITEM_SOCKET].pending & ZX_SOCKET_READABLE) {
            size_t length;
            status = zx_socket_read(port->socket, 0, out_buffer + out_buffer_count,
                                    sizeof(out_buffer) - out_buffer_count, &length);
            if (status != ZX_OK) {
                zxlogf(ERROR, "serial_out_thread: zx_socket_read returned %d\n", status);
                break;
            }
            out_buffer_count += length;
        }
        if (items[WAIT_ITEM_SOCKET].pending & ZX_SOCKET_PEER_CLOSED) {
            peer_closed = true;
        }
    }

    serial_impl_enable(&port->serial, false);
    serial_impl_set_notify_callback(&port->serial, &kNoCallback);

    zx_handle_close(port->event);
    zx_handle_close(port->socket);
    port->event = ZX_HANDLE_INVALID;
    port->socket = ZX_HANDLE_INVALID;
    mtx_lock(&port->lock);
    port->open = false;
    mtx_unlock(&port->lock);

    return 0;
}

static void platform_serial_state_cb(void* cookie, serial_state_t state) {
    serial_port_t* port = cookie;

    // update our event handle signals with latest state from the serial driver
    zx_signals_t event_set = 0;
    zx_signals_t event_clear = 0;
    zx_signals_t device_set = 0;
    zx_signals_t device_clear = 0;

    if (state & SERIAL_STATE_READABLE) {
        event_set |= EVENT_READABLE_SIGNAL;
        device_set |= DEV_STATE_READABLE;
    } else {
        event_clear |= EVENT_READABLE_SIGNAL;
        device_clear |= DEV_STATE_READABLE;
    }
    if (state & SERIAL_STATE_WRITABLE) {
        event_set |= EVENT_WRITABLE_SIGNAL;
        device_set |= DEV_STATE_WRITABLE;
    } else {
        event_clear |= EVENT_WRITABLE_SIGNAL;
        device_clear |= DEV_STATE_WRITABLE;
    }

    if (port->socket != ZX_HANDLE_INVALID) {
        // another driver bound to us
        zx_object_signal(port->event, event_clear, event_set);
    } else {
        // someone opened us via /dev file system
        device_state_clr_set(port->zxdev, device_clear, device_set);
    }
}

static zx_status_t serial_port_get_info(void* ctx, serial_port_info_t* info) {
    serial_port_t* port = ctx;
    return serial_impl_get_info(&port->serial, info);
}

static zx_status_t serial_port_config(void* ctx, uint32_t baud_rate, uint32_t flags) {
    serial_port_t* port = ctx;
    return serial_impl_config(&port->serial, baud_rate, flags);
}

static zx_status_t serial_port_open_socket(void* ctx, zx_handle_t* out_handle) {
    serial_port_t* port = ctx;

    mtx_lock(&port->lock);
    if (port->open) {
        mtx_unlock(&port->lock);
        return ZX_ERR_ALREADY_BOUND;
    }

    zx_handle_t socket = ZX_HANDLE_INVALID;
    zx_status_t status = zx_socket_create(ZX_SOCKET_STREAM, &port->socket, &socket);
    if (status != ZX_OK) {
        mtx_unlock(&port->lock);
        return status;
    }

    status = zx_event_create(0, &port->event);
    if (status != ZX_OK) {
        goto fail;
    }

    const serial_notify_t callback = {platform_serial_state_cb, port};
    serial_impl_set_notify_callback(&port->serial, &callback);

    status = serial_impl_enable(&port->serial, true);
    if (status != ZX_OK) {
        goto fail;
    }

    int thrd_rc = thrd_create_with_name(&port->thread, platform_serial_thread, port,
                                        "platform_serial_thread");
    if (thrd_rc != thrd_success) {
        status = thrd_status_to_zx_status(thrd_rc);
        goto fail;
    }

    *out_handle = socket;
    port->open = true;
    mtx_unlock(&port->lock);
    return ZX_OK;

fail:
    zx_handle_close(socket);
    mtx_unlock(&port->lock);

    return status;
}

static serial_protocol_ops_t serial_ops = {
    .get_info = serial_port_get_info,
    .config = serial_port_config,
    .open_socket = serial_port_open_socket,
};

static zx_status_t serial_open(void* ctx, zx_device_t** dev_out, uint32_t flags) {
    serial_port_t* port = ctx;

    mtx_lock(&port->lock);

    if (port->open) {
        mtx_unlock(&port->lock);
        return ZX_ERR_ALREADY_BOUND;
    }

    const serial_notify_t callback = {platform_serial_state_cb, port};
    serial_impl_set_notify_callback(&port->serial, &callback);

    zx_status_t status = serial_impl_enable(&port->serial, true);
    if (status == ZX_OK) {
        port->open = true;
    }

    mtx_unlock(&port->lock);
    return status;
}

static zx_status_t serial_close(void* ctx, uint32_t flags) {
    serial_port_t* port = ctx;

    mtx_lock(&port->lock);

    if (port->open) {
        serial_impl_set_notify_callback(&port->serial, &kNoCallback);
        serial_impl_enable(&port->serial, false);
        port->open = false;
        mtx_unlock(&port->lock);
        return ZX_OK;
    } else {
        zxlogf(ERROR, "port_serial_close called when not open\n");
        mtx_unlock(&port->lock);
        return ZX_ERR_BAD_STATE;
    }
}

static zx_status_t serial_read(void* ctx, void* buf, size_t count, zx_off_t off,
                                    size_t* actual) {
    serial_port_t* port = ctx;

    if (!port->open) {
        return ZX_ERR_BAD_STATE;
    }

    return serial_impl_read(&port->serial, buf, count, actual);
}

static zx_status_t serial_write(void* ctx, const void* buf, size_t count, zx_off_t off,
                                     size_t* actual) {
    serial_port_t* port = ctx;

    if (!port->open) {
        return ZX_ERR_BAD_STATE;
    }

    return serial_impl_write(&port->serial, buf, count, actual);
}

static zx_status_t fidl_GetClass(void* ctx, fidl_txn_t* txn) {
    serial_port_t* port = ctx;
    return fuchsia_hardware_serial_DeviceGetClass_reply(txn, port->serial_class);
}

static zx_status_t fidl_SetConfig(void* ctx, const fuchsia_hardware_serial_Config* config,
                                  fidl_txn_t* txn) {
    serial_port_t* port = ctx;

    uint32_t flags = 0;
    switch (config->character_width) {
    case fuchsia_hardware_serial_CharacterWidth_BITS_5: flags |= SERIAL_DATA_BITS_5; break;
    case fuchsia_hardware_serial_CharacterWidth_BITS_6: flags |= SERIAL_DATA_BITS_6; break;
    case fuchsia_hardware_serial_CharacterWidth_BITS_7: flags |= SERIAL_DATA_BITS_7; break;
    case fuchsia_hardware_serial_CharacterWidth_BITS_8: flags |= SERIAL_DATA_BITS_8; break;
    }

    switch (config->stop_width) {
    case fuchsia_hardware_serial_StopWidth_BITS_1: flags |= SERIAL_STOP_BITS_1; break;
    case fuchsia_hardware_serial_StopWidth_BITS_2: flags |= SERIAL_STOP_BITS_2; break;
    }

    switch (config->parity) {
    case fuchsia_hardware_serial_Parity_NONE: flags |= SERIAL_PARITY_NONE; break;
    case fuchsia_hardware_serial_Parity_EVEN: flags |= SERIAL_PARITY_EVEN; break;
    case fuchsia_hardware_serial_Parity_ODD: flags |= SERIAL_PARITY_ODD; break;
    }

    switch (config->control_flow) {
    case fuchsia_hardware_serial_FlowControl_NONE: flags |= SERIAL_FLOW_CTRL_NONE; break;
    case fuchsia_hardware_serial_FlowControl_CTS_RTS: flags |= SERIAL_FLOW_CTRL_CTS_RTS; break;
    }

    zx_status_t status = serial_impl_config(&port->serial, config->baud_rate, flags);
    return fuchsia_hardware_serial_DeviceSetConfig_reply(txn, status);
}

static zx_status_t serial_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
    static const fuchsia_hardware_serial_Device_ops_t ops = {
        .GetClass = fidl_GetClass,
        .SetConfig = fidl_SetConfig,
    };
    return fuchsia_hardware_serial_Device_dispatch(ctx, txn, msg, &ops);
};

static void serial_release(void* ctx) {
    serial_port_t* port = ctx;

    serial_impl_enable(&port->serial, false);
    serial_impl_set_notify_callback(&port->serial, &kNoCallback);
    zx_handle_close(port->event);
    zx_handle_close(port->socket);
    free(port);
}

static zx_protocol_device_t serial_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .open = serial_open,
    .close = serial_close,
    .read = serial_read,
    .write = serial_write,
    .message = serial_message,
    .release = serial_release,
};

static zx_status_t serial_bind(void* ctx, zx_device_t* parent) {
    serial_port_t* port = calloc(1, sizeof(serial_port_t));
    if (!port) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_SERIAL_IMPL, &port->serial);
    if (status != ZX_OK) {
        zxlogf(ERROR, "serial_bind: ZX_PROTOCOL_SERIAL_IMPL not available\n");
        free(port);
        return status;
    }

    serial_port_info_t info;
    status = serial_impl_get_info(&port->serial, &info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "serial_bind: serial_impl_get_info failed %d\n", status);
        free(port);
        return status;
    }
    port->serial_class = info.serial_class;

    zx_device_prop_t props[] = {
        { BIND_PROTOCOL, 0, ZX_PROTOCOL_SERIAL },
        { BIND_SERIAL_CLASS, 0, port->serial_class },
    };

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "serial",
        .ctx = port,
        .ops = &serial_device_proto,
        .proto_id = ZX_PROTOCOL_SERIAL,
        .proto_ops = &serial_ops,
        .props = props,
        .prop_count = countof(props),
    };

    status = device_add(parent, &args, &port->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "serial_bind: device_add failed\n");
        goto fail;
    }

    return ZX_OK;
fail:
        serial_release(port);
        return status;
}

static zx_driver_ops_t serial_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = serial_bind,
};

ZIRCON_DRIVER_BEGIN(serial, serial_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SERIAL_IMPL),
ZIRCON_DRIVER_END(serial)
