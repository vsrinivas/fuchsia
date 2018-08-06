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

typedef struct {
    zx_device_t* zxdev;
    usb_protocol_t usb;

    uint8_t bulk_in_addr;
    uint8_t bulk_out_addr;
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
static void test_req_list_queue(usb_tester_t* usb_tester, list_node_t* test_reqs) {
    test_req_t* test_req;
    list_for_every_entry(test_reqs, test_req, test_req_t, node) {
        usb_request_queue(&usb_tester->usb, test_req->req);
    }
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

static zx_status_t usb_tester_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                  void* out_buf, size_t out_len, size_t* out_actual) {
    usb_tester_t* usb_tester = ctx;

    switch (op) {
    case IOCTL_USB_TESTER_BULK_LOOPBACK: {
        if (in_buf == NULL || in_len != sizeof(usb_tester_params_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        return usb_tester_bulk_loopback(usb_tester, in_buf);
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
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
        usb_endpoint_descriptor_t* endp = usb_desc_iter_next_endpoint(&iter);
        while (endp) {
            uint8_t ep_type = usb_ep_type(endp);
            if (ep_type == USB_ENDPOINT_BULK) {
                if (usb_ep_direction(endp) == USB_ENDPOINT_IN) {
                    usb_tester->bulk_in_addr = endp->bEndpointAddress;
                    zxlogf(TRACE, "usb_tester found bulk in ep: %x\n", usb_tester->bulk_in_addr);
                } else {
                    usb_tester->bulk_out_addr = endp->bEndpointAddress;
                    zxlogf(TRACE, "usb_tester found bulk out ep: %x\n", usb_tester->bulk_out_addr);
                }
            }
            endp = usb_desc_iter_next_endpoint(&iter);
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
