// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/bt-hci.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/serial.h>
#include <zircon/device/bt-hci.h>
#include <zircon/status.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

// The maximum HCI ACL frame size used for data transactions
#define ACL_MAX_FRAME_SIZE 1029 // (1024 + 4 bytes for the ACL header + 1 byte packet indicator)

#define CMD_BUF_SIZE 255 + 4   // 1 byte packet indicator + 3 byte header + payload
#define EVENT_BUF_SIZE 255 + 3 // 1 byte packet indicator + 2 byte header + payload

// The number of currently supported HCI channel endpoints. We currently have
// one channel for command/event flow and one for ACL data flow. The sniff channel is managed
// separately.
#define NUM_CHANNELS 2

#define NUM_WAIT_ITEMS NUM_CHANNELS + 2 // add one for the changed event and one for UART socket

// HCI UART packet indicators
enum {
    HCI_NONE = 0,
    HCI_COMMAND = 1,
    HCI_ACL_DATA = 2,
    HCI_SCO = 3,
    HCI_EVENT = 4,
};

typedef struct {
    zx_device_t* zxdev;
    zx_device_t* parent;
    zx_handle_t uart_socket;
    zx_handle_t cmd_channel;
    zx_handle_t acl_channel;
    zx_handle_t snoop_channel;

    // Signaled when a channel opens or closes
    zx_handle_t channels_changed_evt;

    zx_wait_item_t read_wait_items[NUM_WAIT_ITEMS];
    uint32_t read_wait_item_count;

    bool read_thread_running;

    // type of current packet being read from the UART
    uint8_t cur_uart_packet_type;

    // for accumulating HCI events
    uint8_t event_buffer[EVENT_BUF_SIZE];
    size_t event_buffer_offset;

    // for accumulating ACL data packets
    uint8_t acl_buffer[EVENT_BUF_SIZE];
    size_t acl_buffer_offset;

    mtx_t mutex;
} hci_t;

// macro for returning length of current event packet being received
// payload length is in byte 2 of the packet
// add 3 bytes for packet indicator, event code and length byte
#define EVENT_PACKET_LENGTH(hci) ((hci)->event_buffer_offset > 2 ? (hci)->event_buffer[2] + 3 : 0)

// macro for returning length of current ACL data packet being received
// length is in bytes 3 and 4 of the packet
// add 5 bytes for packet indicator, control info and length fields
#define ACL_PACKET_LENGTH(hci) ((hci)->acl_buffer_offset > 4 ? \
                                    ((hci)->acl_buffer[3] | ((hci)->acl_buffer[4] << 8)) + 5 : 0)

static void channel_cleanup_locked(hci_t* hci, zx_handle_t* channel) {
    if (*channel == ZX_HANDLE_INVALID)
        return;

    zx_handle_close(*channel);
    *channel = ZX_HANDLE_INVALID;
    zx_object_signal(hci->channels_changed_evt, 0, ZX_EVENT_SIGNALED);
}

static void snoop_channel_write_locked(hci_t* hci, uint8_t flags, uint8_t* bytes, size_t length) {
    if (hci->snoop_channel == ZX_HANDLE_INVALID)
        return;

    // We tack on a flags byte to the beginning of the payload.
    uint8_t snoop_buffer[length + 1];
    snoop_buffer[0] = flags;
    memcpy(snoop_buffer + 1, bytes, length);
    zx_status_t status = zx_channel_write(hci->snoop_channel, 0, snoop_buffer, length + 1, NULL, 0);
    if (status < 0) {
        zxlogf(ERROR, "bt-transport-uart: failed to write to snoop channel: %s\n",
               zx_status_get_string(status));
        channel_cleanup_locked(hci, &hci->snoop_channel);
    }
}

static void hci_build_read_wait_items_locked(hci_t* hci) {
    zx_wait_item_t* items = hci->read_wait_items;
    memset(items, 0, sizeof(hci->read_wait_items));
    uint32_t count = 0;

    if (hci->cmd_channel != ZX_HANDLE_INVALID) {
        items[count].handle = hci->cmd_channel;
        items[count].waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
        count++;
    }

    if (hci->acl_channel != ZX_HANDLE_INVALID) {
        items[count].handle = hci->acl_channel;
        items[count].waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
        count++;
    }

    items[count].handle = hci->uart_socket;
    items[count].waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    count++;

    items[count].handle = hci->channels_changed_evt;
    items[count].waitfor = ZX_EVENT_SIGNALED;
    count++;

    hci->read_wait_item_count = count;

    zx_object_signal(hci->channels_changed_evt, ZX_EVENT_SIGNALED, 0);
}

static void hci_build_read_wait_items(hci_t* hci) {
    mtx_lock(&hci->mutex);
    hci_build_read_wait_items_locked(hci);
    mtx_unlock(&hci->mutex);
}

// Returns false if there's an error while sending the packet to the hardware or
// if the channel peer closed its endpoint.
static void hci_handle_cmd_read_events(hci_t* hci, zx_wait_item_t* item) {
    if (item->pending & (ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED)) {
        uint8_t buf[CMD_BUF_SIZE];
        uint32_t length = sizeof(buf) - 1;
        zx_status_t status =
            zx_channel_read(item->handle, 0, buf + 1, NULL, length, 0, &length, NULL);
        if (status < 0) {
            if (status != ZX_ERR_PEER_CLOSED) {
                zxlogf(ERROR, "hci_read_thread: failed to read from command channel %s\n",
                       zx_status_get_string(status));
            }
            goto fail;
        }

        buf[0] = HCI_COMMAND;
        length++;
        status = zx_socket_write(hci->uart_socket, 0, buf, length, NULL);
        if (status < 0) {
            zxlogf(ERROR, "hci_read_thread: zx_socket_write failed: %s\n",
                   zx_status_get_string(status));
            goto fail;
        }

        mtx_lock(&hci->mutex);
        snoop_channel_write_locked(hci, bt_hci_snoop_flags(BT_HCI_SNOOP_TYPE_CMD, false), buf + 1, length - 1);
        mtx_unlock(&hci->mutex);
    }

    return;

fail:
    mtx_lock(&hci->mutex);
    channel_cleanup_locked(hci, &hci->cmd_channel);
    mtx_unlock(&hci->mutex);
}

static void hci_handle_acl_read_events(hci_t* hci, zx_wait_item_t* item) {
    if (item->pending & (ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED)) {
        uint8_t buf[ACL_MAX_FRAME_SIZE];
        uint32_t length = sizeof(buf) - 1;
        zx_status_t status =
            zx_channel_read(item->handle, 0, buf + 1, NULL, length, 0, &length, NULL);
        if (status < 0) {
            zxlogf(ERROR, "hci_read_thread: failed to read from ACL channel %s\n",
                   zx_status_get_string(status));
            goto fail;
        }

        buf[0] = HCI_ACL_DATA;
        length++;
        status = zx_socket_write(hci->uart_socket, 0, buf, length, NULL);
        if (status < 0) {
            zxlogf(ERROR, "hci_read_thread: zx_socket_write failed: %s\n",
                   zx_status_get_string(status));
            goto fail;
        }
        snoop_channel_write_locked(
            hci, bt_hci_snoop_flags(BT_HCI_SNOOP_TYPE_ACL, false), buf + 1, length - 1);
    }

    return;

fail:
    mtx_lock(&hci->mutex);
    channel_cleanup_locked(hci, &hci->acl_channel);
    mtx_unlock(&hci->mutex);
}

static void hci_handle_uart_read_events(hci_t* hci, zx_wait_item_t* item) {
    if (item->pending & (ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED)) {
        uint8_t buf[ACL_MAX_FRAME_SIZE];
        size_t length = sizeof(buf);
        zx_status_t status = zx_socket_read(item->handle, 0, buf, length, &length);
        if (status < 0) {
            zxlogf(ERROR, "hci_read_thread: failed to read from ACL channel %s\n",
                   zx_status_get_string(status));
            goto fail;
        }

        const uint8_t* src = buf;
        const uint8_t* end = src + length;
        uint8_t packet_type = hci->cur_uart_packet_type;

        while (src < end) {
            if (packet_type == HCI_NONE) {
                // start of new packet. read packet type
                packet_type = *src++;
                if (packet_type != HCI_EVENT && packet_type != HCI_ACL_DATA) {
                    zxlogf(INFO, "unsupported HCI packet type %u. We may be out of sync\n",
                           packet_type);
                    return;
                }
            }

            if (packet_type == HCI_EVENT) {
                size_t packet_length = EVENT_PACKET_LENGTH(hci);

                while (!packet_length && src < end) {
                    // read until we have enough to compute packet length
                    hci->event_buffer[hci->event_buffer_offset++] = *src++;
                    packet_length = EVENT_PACKET_LENGTH(hci);
                }
                if (!packet_length) {
                    break;
                }

                size_t remaining = end - src;
                size_t copy = packet_length - hci->event_buffer_offset;
                if (copy > remaining) copy = remaining;
                memcpy(hci->event_buffer + hci->event_buffer_offset, src, copy);
                src += copy;
                hci->event_buffer_offset += copy;

                if (hci->event_buffer_offset == packet_length) {
                    // send accumulated event packet, minus the packet indicator
                    zx_status_t status = zx_channel_write(hci->cmd_channel, 0,
                                                          &hci->event_buffer[1],
                                                          packet_length - 1, NULL, 0);
                    if (status < 0) {
                        zxlogf(ERROR, "bt-transport-uart: failed to write event packet: %s\n",
                               zx_status_get_string(status));
                    }
                    snoop_channel_write_locked(hci, bt_hci_snoop_flags(BT_HCI_SNOOP_TYPE_EVT, true),
                                               &hci->event_buffer[1], packet_length - 1);

                    // reset buffer
                    packet_type = HCI_NONE;
                    hci->event_buffer_offset = 1;
                }
            } else { // HCI_ACL_DATA
                size_t packet_length = EVENT_PACKET_LENGTH(hci);

                while (!packet_length && src < end) {
                    // read until we have enough to compute packet length
                    hci->acl_buffer[hci->acl_buffer_offset++] = *src++;
                    packet_length = ACL_PACKET_LENGTH(hci);
                }
                if (!packet_length) {
                    break;
                }

                size_t remaining = end - src;
                size_t copy = packet_length - hci->acl_buffer_offset;
                if (copy > remaining) copy = remaining;
                memcpy(hci->acl_buffer + hci->acl_buffer_offset, src, copy);
                src += copy;
                hci->acl_buffer_offset += copy;

                if (hci->acl_buffer_offset == packet_length) {
                    // send accumulated ACL data packet, minus the packet indicator
                    zx_status_t status = zx_channel_write(hci->acl_channel, 0,
                                                          &hci->acl_buffer[1],
                                                          packet_length - 1, NULL, 0);
                    if (status < 0) {
                        zxlogf(ERROR, "bt-transport-uart: failed to write ACL packet: %s\n",
                               zx_status_get_string(status));
                    }

                    // If the snoop channel is open then try to write the packet
                    // even if acl_channel was closed.
                    snoop_channel_write_locked(hci,
                                               bt_hci_snoop_flags(BT_HCI_SNOOP_TYPE_ACL, true),
                                               &hci->acl_buffer[1], packet_length - 1);

                    // reset buffer
                    packet_type = HCI_NONE;
                    hci->acl_buffer_offset = 1;
                }
            }
        }

        hci->cur_uart_packet_type = packet_type;
    }

    return;

fail:
    mtx_lock(&hci->mutex);
    channel_cleanup_locked(hci, &hci->acl_channel);
    mtx_unlock(&hci->mutex);
}

static bool hci_has_read_channels_locked(hci_t* hci) {
    // One for the signal event and one for uart socket, any additional are read channels.
    return hci->read_wait_item_count > 2;
}

static int hci_read_thread(void* arg) {
    hci_t* hci = (hci_t*)arg;

    mtx_lock(&hci->mutex);

    if (!hci_has_read_channels_locked(hci)) {
        zxlogf(ERROR, "bt-transport-uart: no channels are open - exiting\n");
        hci->read_thread_running = false;
        mtx_unlock(&hci->mutex);
        return 0;
    }

    mtx_unlock(&hci->mutex);

    while (1) {
        zx_status_t status = zx_object_wait_many(hci->read_wait_items, hci->read_wait_item_count,
                                                 ZX_TIME_INFINITE);
        if (status < 0) {
            zxlogf(ERROR, "bt-transport-uart: zx_object_wait_many failed (%s) - exiting\n",
                   zx_status_get_string(status));
            mtx_lock(&hci->mutex);
            channel_cleanup_locked(hci, &hci->cmd_channel);
            channel_cleanup_locked(hci, &hci->acl_channel);
            mtx_unlock(&hci->mutex);
            break;
        }

        for (unsigned i = 0; i < hci->read_wait_item_count; ++i) {
            mtx_lock(&hci->mutex);
            zx_wait_item_t item = hci->read_wait_items[i];
            mtx_unlock(&hci->mutex);

            if (item.handle == hci->cmd_channel) {
                hci_handle_cmd_read_events(hci, &item);
            } else if (item.handle == hci->acl_channel) {
                hci_handle_acl_read_events(hci, &item);
            } else if (item.handle == hci->uart_socket) {
                hci_handle_uart_read_events(hci, &item);
            }
        }

        // The channels might have been changed by the *_read_events, recheck the event.
        status = zx_object_wait_one(hci->channels_changed_evt, ZX_EVENT_SIGNALED, 0u, NULL);
        if (status == ZX_OK) {
            hci_build_read_wait_items(hci);
            if (!hci_has_read_channels_locked(hci)) {
                zxlogf(TRACE, "bt-transport-uart: all channels closed - exiting\n");
                break;
            }
        }
    }

    mtx_lock(&hci->mutex);
    hci->read_thread_running = false;
    mtx_unlock(&hci->mutex);
    return 0;
}

static zx_status_t hci_open_channel(hci_t* hci, zx_handle_t* in_channel, zx_handle_t* out_channel) {
    zx_status_t result = ZX_OK;
    mtx_lock(&hci->mutex);

    if (*in_channel != ZX_HANDLE_INVALID) {
        zxlogf(ERROR, "bt-transport-uart: already bound, failing\n");
        result = ZX_ERR_ALREADY_BOUND;
        goto done;
    }

    zx_status_t status = zx_channel_create(0, in_channel, out_channel);
    if (status < 0) {
        zxlogf(ERROR, "bt-transport-uart: Failed to create channel: %s\n",
               zx_status_get_string(status));
        result = ZX_ERR_INTERNAL;
        goto done;
    }

    // Kick off the hci_read_thread if it's not already running.
    if (!hci->read_thread_running) {
        hci_build_read_wait_items_locked(hci);
        thrd_t read_thread;
        thrd_create_with_name(&read_thread, hci_read_thread, hci, "bt_usb_read_thread");
        hci->read_thread_running = true;
        thrd_detach(read_thread);
    } else {
        // Poke the changed event to get the new channel.
        zx_object_signal(hci->channels_changed_evt, 0, ZX_EVENT_SIGNALED);
    }

done:
    mtx_unlock(&hci->mutex);
    return result;
}

static void hci_unbind(void* ctx) {
    hci_t* hci = ctx;

    // Close the transport channels so that the host stack is notified of device removal.
    mtx_lock(&hci->mutex);

    channel_cleanup_locked(hci, &hci->cmd_channel);
    channel_cleanup_locked(hci, &hci->acl_channel);
    channel_cleanup_locked(hci, &hci->snoop_channel);

    mtx_unlock(&hci->mutex);

    device_remove(hci->zxdev);
}

static void hci_release(void* ctx) {
    hci_t* hci = ctx;
    zx_handle_close(hci->uart_socket);
    free(hci);
}

static zx_status_t hci_open_command_channel(void* ctx, zx_handle_t* out_channel) {
    hci_t* hci = ctx;
    return hci_open_channel(hci, &hci->cmd_channel, out_channel);
}

static zx_status_t hci_open_acl_data_channel(void* ctx, zx_handle_t* out_channel) {
    hci_t* hci = ctx;
    return hci_open_channel(hci, &hci->acl_channel, out_channel);
}

static zx_status_t hci_open_snoop_channel(void* ctx, zx_handle_t* out_channel) {
    hci_t* hci = ctx;
    return hci_open_channel(hci, &hci->snoop_channel, out_channel);
}

static bt_hci_protocol_ops_t hci_protocol_ops = {
    .open_command_channel = hci_open_command_channel,
    .open_acl_data_channel = hci_open_acl_data_channel,
    .open_snoop_channel = hci_open_snoop_channel,
};

static zx_status_t hci_get_protocol(void* ctx, uint32_t proto_id, void* protocol) {
    hci_t* hci = ctx;
    if (proto_id != ZX_PROTOCOL_BT_HCI) {
        // Pass this on for drivers to load firmware / initialize
        return device_get_protocol(hci->parent, proto_id, protocol);
    }

    bt_hci_protocol_t* hci_proto = protocol;

    hci_proto->ops = &hci_protocol_ops;
    hci_proto->ctx = ctx;
    return ZX_OK;
};

static zx_protocol_device_t hci_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = hci_get_protocol,
    .unbind = hci_unbind,
    .release = hci_release,
};

static zx_status_t hci_bind(void* ctx, zx_device_t* parent) {
    serial_protocol_t serial;

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_SERIAL, &serial);
    if (status != ZX_OK) {
        zxlogf(ERROR, "bt-transport-uart: get protocol ZX_PROTOCOL_SERIAL failed\n");
        return status;
    }

    hci_t* hci = calloc(1, sizeof(hci_t));
    if (!hci) {
        zxlogf(ERROR, "bt-transport-uart: Not enough memory for hci_t\n");
        return ZX_ERR_NO_MEMORY;
    }

    status = serial_open_socket(&serial, &hci->uart_socket);
     if (status != ZX_OK) {
        zxlogf(ERROR, "bt-transport-uart: serial_open_socket failed: %s\n",
               zx_status_get_string(status));
        goto fail;
    }

    zx_event_create(0, &hci->channels_changed_evt);
    mtx_init(&hci->mutex, mtx_plain);
    hci->parent = parent;
    hci->cur_uart_packet_type = HCI_NONE;

    // pre-populate event packet indicators
    hci->event_buffer[0] = HCI_EVENT;
    hci->event_buffer_offset = 1;
    hci->acl_buffer[0] = HCI_ACL_DATA;
    hci->acl_buffer_offset = 1;

    serial_port_info_t info;
    status = serial_get_info(&serial, &info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "hci_bind: serial_get_info failed\n");
        goto fail;
    }
    if (info.serial_class != SERIAL_CLASS_BLUETOOTH_HCI) {
        zxlogf(ERROR, "hci_bind: info.device_class != SERIAL_CLASS_BLUETOOTH_HCI\n");
        status = ZX_ERR_INTERNAL;
        goto fail;
    }

    // Copy the PID and VID from the platform device info so it can be filtered on
    // for HCI drivers
    zx_device_prop_t props[] = {
      { BIND_PROTOCOL, 0, ZX_PROTOCOL_BT_TRANSPORT },
      { BIND_SERIAL_VID, 0, info.serial_vid },
      { BIND_SERIAL_PID, 0, info.serial_pid },
    };

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "bt-transport-uart",
        .ctx = hci,
        .ops = &hci_device_proto,
        .proto_id = ZX_PROTOCOL_BT_TRANSPORT,
        .props = props,
        .prop_count = countof(props),
    };

    status = device_add(parent, &args, &hci->zxdev);
    if (status == ZX_OK) {
        return ZX_OK;
    }

fail:
    zxlogf(ERROR, "hci_bind: bind failed: %s\n", zx_status_get_string(status));
    hci_release(hci);
    return status;
}

static zx_driver_ops_t bt_hci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = hci_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(bt_transport_uart, bt_hci_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_SERIAL),
    BI_MATCH_IF(EQ, BIND_SERIAL_CLASS, SERIAL_CLASS_BLUETOOTH_HCI),
ZIRCON_DRIVER_END(bt_transport_uart)
