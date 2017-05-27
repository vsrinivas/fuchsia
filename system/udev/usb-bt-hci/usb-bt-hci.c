// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/common/usb.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <magenta/device/bt-hci.h>
#include <magenta/listnode.h>
#include <magenta/status.h>

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

#define CMD_BUF_SIZE 255 + 3   // 3 byte header + payload
#define EVENT_BUF_SIZE 255 + 2 // 2 byte header + payload

// The number of currently supported HCI channel endpoints. We currently have
// one channel for command/event flow and one for ACL data flow. The sniff channel is managed
// separately.
#define NUM_CHANNELS 2

// Uncomment these to force using a particular Bluetooth module
// #define USB_VID 0x0a12  // CSR
// #define USB_PID 0x0001

typedef struct {
    mx_device_t* mxdev;
    mx_device_t* usb_mxdev;

    mx_handle_t cmd_channel;
    mx_handle_t acl_channel;
    mx_handle_t snoop_channel;

    mx_wait_item_t read_wait_items[NUM_CHANNELS];
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
        iotxn_t* txn = containerof(node, iotxn_t, node);
        iotxn_queue(hci->usb_mxdev, txn);
    }
}

static void queue_interrupt_requests_locked(hci_t* hci) {
    list_node_t* node;
    while ((node = list_remove_head(&hci->free_event_reqs)) != NULL) {
        iotxn_t* txn = containerof(node, iotxn_t, node);
        iotxn_queue(hci->usb_mxdev, txn);
    }
}

static void cmd_channel_cleanup_locked(hci_t* hci) {
    if (hci->cmd_channel == MX_HANDLE_INVALID) return;

    mx_handle_close(hci->cmd_channel);
    hci->cmd_channel = MX_HANDLE_INVALID;
}

static void acl_channel_cleanup_locked(hci_t* hci) {
    if (hci->acl_channel == MX_HANDLE_INVALID) return;

    mx_handle_close(hci->acl_channel);
    hci->acl_channel = MX_HANDLE_INVALID;
}

static void snoop_channel_cleanup_locked(hci_t* hci) {
    if (hci->snoop_channel == MX_HANDLE_INVALID) return;

    mx_handle_close(hci->snoop_channel);
    hci->snoop_channel = MX_HANDLE_INVALID;
}

static void snoop_channel_write_locked(hci_t* hci, uint8_t flags, uint8_t* bytes, size_t length) {
    if (hci->snoop_channel == MX_HANDLE_INVALID)
        return;

    // We tack on a flags byte to the beginning of the payload.
    uint8_t snoop_buffer[length + 1];
    snoop_buffer[0] = flags;
    memcpy(snoop_buffer + 1, bytes, length);
    mx_status_t status = mx_channel_write(hci->snoop_channel, 0, snoop_buffer, length + 1, NULL, 0);
    if (status < 0) {
        printf("usb-bt-hci: failed to write to snoop channel: %s\n", mx_status_get_string(status));
        snoop_channel_cleanup_locked(hci);
    }
}

static void hci_event_complete(iotxn_t* txn, void* cookie) {
    hci_t* hci = (hci_t*)cookie;
    mtx_lock(&hci->mutex);

    // Handle the interrupt as long as either the command channel or the snoop
    // channel is open.
    if (hci->cmd_channel == MX_HANDLE_INVALID && hci->snoop_channel == MX_HANDLE_INVALID)
        goto out2;

    if (txn->status == NO_ERROR) {
        uint8_t* buffer;
        iotxn_mmap(txn, (void **)&buffer);
        size_t length = txn->actual;
        size_t packet_size = buffer[1] + 2;

        // simple case - packet fits in received data
        if (hci->event_buffer_offset == 0 && length >= 2) {
            if (packet_size == length) {
                if (hci->cmd_channel != MX_HANDLE_INVALID) {
                    mx_status_t status = mx_channel_write(hci->cmd_channel, 0, buffer, length, NULL, 0);
                    if (status < 0) {
                        printf("hci_interrupt failed to write: %s\n", mx_status_get_string(status));
                    }
                }
                snoop_channel_write_locked(hci, BT_HCI_SNOOP_FLAG_RECEIVED, buffer, length);
                goto out;
            }
        }

        // complicated case - need to accumulate into hci->event_buffer

        if (hci->event_buffer_offset + length > sizeof(hci->event_buffer)) {
            printf("hci->event_buffer would overflow!\n");
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
            mx_status_t status = mx_channel_write(hci->cmd_channel, 0, hci->event_buffer,
                                                  packet_size, NULL, 0);
            if (status < 0) {
                printf("hci_interrupt failed to write: %s\n", mx_status_get_string(status));
            }

            snoop_channel_write_locked(hci, BT_HCI_SNOOP_FLAG_RECEIVED, hci->event_buffer, packet_size);

            uint32_t remaining = hci->event_buffer_offset - packet_size;
            memmove(hci->event_buffer, hci->event_buffer + packet_size, remaining);
            hci->event_buffer_offset = 0;
            hci->event_buffer_packet_length = 0;
        }
    }

out:
    list_add_head(&hci->free_event_reqs, &txn->node);
    queue_interrupt_requests_locked(hci);
out2:
    mtx_unlock(&hci->mutex);
}

static void hci_acl_read_complete(iotxn_t* txn, void* cookie) {
    hci_t* hci = (hci_t*)cookie;

    mtx_lock(&hci->mutex);

    if (txn->status == NO_ERROR) {
        void* buffer;
        iotxn_mmap(txn, &buffer);

        // The channel handle could be invalid here (e.g. if no process called
        // the ioctl or they closed their endpoint). Instead of explicitly
        // checking we let mx_channel_write fail with ERR_BAD_HANDLE or
        // ERR_PEER_CLOSED.
        mx_status_t status = mx_channel_write(hci->acl_channel, 0, buffer, txn->actual, NULL, 0);
        if (status < 0) {
            printf("hci_acl_read_complete failed to write: %s\n", mx_status_get_string(status));
        }

        // If the snoop channel is open then try to write the packet even if acl_channel was closed.
        snoop_channel_write_locked(
            hci, BT_HCI_SNOOP_FLAG_DATA | BT_HCI_SNOOP_FLAG_RECEIVED, buffer, txn->actual);
    }

    list_add_head(&hci->free_acl_read_reqs, &txn->node);
    queue_acl_read_requests_locked(hci);

    mtx_unlock(&hci->mutex);
}

static void hci_acl_write_complete(iotxn_t* txn, void* cookie) {
    hci_t* hci = (hci_t*)cookie;

    // FIXME what to do with error here?
    mtx_lock(&hci->mutex);
    list_add_tail(&hci->free_acl_write_reqs, &txn->node);

    if (hci->snoop_channel) {
        void* buffer;
        iotxn_mmap(txn, &buffer);
        snoop_channel_write_locked(
            hci, BT_HCI_SNOOP_FLAG_DATA | BT_HCI_SNOOP_FLAG_SENT, buffer, txn->actual);
    }

    mtx_unlock(&hci->mutex);
}

static void hci_build_read_wait_items_locked(hci_t* hci) {
    mx_wait_item_t* items = hci->read_wait_items;
    memset(items, 0, sizeof(hci->read_wait_items));
    uint32_t count = 0;

    if (hci->cmd_channel != MX_HANDLE_INVALID) {
        items[count].handle = hci->cmd_channel;
        items[count].waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
        count++;
    }

    if (hci->acl_channel != MX_HANDLE_INVALID) {
        items[count].handle = hci->acl_channel;
        items[count].waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
        count++;
    }

    hci->read_wait_item_count = count;
}

static void hci_build_read_wait_items(hci_t* hci) {
    mtx_lock(&hci->mutex);
    hci_build_read_wait_items_locked(hci);
    mtx_unlock(&hci->mutex);
}

// Returns false if there's an error while sending the packet to the hardware or
// if the channel peer closed its endpoint.
static bool hci_handle_cmd_read_events(hci_t* hci, mx_wait_item_t* cmd_item) {
    if (cmd_item->pending & (MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED)) {
        uint8_t buf[CMD_BUF_SIZE];
        uint32_t length = sizeof(buf);
        mx_status_t status =
            mx_channel_read(cmd_item->handle, 0, buf, NULL, length, 0, &length, NULL);
        if (status < 0) {
            printf("hci_read_thread: failed to read from command channel %s\n",
                   mx_status_get_string(status));
            goto fail;
        }

        status = usb_control(hci->usb_mxdev,
                             USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_DEVICE,
                             0, 0, 0, buf, length);
        if (status < 0) {
            printf("hci_read_thread: usb_control failed: %s\n", mx_status_get_string(status));
            goto fail;
        }

        mtx_lock(&hci->mutex);
        snoop_channel_write_locked(hci, BT_HCI_SNOOP_FLAG_SENT, buf, length);
        mtx_unlock(&hci->mutex);
    }

    return true;

fail:
    mtx_lock(&hci->mutex);
    cmd_channel_cleanup_locked(hci);
    mtx_unlock(&hci->mutex);

    return false;
}

static bool hci_handle_acl_read_events(hci_t* hci, mx_wait_item_t* acl_item) {
    if (acl_item->pending & (MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED)) {
        mtx_lock(&hci->mutex);
        list_node_t* node = list_peek_head(&hci->free_acl_write_reqs);
        mtx_unlock(&hci->mutex);

        // We don't have enough iotxn's. Simply punt the channel read until later.
        if (!node) return node;

        uint8_t buf[BT_HCI_MAX_FRAME_SIZE];
        uint32_t length = sizeof(buf);
        mx_status_t status =
            mx_channel_read(acl_item->handle, 0, buf, NULL, length, 0, &length, NULL);
        if (status < 0) {
            printf("hci_read_thread: failed to read from ACL channel %s\n",
                   mx_status_get_string(status));
            goto fail;
        }

        mtx_lock(&hci->mutex);
        node = list_remove_head(&hci->free_acl_write_reqs);
        mtx_unlock(&hci->mutex);

        // At this point if we don't get a free node from |free_acl_write_reqs| that means that
        // they were cleaned up in hci_release(). Just drop the packet.
        if (!node) return true;

        iotxn_t* txn = containerof(node, iotxn_t, node);
        iotxn_copyto(txn, buf, length, 0);
        txn->length = length;
        iotxn_queue(hci->usb_mxdev, txn);
    }

    return true;

fail:
    mtx_lock(&hci->mutex);
    acl_channel_cleanup_locked(hci);
    mtx_unlock(&hci->mutex);

    return false;
}

static int hci_read_thread(void* arg) {
    hci_t* hci = (hci_t*)arg;

    mtx_lock(&hci->mutex);

    if (hci->read_wait_item_count == 0) {
        printf("hci_read_thread: no channels are open - exiting\n");
        mtx_unlock(&hci->mutex);
        goto done;
    }

    mtx_unlock(&hci->mutex);

    while (1) {
        mx_status_t status = mx_object_wait_many(
            hci->read_wait_items, hci->read_wait_item_count, MX_TIME_INFINITE);
        if (status < 0) {
            printf("hci_read_thread: mx_object_wait_many failed: %s\n",
                   mx_status_get_string(status));
            mtx_lock(&hci->mutex);
            cmd_channel_cleanup_locked(hci);
            acl_channel_cleanup_locked(hci);
            mtx_unlock(&hci->mutex);
            break;
        }

        for (unsigned i = 0; i < NUM_CHANNELS; ++i) {
            mtx_lock(&hci->mutex);
            mx_wait_item_t item = hci->read_wait_items[i];
            mtx_unlock(&hci->mutex);

            mx_handle_t handle = hci->read_wait_items[i].handle;
            if ((handle == hci->cmd_channel && !hci_handle_cmd_read_events(hci, &item)) ||
                (handle == hci->acl_channel && !hci_handle_acl_read_events(hci, &item))) {
                // There was an error while handling the read events. Rebuild the
                // wait items array to see if any channels are still open.
                hci_build_read_wait_items(hci);
                if (hci->read_wait_item_count == 0) {
                    printf("hci_read_thread: all channels closed - exiting\n");
                    goto done;
                }
            }
        }
    }

done:
    mtx_lock(&hci->mutex);
    hci->read_thread_running = false;
    mtx_unlock(&hci->mutex);

    printf("hci_read_thread: exiting\n");

    return 0;
}

static mx_status_t hci_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                            void* out_buf, size_t out_len, size_t* out_actual) {
    ssize_t result = ERR_NOT_SUPPORTED;
    hci_t* hci = ctx;

    mtx_lock(&hci->mutex);

    if (op == IOCTL_BT_HCI_GET_COMMAND_CHANNEL) {
        mx_handle_t* reply = out_buf;
        if (out_len < sizeof(*reply)) {
            result = ERR_BUFFER_TOO_SMALL;
            goto done;
        }

        if (hci->cmd_channel != MX_HANDLE_INVALID) {
            result = ERR_ALREADY_BOUND;
            goto done;
        }

        mx_handle_t remote_end;
        mx_status_t status = mx_channel_create(0, &hci->cmd_channel, &remote_end);
        if (status < 0) {
            printf("hci_ioctl: Failed to create command channel: %s\n",
                   mx_status_get_string(status));
            result = ERR_INTERNAL;
            goto done;
        }

        *reply = remote_end;
        *out_actual = sizeof(*reply);
        result = NO_ERROR;
    } else if (op == IOCTL_BT_HCI_GET_ACL_DATA_CHANNEL) {
        mx_handle_t* reply = out_buf;
        if (out_len < sizeof(*reply)) {
            result = ERR_BUFFER_TOO_SMALL;
            goto done;
        }

        if (hci->acl_channel != MX_HANDLE_INVALID) {
            result = ERR_ALREADY_BOUND;
            goto done;
        }

        mx_handle_t remote_end;
        mx_status_t status = mx_channel_create(0, &hci->acl_channel, &remote_end);
        if (status < 0) {
            printf("hci_ioctl: Failed to create ACL data channel: %s\n",
                   mx_status_get_string(status));
            result = ERR_INTERNAL;
            goto done;
        }

        *reply = remote_end;
        *out_actual = sizeof(*reply);
        result = NO_ERROR;
    } else if (op == IOCTL_BT_HCI_GET_SNOOP_CHANNEL) {
        mx_handle_t* reply = out_buf;
        if (out_len < sizeof(*reply)) {
            result = ERR_BUFFER_TOO_SMALL;
            goto done;
        }

        if (hci->snoop_channel != MX_HANDLE_INVALID) {
            result = ERR_ALREADY_BOUND;
            goto done;
        }

        mx_handle_t remote_end;
        mx_status_t status = mx_channel_create(0, &hci->snoop_channel, &remote_end);
        if (status < 0) {
            printf("hci_ioctl: Failed to create snoop channel: %s\n",
                   mx_status_get_string(status));
            result = ERR_INTERNAL;
            goto done;
        }

        *reply = remote_end;
        *out_actual = sizeof(*reply);
        result = NO_ERROR;
    }

    hci_build_read_wait_items_locked(hci);

    // Kick off the hci_read_thread if it's not already running.
    if (result == NO_ERROR && !hci->read_thread_running) {
        thrd_t read_thread;
        thrd_create_with_name(&read_thread, hci_read_thread, hci, "hci_read_thread");
        hci->read_thread_running = true;
        thrd_detach(read_thread);
    }

done:
    mtx_unlock(&hci->mutex);
    return result;
}

static void hci_unbind(void* ctx) {
    hci_t* hci = ctx;

    // Close the transport channels so that the host stack is notified of device removal.
    mtx_lock(&hci->mutex);

    cmd_channel_cleanup_locked(hci);
    acl_channel_cleanup_locked(hci);
    snoop_channel_cleanup_locked(hci);

    mtx_unlock(&hci->mutex);

    device_remove(hci->mxdev);
}

static void hci_release(void* ctx) {
    hci_t* hci = ctx;

    mtx_lock(&hci->mutex);

    iotxn_t* txn;
    while ((txn = list_remove_head_type(&hci->free_event_reqs, iotxn_t, node)) != NULL) {
        iotxn_release(txn);
    }
    while ((txn = list_remove_head_type(&hci->free_acl_read_reqs, iotxn_t, node)) != NULL) {
        iotxn_release(txn);
    }
    while ((txn = list_remove_head_type(&hci->free_acl_write_reqs, iotxn_t, node)) != NULL) {
        iotxn_release(txn);
    }

    mtx_unlock(&hci->mutex);

    free(hci);
}

static mx_protocol_device_t hci_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = hci_ioctl,
    .unbind = hci_unbind,
    .release = hci_release,
};

static mx_status_t hci_bind(void* ctx, mx_device_t* device, void** cookie) {
    // find our endpoints
    usb_desc_iter_t iter;
    mx_status_t result = usb_desc_iter_init(device, &iter);
    if (result < 0) return result;

    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
    if (!intf || intf->bNumEndpoints != 3) {
        usb_desc_iter_release(&iter);
        return ERR_NOT_SUPPORTED;
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
        printf("hci_bind could not find endpoints\n");
        return ERR_NOT_SUPPORTED;
    }

    hci_t* hci = calloc(1, sizeof(hci_t));
    if (!hci) {
        printf("Not enough memory for hci_t\n");
        return ERR_NO_MEMORY;
    }

    list_initialize(&hci->free_event_reqs);
    list_initialize(&hci->free_acl_read_reqs);
    list_initialize(&hci->free_acl_write_reqs);

    mtx_init(&hci->mutex, mtx_plain);

    hci->usb_mxdev = device;

    mx_status_t status = NO_ERROR;

    for (int i = 0; i < EVENT_REQ_COUNT; i++) {
        iotxn_t* txn = usb_alloc_iotxn(intr_addr, intr_max_packet);
        if (!txn) {
            status = ERR_NO_MEMORY;
            goto fail;
        }
        txn->length = intr_max_packet;
        txn->complete_cb = hci_event_complete;
        txn->cookie = hci;
        list_add_head(&hci->free_event_reqs, &txn->node);
    }
    for (int i = 0; i < ACL_READ_REQ_COUNT; i++) {
        iotxn_t* txn = usb_alloc_iotxn(bulk_in_addr, BT_HCI_MAX_FRAME_SIZE);
        if (!txn) {
            status = ERR_NO_MEMORY;
            goto fail;
        }
        txn->length = BT_HCI_MAX_FRAME_SIZE;
        txn->complete_cb = hci_acl_read_complete;
        txn->cookie = hci;
        list_add_head(&hci->free_acl_read_reqs, &txn->node);
    }
    for (int i = 0; i < ACL_WRITE_REQ_COUNT; i++) {
        iotxn_t* txn = usb_alloc_iotxn(bulk_out_addr, BT_HCI_MAX_FRAME_SIZE);
        if (!txn) {
            status = ERR_NO_MEMORY;
            goto fail;
        }
        txn->length = BT_HCI_MAX_FRAME_SIZE;
        txn->complete_cb = hci_acl_write_complete;
        txn->cookie = hci;
        list_add_head(&hci->free_acl_write_reqs, &txn->node);
    }

    mtx_lock(&hci->mutex);
    queue_interrupt_requests_locked(hci);
    queue_acl_read_requests_locked(hci);
    mtx_unlock(&hci->mutex);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "usb_bt_hci",
        .ctx = hci,
        .ops = &hci_device_proto,
        .proto_id = MX_PROTOCOL_BLUETOOTH_HCI,
    };

    status = device_add(device, &args, &hci->mxdev);
    if (status == NO_ERROR) return NO_ERROR;

fail:
    printf("hci_bind failed: %s\n", mx_status_get_string(status));
    hci_release(hci);
    return status;
}

static mx_driver_ops_t usb_bt_hci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = hci_bind,
};

MAGENTA_DRIVER_BEGIN(usb_bt_hci, usb_bt_hci_driver_ops, "magenta", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB),
#if defined(USB_VID) && defined(USB_PID)
    BI_ABORT_IF(NE, BIND_USB_VID, USB_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, USB_PID),
    BI_ABORT(),
#else
    BI_ABORT_IF(NE, BIND_USB_CLASS, 224),
    BI_ABORT_IF(NE, BIND_USB_SUBCLASS, 1),
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, 1),
#endif
MAGENTA_DRIVER_END(usb_bt_hci)
