// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/bt-hci.h>
#include <ddk/protocol/usb.h>
#include <ddk/usb-request.h>
#include <driver/usb.h>
#include <zircon/device/bt-hci.h>
#include <zircon/listnode.h>
#include <zircon/status.h>
#include <zircon/syscalls/port.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#define EVENT_REQ_COUNT 8

// TODO(armansito): Consider increasing these.
#define ACL_READ_REQ_COUNT 8
#define ACL_WRITE_REQ_COUNT 8

// The maximum HCI ACL frame size used for data transactions
#define ACL_MAX_FRAME_SIZE 1028 // (1024 + 4 bytes for the ACL header)

#define CMD_BUF_SIZE 255 + 3   // 3 byte header + payload
#define EVENT_BUF_SIZE 255 + 2 // 2 byte header + payload

// The number of currently supported HCI channel endpoints. We currently have
// one channel for command/event flow and one for ACL data flow. The sniff channel is managed
// separately.
#define NUM_CHANNELS 2

#define NUM_WAIT_ITEMS NUM_CHANNELS + 1 // add one item for the changed event

// TODO(jamuraa): move these to hw/usb.h (or hw/bluetooth.h if that exists)
#define USB_SUBCLASS_BLUETOOTH 1
#define USB_PROTOCOL_BLUETOOTH 1

typedef struct {
    zx_device_t* zxdev;
    zx_device_t* usb_zxdev;
    usb_protocol_t usb;

    zx_handle_t cmd_channel;
    zx_handle_t acl_channel;
    zx_handle_t snoop_channel;

    // Port to queue PEER_CLOSED signals on
    zx_handle_t snoop_watch;

    // Signaled when a channel opens or closes
    zx_handle_t channels_changed_evt;

    zx_wait_item_t read_wait_items[NUM_WAIT_ITEMS];
    uint32_t read_wait_item_count;

    bool read_thread_running;

    void* intr_queue;

    // for accumulating HCI events
    uint8_t event_buffer[EVENT_BUF_SIZE];
    size_t event_buffer_offset;
    size_t event_buffer_packet_length;

    // pool of free USB requests
    list_node_t free_event_reqs;
    list_node_t free_acl_read_reqs;
    list_node_t free_acl_write_reqs;

    mtx_t mutex;
} hci_t;

static void queue_acl_read_requests_locked(hci_t* hci) {
    list_node_t* node;
    while ((node = list_remove_head(&hci->free_acl_read_reqs)) != NULL) {
        usb_request_t* req = containerof(node, usb_request_t, node);
        usb_request_queue(&hci->usb, req);
    }
}

static void queue_interrupt_requests_locked(hci_t* hci) {
    list_node_t* node;
    while ((node = list_remove_head(&hci->free_event_reqs)) != NULL) {
        usb_request_t* req = containerof(node, usb_request_t, node);
        usb_request_queue(&hci->usb, req);
    }
}

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
        printf("bt-transport-usb: failed to write to snoop channel: %s\n", zx_status_get_string(status));
        channel_cleanup_locked(hci, &hci->snoop_channel);
    }
}

static void hci_event_complete(usb_request_t* req, void* cookie) {
    hci_t* hci = (hci_t*)cookie;
    mtx_lock(&hci->mutex);

    // Handle the interrupt as long as either the command channel or the snoop
    // channel is open.
    if (hci->cmd_channel == ZX_HANDLE_INVALID && hci->snoop_channel == ZX_HANDLE_INVALID)
        goto out2;

    if (req->response.status == ZX_OK) {
        uint8_t* buffer;
        zx_status_t status = usb_req_mmap(&hci->usb, req, (void*)&buffer);
        if (status != ZX_OK) {
            printf("bt-transport-usb: usb_req_mmap failed: %s\n", zx_status_get_string(status));
            goto out2;
        }
        size_t length = req->response.actual;
        size_t packet_size = buffer[1] + 2;

        // simple case - packet fits in received data
        if (hci->event_buffer_offset == 0 && length >= 2) {
            if (packet_size == length) {
                if (hci->cmd_channel != ZX_HANDLE_INVALID) {
                    zx_status_t status = zx_channel_write(hci->cmd_channel, 0, buffer, length, NULL, 0);
                    if (status < 0) {
                        printf("bt-transport-usb: hci_event_complete failed to write: %s\n", zx_status_get_string(status));
                    }
                }
                snoop_channel_write_locked(hci, bt_hci_snoop_flags(BT_HCI_SNOOP_TYPE_EVT, true), buffer, length);
                goto out;
            }
        }

        // complicated case - need to accumulate into hci->event_buffer

        if (hci->event_buffer_offset + length > sizeof(hci->event_buffer)) {
            printf("bt-transport-usb: event_buffer would overflow!\n");
            goto out2;
        }

        memcpy(&hci->event_buffer[hci->event_buffer_offset], buffer, length);
        if (hci->event_buffer_offset == 0) {
            hci->event_buffer_packet_length = packet_size;
        } else {
            packet_size = hci->event_buffer_packet_length;
        }
        hci->event_buffer_offset += length;

        // check to see if we have a full packet
        if (packet_size <= hci->event_buffer_offset) {
            zx_status_t status = zx_channel_write(hci->cmd_channel, 0, hci->event_buffer,
                                                  packet_size, NULL, 0);
            if (status < 0) {
                printf("bt-transport-usb: failed to write: %s\n", zx_status_get_string(status));
            }

            snoop_channel_write_locked(hci, bt_hci_snoop_flags(BT_HCI_SNOOP_TYPE_EVT, true), hci->event_buffer, packet_size);

            uint32_t remaining = hci->event_buffer_offset - packet_size;
            memmove(hci->event_buffer, hci->event_buffer + packet_size, remaining);
            hci->event_buffer_offset = 0;
            hci->event_buffer_packet_length = 0;
        }
    }

out:
    list_add_head(&hci->free_event_reqs, &req->node);
    queue_interrupt_requests_locked(hci);
out2:
    mtx_unlock(&hci->mutex);
}

static void hci_acl_read_complete(usb_request_t* req, void* cookie) {
    hci_t* hci = (hci_t*)cookie;

    mtx_lock(&hci->mutex);

    if (req->response.status == ZX_OK) {
        void* buffer;
        zx_status_t status = usb_req_mmap(&hci->usb, req, &buffer);
        if (status != ZX_OK) {
            printf("bt-transport-usb: usb_req_mmap failed: %s\n", zx_status_get_string(status));
            mtx_unlock(&hci->mutex);
            return;
        }

        // The channel handle could be invalid here (e.g. if no one opened
        // the channel or it was closed). Instead of explicitly checking we
        // let zx_channel_write fail with ZX_ERR_BAD_HANDLE or ZX_ERR_PEER_CLOSED.
        status = zx_channel_write(hci->acl_channel, 0, buffer, req->response.actual, NULL, 0);
        if (status < 0) {
            printf("bt-transport-usb: hci_acl_read_complete failed to write: %s\n", zx_status_get_string(status));
        }

        // If the snoop channel is open then try to write the packet even if acl_channel was closed.
        snoop_channel_write_locked(
            hci, bt_hci_snoop_flags(BT_HCI_SNOOP_TYPE_ACL, true), buffer, req->response.actual);
    }

    list_add_head(&hci->free_acl_read_reqs, &req->node);
    queue_acl_read_requests_locked(hci);

    mtx_unlock(&hci->mutex);
}

static void hci_acl_write_complete(usb_request_t* req, void* cookie) {
    hci_t* hci = (hci_t*)cookie;

    // FIXME what to do with error here?
    mtx_lock(&hci->mutex);
    list_add_tail(&hci->free_acl_write_reqs, &req->node);

    if (hci->snoop_channel) {
        void* buffer;
        zx_status_t status = usb_req_mmap(&hci->usb, req, &buffer);
        if (status != ZX_OK) {
            printf("bt-transport-usb: usb_req_mmap failed: %s\n", zx_status_get_string(status));
            mtx_unlock(&hci->mutex);
            return;
        }

        snoop_channel_write_locked(
            hci, bt_hci_snoop_flags(BT_HCI_SNOOP_TYPE_ACL, false), buffer, req->response.actual);
    }

    mtx_unlock(&hci->mutex);
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
static void hci_handle_cmd_read_events(hci_t* hci, zx_wait_item_t* cmd_item) {
    if (cmd_item->pending & (ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED)) {
        uint8_t buf[CMD_BUF_SIZE];
        uint32_t length = sizeof(buf);
        zx_status_t status =
            zx_channel_read(cmd_item->handle, 0, buf, NULL, length, 0, &length, NULL);
        if (status < 0) {
            printf("hci_read_thread: failed to read from command channel %s\n",
                   zx_status_get_string(status));
            goto fail;
        }

        status = usb_control(&hci->usb, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_DEVICE,
                             0, 0, 0, buf, length, ZX_TIME_INFINITE, NULL);
        if (status < 0) {
            printf("hci_read_thread: usb_control failed: %s\n", zx_status_get_string(status));
            goto fail;
        }

        mtx_lock(&hci->mutex);
        snoop_channel_write_locked(hci, bt_hci_snoop_flags(BT_HCI_SNOOP_TYPE_CMD, false), buf, length);
        mtx_unlock(&hci->mutex);
    }

    return;

fail:
    mtx_lock(&hci->mutex);
    channel_cleanup_locked(hci, &hci->cmd_channel);
    mtx_unlock(&hci->mutex);
}

static void hci_handle_acl_read_events(hci_t* hci, zx_wait_item_t* acl_item) {
    if (acl_item->pending & (ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED)) {
        mtx_lock(&hci->mutex);
        list_node_t* node = list_peek_head(&hci->free_acl_write_reqs);
        mtx_unlock(&hci->mutex);

        // We don't have enough reqs. Simply punt the channel read until later.
        if (!node)
            return;

        uint8_t buf[ACL_MAX_FRAME_SIZE];
        uint32_t length = sizeof(buf);
        zx_status_t status =
            zx_channel_read(acl_item->handle, 0, buf, NULL, length, 0, &length, NULL);
        if (status < 0) {
            printf("hci_read_thread: failed to read from ACL channel %s\n",
                   zx_status_get_string(status));
            goto fail;
        }

        mtx_lock(&hci->mutex);
        node = list_remove_head(&hci->free_acl_write_reqs);
        mtx_unlock(&hci->mutex);

        // At this point if we don't get a free node from |free_acl_write_reqs| that means that
        // they were cleaned up in hci_release(). Just drop the packet.
        if (!node)
            return;

        usb_request_t* req = containerof(node, usb_request_t, node);
        usb_req_copy_to(&hci->usb, req, buf, length, 0);
        req->header.length = length;
        usb_request_queue(&hci->usb, req);
    }

    return;

fail:
    mtx_lock(&hci->mutex);
    channel_cleanup_locked(hci, &hci->acl_channel);
    mtx_unlock(&hci->mutex);
}

static bool hci_has_read_channels_locked(hci_t* hci) {
    // One for the signal event, any additional are read channels.
    return hci->read_wait_item_count > 1;
}

static int hci_read_thread(void* arg) {
    hci_t* hci = (hci_t*)arg;

    mtx_lock(&hci->mutex);

    if (!hci_has_read_channels_locked(hci)) {
        printf("bt-transport-usb: no channels are open - exiting\n");
        hci->read_thread_running = false;
        mtx_unlock(&hci->mutex);
        return 0;
    }

    mtx_unlock(&hci->mutex);

    while (1) {
        zx_status_t status = zx_object_wait_many(
            hci->read_wait_items, hci->read_wait_item_count, ZX_TIME_INFINITE);
        if (status < 0) {
            printf("bt-transport-usb: zx_object_wait_many failed (%s) - exiting\n",
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
            }
        }

        // The channels might have been changed by the *_read_events, recheck the event.
        status = zx_object_wait_one(hci->channels_changed_evt, ZX_EVENT_SIGNALED, 0u, NULL);
        if (status == ZX_OK) {
            hci_build_read_wait_items(hci);
            if (!hci_has_read_channels_locked(hci)) {
                printf("bt-transport-usb: all channels closed - exiting\n");
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
        printf("bt-transport-usb: already bound, failing\n");
        result = ZX_ERR_ALREADY_BOUND;
        goto done;
    }

    zx_status_t status = zx_channel_create(0, in_channel, out_channel);
    if (status < 0) {
        printf("bt-transport-usb: Failed to create channel: %s\n",
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

    mtx_lock(&hci->mutex);

    usb_request_t* req;
    while ((req = list_remove_head_type(&hci->free_event_reqs, usb_request_t, node)) != NULL) {
        usb_req_release(&hci->usb, req);
    }
    while ((req = list_remove_head_type(&hci->free_acl_read_reqs, usb_request_t, node)) != NULL) {
        usb_req_release(&hci->usb, req);
    }
    while ((req = list_remove_head_type(&hci->free_acl_write_reqs, usb_request_t, node)) != NULL) {
        usb_req_release(&hci->usb, req);
    }

    mtx_unlock(&hci->mutex);

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

    if (hci->snoop_watch == ZX_HANDLE_INVALID) {
        zx_status_t status = zx_port_create(0, &hci->snoop_watch);
        if (status != ZX_OK) {
            printf(
                "bt-transport-usb: failed to create a port to watch snoop channel: "
                "%s\n",
                zx_status_get_string(status));
            return status;
        }
    }

    zx_port_packet_t packet;
    zx_status_t status = zx_port_wait(hci->snoop_watch, 0, &packet);
    if (status == ZX_ERR_TIMED_OUT) {
        printf("bt-transport-usb: timed out: %s\n",
               zx_status_get_string(status));
    } else if (packet.signal.observed & ZX_CHANNEL_PEER_CLOSED) {
        hci->snoop_channel = ZX_HANDLE_INVALID;
    }

    zx_status_t ret = hci_open_channel(hci, &hci->snoop_channel, out_channel);
    if (ret == ZX_OK) {
        zx_signals_t sigs = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
        zx_object_wait_async(hci->snoop_channel, hci->snoop_watch, 0, sigs,
                             ZX_WAIT_ASYNC_REPEATING);
    }
    return ret;
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
        return device_get_protocol(hci->usb_zxdev, proto_id, protocol);
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

static zx_status_t hci_bind(void* ctx, zx_device_t* device) {
    usb_protocol_t usb;

    zx_status_t status = device_get_protocol(device, ZX_PROTOCOL_USB, &usb);
    if (status != ZX_OK) {
        printf("bt-transport-usb: get protocol failed: %s\n", zx_status_get_string(status));
        return status;
    }

    // find our endpoints
    usb_desc_iter_t iter;
    zx_status_t result = usb_desc_iter_init(&usb, &iter);
    if (result < 0) {
        printf("bt-transport-usb: usb iterator failed: %s\n", zx_status_get_string(status));
        return result;
    }

    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
    if (!intf || intf->bNumEndpoints != 3) {
        usb_desc_iter_release(&iter);
        return ZX_ERR_NOT_SUPPORTED;
    }

    uint8_t bulk_in_addr = 0;
    uint8_t bulk_out_addr = 0;
    uint8_t intr_addr = 0;
    uint16_t intr_max_packet = 0;

    usb_endpoint_descriptor_t* endp = usb_desc_iter_next_endpoint(&iter);
    while (endp) {
        if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
            if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                bulk_out_addr = endp->bEndpointAddress;
            }
        } else {
            if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                bulk_in_addr = endp->bEndpointAddress;
            } else if (usb_ep_type(endp) == USB_ENDPOINT_INTERRUPT) {
                intr_addr = endp->bEndpointAddress;
                intr_max_packet = usb_ep_max_packet(endp);
            }
        }
        endp = usb_desc_iter_next_endpoint(&iter);
    }
    usb_desc_iter_release(&iter);

    if (!bulk_in_addr || !bulk_out_addr || !intr_addr) {
        printf("bt-transport-usb: bind could not find endpoints\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    hci_t* hci = calloc(1, sizeof(hci_t));
    if (!hci) {
        printf("bt-transport-usb: Not enough memory for hci_t\n");
        return ZX_ERR_NO_MEMORY;
    }

    list_initialize(&hci->free_event_reqs);
    list_initialize(&hci->free_acl_read_reqs);
    list_initialize(&hci->free_acl_write_reqs);

    zx_event_create(0, &hci->channels_changed_evt);

    mtx_init(&hci->mutex, mtx_plain);

    hci->usb_zxdev = device;
    memcpy(&hci->usb, &usb, sizeof(hci->usb));

    for (int i = 0; i < EVENT_REQ_COUNT; i++) {
        usb_request_t* req;
        status = usb_req_alloc(&usb, &req, intr_max_packet, intr_addr);
        if (status != ZX_OK) {
            goto fail;
        }
        req->complete_cb = hci_event_complete;
        req->cookie = hci;
        list_add_head(&hci->free_event_reqs, &req->node);
    }
    for (int i = 0; i < ACL_READ_REQ_COUNT; i++) {
        usb_request_t* req;
        status = usb_req_alloc(&usb, &req, ACL_MAX_FRAME_SIZE, bulk_in_addr);
        if (status != ZX_OK) {
            goto fail;
        }
        req->complete_cb = hci_acl_read_complete;
        req->cookie = hci;
        list_add_head(&hci->free_acl_read_reqs, &req->node);
    }
    for (int i = 0; i < ACL_WRITE_REQ_COUNT; i++) {
        usb_request_t* req;
        status = usb_req_alloc(&usb, &req, ACL_MAX_FRAME_SIZE, bulk_out_addr);
        if (status != ZX_OK) {
            goto fail;
        }
        req->complete_cb = hci_acl_write_complete;
        req->cookie = hci;
        list_add_head(&hci->free_acl_write_reqs, &req->node);
    }

    mtx_lock(&hci->mutex);
    queue_interrupt_requests_locked(hci);
    queue_acl_read_requests_locked(hci);
    mtx_unlock(&hci->mutex);

    // Copy the PID and VID from the underlying BT so that it can be filtered on
    // for HCI drivers
    usb_device_descriptor_t dev_desc;
    usb_get_device_descriptor(&usb, &dev_desc);
    zx_device_prop_t props[] = {
      { BIND_PROTOCOL, 0, ZX_PROTOCOL_BT_TRANSPORT },
      { BIND_USB_VID, 0, dev_desc.idVendor },
      { BIND_USB_PID, 0, dev_desc.idProduct },
    };

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "bt_transport_usb",
        .ctx = hci,
        .ops = &hci_device_proto,
        .proto_id = ZX_PROTOCOL_BT_TRANSPORT,
        .props = props,
        .prop_count = countof(props),
    };

    status = device_add(device, &args, &hci->zxdev);
    if (status == ZX_OK) {
        return ZX_OK;
    }

fail:
    printf("bt-transport-usb: bind failed: %s\n", zx_status_get_string(status));
    hci_release(hci);
    return status;
}

static zx_driver_ops_t usb_bt_hci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = hci_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(bt_transport_usb, usb_bt_hci_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_CLASS, USB_CLASS_WIRELESS),
    BI_ABORT_IF(NE, BIND_USB_SUBCLASS, USB_SUBCLASS_BLUETOOTH),
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, USB_PROTOCOL_BLUETOOTH),
ZIRCON_DRIVER_END(bt_transport_usb)
