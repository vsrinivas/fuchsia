// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/common/usb.h>
#include <ddk/protocol/bluetooth-hci.h>
#include <magenta/listnode.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#define EVENT_REQ_COUNT 8
#define ACL_READ_REQ_COUNT 8
#define ACL_WRITE_REQ_COUNT 8
#define ACL_BUF_SIZE 2048

// Uncomment these to force using a particular Bluetooth module
// #define USB_VID 0x0a12  // CSR
// #define USB_PID 0x0001

typedef struct {
    mx_device_t device;
    mx_device_t* usb_device;

    mx_handle_t control_pipe[2];
    mx_handle_t acl_pipe[2];

    void* intr_queue;

    // for accumulating HCI events
    uint8_t event_buffer[2 + 255]; // 2 byte header and 0 - 255 data
    size_t event_buffer_offset;
    size_t event_buffer_packet_length;

    // pool of free USB requests
    list_node_t free_event_reqs;
    list_node_t free_acl_read_reqs;
    list_node_t free_acl_write_reqs;

    mtx_t mutex;
} hci_t;
#define get_hci(dev) containerof(dev, hci_t, device)

static void queue_acl_read_requests_locked(hci_t* hci) {
    list_node_t* node;
    while ((node = list_remove_head(&hci->free_acl_read_reqs)) != NULL) {
        iotxn_t* txn = containerof(node, iotxn_t, node);
        iotxn_queue(hci->usb_device, txn);
    }
}

static void queue_interrupt_requests_locked(hci_t* hci) {
    list_node_t* node;
    while ((node = list_remove_head(&hci->free_event_reqs)) != NULL) {
        iotxn_t* txn = containerof(node, iotxn_t, node);
        iotxn_queue(hci->usb_device, txn);
    }
}

static void hci_event_complete(iotxn_t* txn, void* cookie) {
    hci_t* hci = (hci_t*)cookie;
    mtx_lock(&hci->mutex);
    if (txn->status == NO_ERROR) {
        uint8_t* buffer;
        txn->ops->mmap(txn, (void **)&buffer);
        size_t length = txn->actual;
        size_t packet_size = buffer[1] + 2;

        // simple case - packet fits in received data
        if (hci->event_buffer_offset == 0 && length >= 2) {
            if (packet_size == length) {
                mx_status_t status = mx_msgpipe_write(hci->control_pipe[0], buffer, length,
                                                            NULL, 0, 0);
                if (status < 0) {
                    printf("hci_interrupt failed to write\n");
                }
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
            mx_status_t status = mx_msgpipe_write(hci->control_pipe[0], hci->event_buffer,
                                                        packet_size, NULL, 0, 0);
            if (status < 0) {
                printf("hci_interrupt failed to write\n");
            }
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

    if (txn->status == NO_ERROR) {
        void* buffer;
        txn->ops->mmap(txn, &buffer);
        mx_status_t status = mx_msgpipe_write(hci->acl_pipe[0], buffer, txn->actual, NULL, 0, 0);
        if (status < 0) {
            printf("hci_acl_read_complete failed to write\n");
        }
    }

    mtx_lock(&hci->mutex);
    list_add_head(&hci->free_acl_read_reqs, &txn->node);
    queue_acl_read_requests_locked(hci);
    mtx_unlock(&hci->mutex);
}

static void hci_acl_write_complete(iotxn_t* txn, void* cookie) {
    hci_t* hci = (hci_t*)cookie;

    // FIXME what to do with error here?
    mtx_lock(&hci->mutex);
    list_add_tail(&hci->free_acl_write_reqs, &txn->node);
    mtx_unlock(&hci->mutex);
}

static int hci_read_thread(void* arg) {
    hci_t* hci = (hci_t*)arg;

    mx_handle_t handles[2];
    handles[0] = hci->control_pipe[0];
    handles[1] = hci->acl_pipe[0];
    mx_signals_t signals[2];
    signals[0] = MX_SIGNAL_READABLE;
    signals[1] = MX_SIGNAL_READABLE;

    while (1) {
        mx_signals_state_t signals_state[2];

        mx_status_t status = mx_handle_wait_many(countof(handles), handles, signals,
                                                       MX_TIME_INFINITE, NULL, signals_state);
        if (status < 0) {
            printf("mx_handle_wait_many fail\n");
            break;
        }
        if (signals_state[0].satisfied & MX_SIGNAL_READABLE) {
            uint8_t buf[256];
            uint32_t length = sizeof(buf);
            status = mx_msgpipe_read(handles[0], buf, &length, NULL, 0, 0);
            if (status >= 0) {
                status = usb_control(hci->usb_device,
                                     USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_DEVICE,
                                     0, 0, 0, buf, length);
                if (status < 0) {
                    printf("hci_read_thread control failed\n");
                }
            } else {
                printf("event read failed\n");
                break;
            }
        }
        if (signals_state[1].satisfied & MX_SIGNAL_READABLE) {
            uint8_t buf[ACL_BUF_SIZE];
            uint32_t length = sizeof(buf);
            status = mx_msgpipe_read(handles[1], buf, &length, NULL, 0, 0);
            if (status >= 0) {
                mtx_lock(&hci->mutex);

                list_node_t* node;
                do {
                    node = list_remove_head(&hci->free_acl_write_reqs);
                    if (!node) {
                        // FIXME this is nasty
                        mtx_unlock(&hci->mutex);
                        usleep(10 * 1000);
                        mtx_lock(&hci->mutex);
                    }
                } while (!node);
                mtx_unlock(&hci->mutex);

                iotxn_t* txn = containerof(node, iotxn_t, node);
                txn->ops->copyto(txn, buf, length, 0);
                txn->length = length;
                iotxn_queue(hci->usb_device, txn);
            }
        }
    }
    return 0;
}

static mx_handle_t hci_get_control_pipe(mx_device_t* device) {
    hci_t* hci = get_hci(device);
    return hci->control_pipe[1];
}

static mx_handle_t hci_get_acl_pipe(mx_device_t* device) {
    hci_t* hci = get_hci(device);
    return hci->acl_pipe[1];
}

static bluetooth_hci_protocol_t hci_proto = {
    .get_control_pipe = hci_get_control_pipe,
    .get_acl_pipe = hci_get_acl_pipe,
};

static void hci_unbind(mx_device_t* device) {
    hci_t* hci = get_hci(device);
    device_remove(&hci->device);
}

static mx_status_t hci_release(mx_device_t* device) {
    hci_t* hci = get_hci(device);

    iotxn_t* txn;
    while ((txn = list_remove_head_type(&hci->free_event_reqs, iotxn_t, node)) != NULL) {
        txn->ops->release(txn);
    }
    while ((txn = list_remove_head_type(&hci->free_acl_read_reqs, iotxn_t, node)) != NULL) {
        txn->ops->release(txn);
    }
    while ((txn = list_remove_head_type(&hci->free_acl_write_reqs, iotxn_t, node)) != NULL) {
        txn->ops->release(txn);
    }

    mx_handle_close(hci->control_pipe[0]);
    mx_handle_close(hci->control_pipe[1]);
    mx_handle_close(hci->acl_pipe[0]);
    mx_handle_close(hci->acl_pipe[1]);
    free(hci);

    return NO_ERROR;
}

static mx_protocol_device_t hci_device_proto = {
    .unbind = hci_unbind,
    .release = hci_release,
};

static mx_status_t hci_bind(mx_driver_t* driver, mx_device_t* device) {
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

    mx_status_t status = mx_msgpipe_create(hci->control_pipe, 0);
    if (status < 0) {
        goto fail;
    }
    status = mx_msgpipe_create(hci->acl_pipe, 0);
    if (status < 0) {
        goto fail;
    }

    list_initialize(&hci->free_event_reqs);
    list_initialize(&hci->free_acl_read_reqs);
    list_initialize(&hci->free_acl_write_reqs);

    hci->usb_device = device;

    for (int i = 0; i < EVENT_REQ_COUNT; i++) {
        iotxn_t* txn = usb_alloc_iotxn(intr_addr, intr_max_packet, 0);
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
        iotxn_t* txn = usb_alloc_iotxn(bulk_in_addr, ACL_BUF_SIZE, 0);
        if (!txn) {
            status = ERR_NO_MEMORY;
            goto fail;
        }
        txn->length = ACL_BUF_SIZE;
        txn->complete_cb = hci_acl_read_complete;
        txn->cookie = hci;
        list_add_head(&hci->free_acl_read_reqs, &txn->node);
    }
    for (int i = 0; i < ACL_WRITE_REQ_COUNT; i++) {
        iotxn_t* txn = usb_alloc_iotxn(bulk_out_addr, ACL_BUF_SIZE, 0);
        if (!txn) {
            status = ERR_NO_MEMORY;
            goto fail;
        }
        txn->length = ACL_BUF_SIZE;
        txn->complete_cb = hci_acl_write_complete;
        txn->cookie = hci;
        list_add_head(&hci->free_acl_write_reqs, &txn->node);
    }

    device_init(&hci->device, driver, "usb_bt_hci", &hci_device_proto);

    mtx_lock(&hci->mutex);
    queue_interrupt_requests_locked(hci);
    queue_acl_read_requests_locked(hci);
    mtx_unlock(&hci->mutex);

    thrd_t thread;
    thrd_create_with_name(&thread, hci_read_thread, hci, "hci_read_thread");
    thrd_detach(thread);

    hci->device.protocol_id = MX_PROTOCOL_BLUETOOTH_HCI;
    hci->device.protocol_ops = &hci_proto;
    status = device_add(&hci->device, device);
    if (status == NO_ERROR) return NO_ERROR;

fail:
    printf("hci_bind failed: %d\n", status);
    hci_release(&hci->device);
    return status;
}

mx_driver_t _driver_usb_bt_hci = {
    .ops = {
        .bind = hci_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_usb_bt_hci, "usb-bt-hci", "magenta", "0.1", 4)
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
MAGENTA_DRIVER_END(_driver_usb_bt_hci)
