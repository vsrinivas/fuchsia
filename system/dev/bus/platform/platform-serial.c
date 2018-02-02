// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/serial.h>
#include <zircon/threads.h>
#include <stdlib.h>
#include <threads.h>

#include "platform-bus.h"

typedef struct serial_port {
    serial_driver_protocol_t serial;
    uint32_t port_num;
    zx_handle_t socket; // socket used for communicating with our client
    zx_handle_t event;  // event for signaling serial driver state changes
    thrd_t thread;
    mtx_t lock;
} serial_port_t;

enum {
    WAIT_ITEM_SOCKET,
    WAIT_ITEM_EVENT,
};

#define UART_BUFFER_SIZE    1024

#define EVENT_READABLE_SIGNAL ZX_USER_SIGNAL_0
#define EVENT_WRITABLE_SIGNAL ZX_USER_SIGNAL_1
#define EVENT_CANCEL_SIGNAL ZX_USER_SIGNAL_2

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
            } else if (status != ZX_ERR_SHOULD_WAIT && status != ZX_SOCKET_PEER_CLOSED) {
                zxlogf(ERROR, "platform_serial_thread: zx_socket_write returned %d\n", status);
                break;
            }
        }

        // attempt pending serial write
        if (out_buffer_count > 0) {
            size_t actual;
            zx_status_t status = serial_driver_write(&port->serial, port->port_num,
                                                     out_buffer + out_buffer_offset,
                                                     out_buffer_count, &actual);
            if (status == ZX_OK) {
                out_buffer_count -= actual;
                if (out_buffer_count > 0) {
                    out_buffer_offset += actual;
                } else {
                    // out_buffer empty now, reset to beginning
                    out_buffer_offset = 0;
                }
            } else if (status != ZX_ERR_SHOULD_WAIT && status != ZX_SOCKET_PEER_CLOSED) {
                zxlogf(ERROR, "platform_serial_thread: serial_driver_write returned %d\n", status);
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
            status = serial_driver_read(&port->serial, port->port_num, in_buffer + in_buffer_count,
                                        sizeof(in_buffer) - in_buffer_count, &length);

            if (status != ZX_OK) {
                zxlogf(ERROR, "platform_serial_thread: serial_driver_read returned %d\n", status);
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

    serial_driver_enable(&port->serial, port->port_num, false);
    serial_driver_set_notify_callback(&port->serial, port->port_num, NULL, NULL);

    zx_handle_close(port->event);
    zx_handle_close(port->socket);
    port->event = ZX_HANDLE_INVALID;
    port->socket = ZX_HANDLE_INVALID;

    return 0;
}

static void platform_serial_state_cb(uint32_t port_num, uint32_t state, void* cookie) {
    serial_port_t* port = cookie;

    // update our event handle signals with latest state from the serial driver
    zx_signals_t set = 0;
    zx_signals_t clear = 0;
    if (state & SERIAL_STATE_READABLE) {
        set |= EVENT_READABLE_SIGNAL;
    } else {
        clear |= EVENT_READABLE_SIGNAL;
    }
    if (state & SERIAL_STATE_WRITABLE) {
        set |= EVENT_WRITABLE_SIGNAL;
    } else {
        clear |= EVENT_WRITABLE_SIGNAL;
    }

    zx_object_signal(port->event, clear, set);
}

zx_status_t platform_serial_init(platform_bus_t* bus, serial_driver_protocol_t* serial) {
    uint32_t port_count = serial_driver_get_port_count(serial);
    if (!port_count) {
        return ZX_ERR_INVALID_ARGS;
     }

    if (bus->serial_ports) {
        // already initialized
        return ZX_ERR_BAD_STATE;
    }

    serial_port_t* ports = calloc(port_count, sizeof(serial_port_t));
    if (!ports) {
        return ZX_ERR_NO_MEMORY;
    }

    bus->serial_ports = ports;
    bus->serial_port_count = port_count;

    for (uint32_t i = 0; i < port_count; i++) {
        serial_port_t* port = &ports[i];
        mtx_init(&port->lock, mtx_plain);
        memcpy(&port->serial, serial, sizeof(port->serial));
        port->port_num = i;
    }

    return ZX_OK;
}

static void platform_serial_port_release(serial_port_t* port) {
    serial_driver_enable(&port->serial, port->port_num, false);
    serial_driver_set_notify_callback(&port->serial, port->port_num, NULL, NULL);
    zx_handle_close(port->event);
    zx_handle_close(port->socket);
    port->event = ZX_HANDLE_INVALID;
    port->socket = ZX_HANDLE_INVALID;
}

void platform_serial_release(platform_bus_t* bus) {
    if (bus->serial_ports) {
        for (unsigned i = 0; i < bus->serial_port_count; i++) {
            platform_serial_port_release(&bus->serial_ports[i]);
        }
    }
    free(bus->serial_ports);
}

zx_status_t platform_serial_config(platform_bus_t* bus, uint32_t port_num, uint32_t baud_rate,
                                   uint32_t flags) {
    if (port_num >= bus->serial_port_count) {
        return ZX_ERR_NOT_FOUND;
    }

// locking? flushing?
    return serial_driver_config(&bus->serial, port_num, baud_rate, flags);
}

zx_status_t platform_serial_open_socket(platform_bus_t* bus, uint32_t port_num,
                                        zx_handle_t* out_handle) {
    if (port_num >= bus->serial_port_count) {
        return ZX_ERR_NOT_FOUND;
    }
    serial_port_t* port = &bus->serial_ports[port_num];

    mtx_lock(&port->lock);
    if (port->socket != ZX_HANDLE_INVALID) {
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

    serial_driver_set_notify_callback(&bus->serial, port_num, platform_serial_state_cb, port);

    status = serial_driver_enable(&bus->serial, port_num, true);
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
    mtx_unlock(&port->lock);
    return ZX_OK;

fail:
    zx_handle_close(socket);
    platform_serial_port_release(port);
    mtx_unlock(&port->lock);

    return status;
}
