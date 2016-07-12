// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/bluetooth-hci.h>
#include <ddk/protocol/usb-device.h>
#include <system/listnode.h>
#include <runtime/mutex.h>
#include <runtime/thread.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    usb_device_protocol_t* device_protocol;

    mx_handle_t control_pipe[2];
    mx_handle_t acl_pipe[2];

    void* intr_queue;

    usb_endpoint_t* bulk_in;
    usb_endpoint_t* bulk_out;
    usb_endpoint_t* intr_ep;

    // for accumulating HCI events
    uint8_t event_buffer[2 + 255]; // 2 byte header and 0 - 255 data
    size_t event_buffer_offset;
    size_t event_buffer_packet_length;

    // pool of free USB requests
    list_node_t free_event_reqs;
    list_node_t free_acl_read_reqs;
    list_node_t free_acl_write_reqs;

    mxr_mutex_t mutex;
} hci_t;
#define get_hci(dev) containerof(dev, hci_t, device)

static void queue_acl_read_requests_locked(hci_t* hci) {
    list_node_t* node;
    while ((node = list_remove_head(&hci->free_acl_read_reqs)) != NULL) {
        usb_request_t* req = containerof(node, usb_request_t, node);
        req->transfer_length = req->buffer_length;
        mx_status_t status = hci->device_protocol->queue_request(hci->usb_device, req);
        if (status != NO_ERROR) {
            printf("bulk queue queue_request %d\n", status);
            list_add_head(&hci->free_event_reqs, &req->node);
            break;
        }
    }
}

static void queue_interrupt_requests_locked(hci_t* hci) {
    list_node_t* node;
    while ((node = list_remove_head(&hci->free_event_reqs)) != NULL) {
        usb_request_t* req = containerof(node, usb_request_t, node);
        req->transfer_length = req->buffer_length;
        mx_status_t status = hci->device_protocol->queue_request(hci->usb_device, req);
        if (status != NO_ERROR) {
            printf("interrupt queue_request failed %d\n", status);
            list_add_head(&hci->free_event_reqs, &req->node);
            break;
        }
    }
}

static void hci_event_complete(usb_request_t* request) {
    hci_t* hci = (hci_t*)request->client_data;
    mxr_mutex_lock(&hci->mutex);
    if (request->status == NO_ERROR) {
        uint8_t* buffer = request->buffer;
        size_t length = request->transfer_length;

        // simple case - packet fits in received data
        if (hci->event_buffer_offset == 0 && length >= 2) {
            size_t packet_size = buffer[1] + 2;
            if (packet_size == length) {
                mx_status_t status = mx_message_write(hci->control_pipe[0], buffer, length,
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
        size_t packet_size;
        if (hci->event_buffer_offset == 0) {
            packet_size = buffer[1] + 2;
            hci->event_buffer_packet_length = packet_size;
        } else {
            packet_size = hci->event_buffer_packet_length;
        }
        hci->event_buffer_offset += length;

        // check to see if we have a full packet
        packet_size = hci->event_buffer[1] + 2;
        if (packet_size <= hci->event_buffer_offset) {
            mx_status_t status = mx_message_write(hci->control_pipe[0], hci->event_buffer,
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
    list_add_head(&hci->free_event_reqs, &request->node);
    queue_interrupt_requests_locked(hci);
out2:
    mxr_mutex_unlock(&hci->mutex);
}

static void hci_acl_read_complete(usb_request_t* request) {
    hci_t* hci = (hci_t*)request->client_data;

    if (request->status == NO_ERROR) {
        mx_status_t status = mx_message_write(hci->acl_pipe[0], request->buffer,
                                                    request->transfer_length, NULL, 0, 0);
        if (status < 0) {
            printf("hci_acl_read_complete failed to write\n");
        }
    }

    mxr_mutex_lock(&hci->mutex);
    list_add_head(&hci->free_acl_read_reqs, &request->node);
    queue_acl_read_requests_locked(hci);
    mxr_mutex_unlock(&hci->mutex);
}

static void hci_acl_write_complete(usb_request_t* request) {
    hci_t* hci = (hci_t*)request->client_data;

    // FIXME what to do with error here?
    mxr_mutex_lock(&hci->mutex);
    list_add_tail(&hci->free_acl_write_reqs, &request->node);
    mxr_mutex_unlock(&hci->mutex);
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
        mx_signals_t satisfied_signals[2];
        mx_signals_t satisfiable_signals[2];

        mx_status_t status = mx_handle_wait_many(countof(handles), handles, signals,
                                                       MX_TIME_INFINITE, satisfied_signals,
                                                       satisfiable_signals);
        if (status < 0) {
            printf("mx_handle_wait_many fail\n");
            break;
        }
        if (satisfied_signals[0] & MX_SIGNAL_READABLE) {
            uint8_t buf[256];
            uint32_t length = sizeof(buf);
            status = mx_message_read(handles[0], buf, &length, NULL, 0, 0);
            if (status >= 0) {
                status = hci->device_protocol->control(hci->usb_device,
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
        if (satisfied_signals[1] & MX_SIGNAL_READABLE) {
            uint8_t buf[ACL_BUF_SIZE];
            uint32_t length = sizeof(buf);
            status = mx_message_read(handles[1], buf, &length, NULL, 0, 0);
            if (status >= 0) {
                mxr_mutex_lock(&hci->mutex);

                list_node_t* node;
                do {
                    node = list_remove_head(&hci->free_acl_write_reqs);
                    if (!node) {
                        // FIXME this is nasty
                        mxr_mutex_unlock(&hci->mutex);
                        usleep(10 * 1000);
                        mxr_mutex_lock(&hci->mutex);
                    }
                } while (!node);
                mxr_mutex_unlock(&hci->mutex);

                usb_request_t* request = containerof(node, usb_request_t, node);
                memcpy(request->buffer, buf, length);
                request->transfer_length = length;
                status = hci->device_protocol->queue_request(hci->usb_device, request);
                if (status < 0) {
                    printf("hci_read_thread bulk write failed\n");
                    break;
                }
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

static mx_status_t hci_release(mx_device_t* device) {
    hci_t* hci = get_hci(device);
    free(hci);

    return NO_ERROR;
}

static mx_protocol_device_t hci_device_proto = {
    .release = hci_release,
};

static mx_status_t hci_bind(mx_driver_t* driver, mx_device_t* device) {
    usb_device_protocol_t* protocol;
    if (device_get_protocol(device, MX_PROTOCOL_USB_DEVICE, (void**)&protocol)) {
        return ERR_NOT_SUPPORTED;
    }
    usb_device_config_t* device_config;
    mx_status_t status = protocol->get_config(device, &device_config);
    if (status < 0)
        return status;

    // find our endpoints
    usb_configuration_t* config = &device_config->configurations[0];
    usb_interface_t* intf = &config->interfaces[0];
    if (intf->num_endpoints != 3) {
        printf("hci_bind wrong number of endpoints: %d\n", intf->num_endpoints);
        return ERR_NOT_SUPPORTED;
    }
    usb_endpoint_t* bulk_in = NULL;
    usb_endpoint_t* bulk_out = NULL;
    usb_endpoint_t* intr_ep = NULL;

    for (int i = 0; i < intf->num_endpoints; i++) {
        usb_endpoint_t* endp = &intf->endpoints[i];
        if (endp->direction == USB_ENDPOINT_OUT) {
            if (endp->type == USB_ENDPOINT_BULK) {
                bulk_out = endp;
            }
        } else {
            if (endp->type == USB_ENDPOINT_BULK) {
                bulk_in = endp;
            } else if (endp->type == USB_ENDPOINT_INTERRUPT) {
                intr_ep = endp;
            }
        }
    }
    if (!bulk_in || !bulk_out || !intr_ep) {
        printf("hci_bind could not find endpoints\n");
        return ERR_NOT_SUPPORTED;
    }

    hci_t* hci = calloc(1, sizeof(hci_t));
    if (!hci) {
        printf("Not enough memory for hci_t\n");
        return ERR_NO_MEMORY;
    }

    hci->control_pipe[0] = mx_message_pipe_create(&hci->control_pipe[1]);
    if (hci->control_pipe[0] < 0) {
        free(hci);
        return ERR_NO_MEMORY;
    }
    hci->acl_pipe[0] = mx_message_pipe_create(&hci->acl_pipe[1]);
    if (hci->acl_pipe[0] < 0) {
        mx_handle_close(hci->control_pipe[0]);
        mx_handle_close(hci->control_pipe[1]);
        free(hci);
        return ERR_NO_MEMORY;
    }

    list_initialize(&hci->free_event_reqs);
    list_initialize(&hci->free_acl_read_reqs);
    list_initialize(&hci->free_acl_write_reqs);

    hci->usb_device = device;
    hci->device_protocol = protocol;
    hci->bulk_in = bulk_in;
    hci->bulk_out = bulk_out;
    hci->intr_ep = intr_ep;

    for (int i = 0; i < EVENT_REQ_COUNT; i++) {
        usb_request_t* req = protocol->alloc_request(device, intr_ep, intr_ep->maxpacketsize);
        if (!req)
            return ERR_NO_MEMORY;
        req->complete_cb = hci_event_complete;
        req->client_data = hci;
        list_add_head(&hci->free_event_reqs, &req->node);
    }
    for (int i = 0; i < ACL_READ_REQ_COUNT; i++) {
        usb_request_t* req = protocol->alloc_request(device, bulk_in, ACL_BUF_SIZE);
        if (!req)
            return ERR_NO_MEMORY;
        req->complete_cb = hci_acl_read_complete;
        req->client_data = hci;
        list_add_head(&hci->free_acl_read_reqs, &req->node);
    }
    for (int i = 0; i < ACL_WRITE_REQ_COUNT; i++) {
        usb_request_t* req = protocol->alloc_request(device, bulk_out, ACL_BUF_SIZE);
        if (!req)
            return ERR_NO_MEMORY;
        req->complete_cb = hci_acl_write_complete;
        req->client_data = hci;
        list_add_head(&hci->free_acl_write_reqs, &req->node);
    }

    status = device_init(&hci->device, driver, "usb_bt_hci", &hci_device_proto);
    if (status != NO_ERROR) {
        free(hci);
        return status;
    }

    mxr_mutex_lock(&hci->mutex);
    queue_interrupt_requests_locked(hci);
    queue_acl_read_requests_locked(hci);
    mxr_mutex_unlock(&hci->mutex);

    mxr_thread_t* thread;
    mxr_thread_create(hci_read_thread, hci, "hci_read_thread", &thread);
    mxr_thread_detach(thread);

    hci->device.protocol_id = MX_PROTOCOL_BLUETOOTH_HCI;
    hci->device.protocol_ops = &hci_proto;
    device_add(&hci->device, device);

    return NO_ERROR;
}

static mx_status_t hci_unbind(mx_driver_t* drv, mx_device_t* dev) {
    // TODO - cleanup
    return NO_ERROR;
}

static mx_bind_inst_t binding[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB_DEVICE),
#if defined(USB_VID) && defined(USB_PID)
    BI_ABORT_IF(NE, BIND_USB_VID, USB_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, USB_PID),
#else
    BI_ABORT_IF(NE, BIND_USB_CLASS, 224),
    BI_ABORT_IF(NE, BIND_USB_SUBCLASS, 1),
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, 1),
#endif
};

mx_driver_t _driver_usb_bt_hci BUILTIN_DRIVER = {
    .name = "usb_bt_hci",
    .ops = {
        .bind = hci_bind,
        .unbind = hci_unbind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};
