// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/usb/usb.h>
#include <lib/sync/completion.h>
#include <zircon/device/usb.h>
#include <zircon/device/usb-tester.h>
#include <zircon/hw/usb.h>

#include <stdlib.h>
#include <string.h>

#include "usb-tester.h"

#define REQ_MAX_LEN 0x10000  // 64 K
#define REQ_TIMEOUT_SECS 5
#define TEST_DUMMY_DATA 42

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))

#define ISOCH_START_FRAME_DELAY  5
#define ISOCH_ADDITIONAL_IN_REQS 8

typedef struct {
    uint8_t intf_num;
    uint8_t alt_setting;

    uint8_t in_addr;
    uint8_t out_addr;
    uint16_t in_max_packet;
    uint16_t out_max_packet;
} isoch_loopback_intf_t;

typedef struct {
    zx_device_t* parent;
    zx_device_t* zxdev;
    usb_protocol_t usb;

    uint8_t bulk_in_addr;
    uint8_t bulk_out_addr;

    isoch_loopback_intf_t isoch_loopback_intf;
} usb_tester_t;

typedef struct {
    usb_request_t* req;
    sync_completion_t completion;  // This will be passed as the request cookie.

    list_node_t node;
} test_req_t;

static void test_req_complete(usb_request_t* req, void* cookie) {
    sync_completion_signal((sync_completion_t*)cookie);
}

static zx_status_t test_req_alloc(usb_tester_t* usb_tester, size_t len, uint8_t ep_address,
                                  test_req_t** out_req) {
    test_req_t* test_req = malloc(sizeof(test_req_t));
    if (!test_req) {
        return ZX_ERR_NO_MEMORY;
    }
    test_req->completion = SYNC_COMPLETION_INIT;

    usb_request_t* req;
    zx_status_t status = usb_req_alloc(&usb_tester->usb, &req, len, ep_address);
    if (status != ZX_OK) {
        free(test_req);
        return status;
    }
    req->cookie = &test_req->completion;
    req->complete_cb = test_req_complete;
    test_req->req = req;

    *out_req = test_req;
    return ZX_OK;
}

static void test_req_release(usb_tester_t* usb_tester, test_req_t* test_req) {
    if (!test_req) {
        return;
    }
    if (test_req->req) {
        usb_req_release(&usb_tester->usb, test_req->req);
    }
    free(test_req);
}

// Waits for the request to complete and verifies its completion status and transferred length.
// Returns ZX_OK if the request completed successfully, and the transferred length equals the
// requested length.
static zx_status_t test_req_wait_complete(usb_tester_t* usb_tester, test_req_t* test_req) {
    usb_request_t* req = test_req->req;

    zx_status_t status = sync_completion_wait(&test_req->completion, ZX_SEC(REQ_TIMEOUT_SECS));
    if (status == ZX_OK) {
        status = req->response.status;
        if (status == ZX_OK) {
            if (req->response.actual != req->header.length) {
                status = ZX_ERR_IO;
            }
        } else if (status == ZX_ERR_IO_REFUSED) {
             usb_reset_endpoint(&usb_tester->usb, req->header.ep_address);
        }
        return status;
    } else if (status == ZX_ERR_TIMED_OUT) {
        // Cancel the request before returning.
        status = usb_cancel_all(&usb_tester->usb, req->header.ep_address);
        if (status != ZX_OK) {
            zxlogf(ERROR, "failed to cancel usb transfers, err: %d\n", status);
            return ZX_ERR_TIMED_OUT;
        }
        status = sync_completion_wait(&test_req->completion, ZX_TIME_INFINITE);
        if (status != ZX_OK) {
            zxlogf(ERROR, "failed to wait for request completion after cancelling request\n");
        }
        return ZX_ERR_TIMED_OUT;
    } else {
        return status;
    }
}

// Fills the given test request with data of the requested pattern.
static zx_status_t test_req_fill_data(usb_tester_t* usb_tester, test_req_t* test_req,
                                      uint32_t data_pattern) {
    uint8_t* buf;
    zx_status_t status = usb_req_mmap(&usb_tester->usb, test_req->req, (void**)&buf);
    if (status != ZX_OK) {
        return status;
    }
    for (size_t i = 0; i < test_req->req->header.length; ++i) {
        switch (data_pattern) {
        case USB_TESTER_DATA_PATTERN_CONSTANT:
            buf[i] = TEST_DUMMY_DATA;
            break;
        case USB_TESTER_DATA_PATTERN_RANDOM:
            buf[i] = rand();
            break;
        default:
            return ZX_ERR_INVALID_ARGS;
        }
    }
    return ZX_OK;
}

// Removes and frees the requests contained in the test_reqs list.
static void test_req_list_release_reqs(usb_tester_t* usb_tester, list_node_t* test_reqs) {
    test_req_t* test_req;
    test_req_t* temp;
    list_for_every_entry_safe(test_reqs, test_req, temp, test_req_t, node) {
        list_delete(&test_req->node);
        test_req_release(usb_tester, test_req);
    }
}

// Allocates the test requests and adds them to the out_test_reqs list.
static zx_status_t test_req_list_alloc(usb_tester_t* usb_tester, int num_reqs, size_t len,
                                       uint8_t ep_addr, list_node_t* out_test_reqs) {
    for (int i = 0; i < num_reqs; ++i) {
        test_req_t* test_req;
        zx_status_t status = test_req_alloc(usb_tester, len, ep_addr, &test_req);
        if (status != ZX_OK) {
            test_req_list_release_reqs(usb_tester, out_test_reqs);
            return status;
        }
        list_add_tail(out_test_reqs, &test_req->node);
    }
    return ZX_OK;
}

// Waits for the completion of each request contained in the test_reqs list in sequential order.
// The caller should check each request for its completion status.
static void test_req_list_wait_complete(usb_tester_t* usb_tester, list_node_t* test_reqs) {
    test_req_t* test_req;
    list_for_every_entry(test_reqs, test_req, test_req_t, node) {
        test_req_wait_complete(usb_tester, test_req);
    }
}

// Fills each request in the test_reqs list with data of the requested data_pattern.
static zx_status_t test_req_list_fill_data(usb_tester_t* usb_tester, list_node_t* test_reqs,
                                           uint32_t data_pattern) {
    test_req_t* test_req;
    list_for_every_entry(test_reqs, test_req, test_req_t, node) {
        zx_status_t status = test_req_fill_data(usb_tester, test_req, data_pattern);
        if (status != ZX_OK) {
            return status;
        }
    }
    return ZX_OK;
}

// Queues all requests contained in the test_reqs list.
static void test_req_list_queue(usb_tester_t* usb_tester, list_node_t* test_reqs,
                                uint64_t start_frame) {
    bool first = true;
    test_req_t* test_req;
    list_for_every_entry(test_reqs, test_req, test_req_t, node) {
        // Set the frame ID for the first request.
        // The following requests will be scheduled for ASAP after that.
        if (first) {
            test_req->req->header.frame = start_frame;
            first = false;
        }
        usb_request_queue(&usb_tester->usb, test_req->req);
    }
}

static zx_status_t usb_tester_set_mode_fwloader(usb_tester_t* usb_tester) {
    size_t out_len;
    zx_status_t status = usb_control(&usb_tester->usb,
                                     USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                                     USB_TESTER_SET_MODE_FWLOADER, 0, 0, NULL, 0,
                                     ZX_SEC(REQ_TIMEOUT_SECS), &out_len);
    if (status != ZX_OK) {
        zxlogf(ERROR, "failed to set mode fwloader, err: %d\n", status);
        return status;
    }
    return ZX_OK;
}

// Tests the loopback of data from the bulk OUT EP to the bulk IN EP.
static zx_status_t usb_tester_bulk_loopback(usb_tester_t* usb_tester,
                                            const usb_tester_params_t* params) {
    if (params->len > REQ_MAX_LEN) {
        return ZX_ERR_INVALID_ARGS;
    }
    test_req_t* out_req = NULL;
    test_req_t* in_req = NULL;

    zx_status_t status = test_req_alloc(usb_tester, params->len, usb_tester->bulk_out_addr,
                                        &out_req);
    if (status != ZX_OK) {
        goto done;
    }
    status = test_req_alloc(usb_tester, params->len, usb_tester->bulk_in_addr, &in_req);
    if (status != ZX_OK) {
        goto done;
    }
    status = test_req_fill_data(usb_tester, out_req, params->data_pattern);
    if (status != ZX_OK) {
        goto done;
    }
    usb_request_queue(&usb_tester->usb, out_req->req);
    usb_request_queue(&usb_tester->usb, in_req->req);

    zx_status_t out_status = test_req_wait_complete(usb_tester, out_req);
    zx_status_t in_status = test_req_wait_complete(usb_tester, in_req);
    status = out_status != ZX_OK ? out_status : in_status;
    if (status != ZX_OK) {
        goto done;
    }

    void* out_data;
    status = usb_req_mmap(&usb_tester->usb, out_req->req, &out_data);
    if (status != ZX_OK) {
        goto done;
    }
    void* in_data;
    status = usb_req_mmap(&usb_tester->usb, in_req->req, &in_data);
    if (status != ZX_OK) {
        goto done;
    }
    status = memcmp(in_data, out_data, params->len) == 0 ? ZX_OK : ZX_ERR_IO;

done:
    test_req_release(usb_tester, out_req);
    test_req_release(usb_tester, in_req);
    return status;
}

// Counts how many requests were successfully loopbacked between the OUT and IN EPs.
// Returns ZX_OK if no fatal error occurred during verification.
// out_num_passed will be populated with the number of successfully loopbacked requests.
static zx_status_t usb_tester_verify_loopback(usb_tester_t* usb_tester, list_node_t* out_reqs,
                                              list_node_t* in_reqs, size_t* out_num_passed) {
    size_t num_passed = 0;
    test_req_t* out_req_unmatched_start = list_next_type(out_reqs, out_reqs, test_req_t, node);
    test_req_t* in_req;
    list_for_every_entry(in_reqs, in_req, test_req_t, node) {
        // You can't transfer an isochronous request of length zero.
        if (in_req->req->response.status != ZX_OK || in_req->req->response.actual == 0) {
            zxlogf(TRACE, "skipping isoch req, status %d, read len %lu\n",
                   in_req->req->response.status, in_req->req->response.actual);
            continue;
        }
        void* in_data;
        zx_status_t status = usb_req_mmap(&usb_tester->usb, in_req->req, &in_data);
        if (status != ZX_OK) {
            return status;
        }
        // We will start searching the OUT requests from after the last matched OUT request to
        // preserve expected ordering.
        test_req_t* out_req = out_req_unmatched_start;
        bool matched = false;
        while (out_req && !matched) {
            if (out_req->req->response.status == ZX_OK &&
                out_req->req->response.actual == in_req->req->response.actual) {
                void* out_data;
                status = usb_req_mmap(&usb_tester->usb, out_req->req, &out_data);
                if (status != ZX_OK) {
                    return status;
                }
                matched = memcmp(in_data, out_data, out_req->req->response.actual) == 0;
            }
            out_req = list_next_type(out_reqs, &out_req->node, test_req_t, node);
        }
        if (matched) {
            out_req_unmatched_start = out_req;
            num_passed++;
        } else {
            // Maybe IN data was corrupted.
            zxlogf(TRACE, "could not find matching isoch req\n");
        }
    }
    *out_num_passed = num_passed;
    return ZX_OK;
}

static zx_status_t usb_tester_isoch_loopback(usb_tester_t* usb_tester,
                                             const usb_tester_params_t* params,
                                             usb_tester_result_t* result) {
    if (params->len > REQ_MAX_LEN) {
        return ZX_ERR_INVALID_ARGS;
    }
    isoch_loopback_intf_t* intf = &usb_tester->isoch_loopback_intf;

    zx_status_t status = usb_set_interface(&usb_tester->usb, intf->intf_num, intf->alt_setting);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_set_interface got err: %d\n", status);
        goto done;
    }
    // TODO(jocelyndang): optionally allow the user to specify a packet size.
    uint16_t packet_size = MIN(intf->in_max_packet, intf->out_max_packet);
    size_t num_reqs = DIV_ROUND_UP(params->len, packet_size);

    zxlogf(TRACE, "allocating %lu reqs of packet size %u, total bytes %lu\n",
           num_reqs, packet_size, params->len);

    list_node_t in_reqs = LIST_INITIAL_VALUE(in_reqs);
    list_node_t out_reqs = LIST_INITIAL_VALUE(out_reqs);
    // We will likely get a few empty IN requests, as there is a delay between the start of an
    // OUT transfer and it being received. Allocate a few more IN requests to account for this.
    status = test_req_list_alloc(usb_tester, num_reqs + ISOCH_ADDITIONAL_IN_REQS, packet_size,
                                 intf->in_addr, &in_reqs);
    if (status != ZX_OK) {
        goto done;
    }
    status = test_req_list_alloc(usb_tester, num_reqs, packet_size, intf->out_addr, &out_reqs);
    if (status != ZX_OK) {
        goto done;
    }
    status = test_req_list_fill_data(usb_tester, &out_reqs, params->data_pattern);
    if (status != ZX_OK) {
        goto done;
    }

    // Find the current frame so we can schedule OUT and IN requests to start simultaneously.
    uint64_t frame;
    size_t read_len;
    status = device_ioctl(usb_tester->parent, IOCTL_USB_GET_CURRENT_FRAME,
                          NULL, 0, &frame, sizeof(frame), &read_len);
    if (status != ZX_OK || read_len != sizeof(frame)) {
        zxlogf(ERROR, "failed to get frame, err: %d, read %lu, want %lu\n",
               status, read_len, sizeof(frame));
        goto done;
    }
    // Adds some delay so we don't miss the scheduled start frame.
    uint64_t start_frame = frame + ISOCH_START_FRAME_DELAY;
    zxlogf(TRACE, "scheduling isoch loopback to start on frame %lu\n", start_frame);

    test_req_list_queue(usb_tester, &in_reqs, start_frame);
    test_req_list_queue(usb_tester, &out_reqs, start_frame);

    test_req_list_wait_complete(usb_tester, &out_reqs);
    test_req_list_wait_complete(usb_tester, &in_reqs);

    size_t num_passed = 0;
    status = usb_tester_verify_loopback(usb_tester, &out_reqs, &in_reqs, &num_passed);
    if (status != ZX_OK) {
        goto done;
    }
    result->num_passed = num_passed;
    result->num_packets = num_reqs;
    zxlogf(TRACE, "%lu / %lu passed\n", num_passed, num_reqs);

done:;
    zx_status_t res = usb_set_interface(&usb_tester->usb, intf->intf_num, 0);
    if (res != ZX_OK) {
        zxlogf(ERROR, "could not switch back to isoch interface default alternate setting\n");
    }
    test_req_list_release_reqs(usb_tester, &out_reqs);
    test_req_list_release_reqs(usb_tester, &in_reqs);
    return status;
}

static zx_status_t usb_tester_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                    void* out_buf, size_t out_len, size_t* out_actual) {
    usb_tester_t* usb_tester = ctx;

    switch (op) {
    case IOCTL_USB_TESTER_SET_MODE_FWLOADER:
        return usb_tester_set_mode_fwloader(usb_tester);
    case IOCTL_USB_TESTER_BULK_LOOPBACK: {
        if (in_buf == NULL || in_len != sizeof(usb_tester_params_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        return usb_tester_bulk_loopback(usb_tester, in_buf);
    }
    case IOCTL_USB_TESTER_ISOCH_LOOPBACK: {
        if (in_buf == NULL || in_len != sizeof(usb_tester_params_t) ||
            out_buf == NULL || out_len != sizeof(usb_tester_result_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        usb_tester_result_t* result = out_buf;
        zx_status_t status = usb_tester_isoch_loopback(usb_tester, in_buf, result);
        if (status != ZX_OK) {
            return status;
        }
        *out_actual = sizeof(*result);
        return ZX_OK;
    }
    default:
        return device_ioctl(usb_tester->parent, op, in_buf, in_len, out_buf, out_len, out_actual);
    }
}

static void usb_tester_free(usb_tester_t* ctx) {
    free(ctx);
}

static void usb_tester_unbind(void* ctx) {
    zxlogf(INFO, "usb_tester_unbind\n");
    usb_tester_t* usb_tester = ctx;

    device_remove(usb_tester->zxdev);
}

static void usb_tester_release(void* ctx) {
    usb_tester_t* usb_tester = ctx;
    usb_tester_free(usb_tester);
}

static zx_protocol_device_t usb_tester_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = usb_tester_ioctl,
    .unbind = usb_tester_unbind,
    .release = usb_tester_release,
};

static bool want_interface(usb_interface_descriptor_t* intf, void* arg) {
    return intf->bInterfaceClass == USB_CLASS_VENDOR;
}

static zx_status_t usb_tester_bind(void* ctx, zx_device_t* device) {
    zxlogf(TRACE, "usb_tester_bind\n");

    usb_tester_t* usb_tester = calloc(1, sizeof(usb_tester_t));
    if (!usb_tester) {
        return ZX_ERR_NO_MEMORY;
    }
    usb_tester->parent = device;

    zx_status_t status = device_get_protocol(device, ZX_PROTOCOL_USB, &usb_tester->usb);
    if (status != ZX_OK) {
        goto error_return;
    }
    status = usb_claim_additional_interfaces(&usb_tester->usb, want_interface, NULL);
    if (status != ZX_OK) {
        goto error_return;
    }
    // Find the endpoints.
    usb_desc_iter_t iter;
    status = usb_desc_iter_init(&usb_tester->usb, &iter);
    if (status != ZX_OK) {
        goto error_return;
    }

    usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, false);
    while (intf) {
        isoch_loopback_intf_t isoch_intf = {
            .intf_num = intf->bInterfaceNumber,
            .alt_setting = intf->bAlternateSetting
        };

        usb_endpoint_descriptor_t* endp = usb_desc_iter_next_endpoint(&iter);
        while (endp) {
            switch (usb_ep_type(endp)) {
            case USB_ENDPOINT_BULK:
                if (usb_ep_direction(endp) == USB_ENDPOINT_IN) {
                    usb_tester->bulk_in_addr = endp->bEndpointAddress;
                    zxlogf(TRACE, "usb_tester found bulk in ep: %x\n", usb_tester->bulk_in_addr);
                } else {
                    usb_tester->bulk_out_addr = endp->bEndpointAddress;
                    zxlogf(TRACE, "usb_tester found bulk out ep: %x\n", usb_tester->bulk_out_addr);
                }
                break;
            case USB_ENDPOINT_ISOCHRONOUS:
                if (usb_ep_direction(endp) == USB_ENDPOINT_IN) {
                    isoch_intf.in_addr = endp->bEndpointAddress;
                    isoch_intf.in_max_packet = usb_ep_max_packet(endp);
                } else {
                    isoch_intf.out_addr = endp->bEndpointAddress;
                    isoch_intf.out_max_packet = usb_ep_max_packet(endp);
                }
                break;
            }
            endp = usb_desc_iter_next_endpoint(&iter);
        }
        if (isoch_intf.in_addr && isoch_intf.out_addr) {
            // Found isoch loopback endpoints.
            memcpy(&usb_tester->isoch_loopback_intf, &isoch_intf, sizeof(isoch_intf));
            zxlogf(TRACE, "usb tester found isoch loopback eps: %x (%u) %x (%u), intf %u %u\n",
                   isoch_intf.in_addr, isoch_intf.in_max_packet,
                   isoch_intf.out_addr, isoch_intf.out_max_packet,
                   isoch_intf.intf_num, isoch_intf.alt_setting);
        }
        intf = usb_desc_iter_next_interface(&iter, false);
    }
    usb_desc_iter_release(&iter);

    // Check we found the pair of bulk endpoints.
    if (!usb_tester->bulk_in_addr || !usb_tester->bulk_out_addr) {
        zxlogf(ERROR, "usb_bind could not find bulk endpoints\n");
        status = ZX_ERR_NOT_SUPPORTED;
        goto error_return;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "usb-tester",
        .ctx = usb_tester,
        .ops = &usb_tester_device_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
        .proto_id = ZX_PROTOCOL_USB_TESTER,
    };

    status = device_add(device, &args, &usb_tester->zxdev);
    if (status != ZX_OK) {
        goto error_return;
    }
    return ZX_OK;

error_return:
    usb_tester_free(usb_tester);
    return status;
}

static zx_driver_ops_t usb_tester_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_tester_bind,
};

ZIRCON_DRIVER_BEGIN(usb_tester, usb_tester_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_VID, GOOGLE_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, USB_TESTER_PID),
ZIRCON_DRIVER_END(usb_tester)
