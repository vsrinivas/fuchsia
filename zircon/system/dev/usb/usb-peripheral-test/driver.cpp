// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb/function.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <usb/usb-request.h>
#include <zircon/listnode.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/device/usb-peripheral-test.h>
#include <zircon/device/usb-peripheral.h>

namespace usb_function_test {

static constexpr size_t BULK_TX_COUNT = 16;
static constexpr size_t BULK_RX_COUNT = 16;
static constexpr size_t INTR_COUNT = 8;

static constexpr size_t BULK_MAX_PACKET = 512;  // FIXME(voydanoff) USB 3.0 support.
static constexpr size_t BULK_REQ_SIZE = 4096;   // FIXME(voydanoff) Increase this when DCI drivers support
                                                //                  non-contiguous DMA buffers.
static constexpr size_t INTR_REQ_SIZE = 1024;

struct usb_test_t {
    zx_device_t* zxdev;
    usb_function_protocol_t function;

    // These are lists of usb_request_t.
    list_node_t bulk_out_reqs __TA_GUARDED(lock);
    list_node_t bulk_in_reqs __TA_GUARDED(lock);
    list_node_t intr_reqs __TA_GUARDED(lock);

    uint8_t test_data[INTR_REQ_SIZE];
    size_t test_data_length;

    bool configured;

    fbl::Mutex lock;

    uint8_t bulk_out_addr;
    uint8_t bulk_in_addr;
    uint8_t intr_addr;
    size_t parent_req_size;
};

namespace {

struct {
    usb_interface_descriptor_t intf;
    usb_endpoint_descriptor_t intr_ep;
    usb_endpoint_descriptor_t bulk_out_ep;
    usb_endpoint_descriptor_t bulk_in_ep;
} descriptors = {
    .intf = {
        .bLength = sizeof(usb_interface_descriptor_t),
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 0, // set later
        .bAlternateSetting = 0,
        .bNumEndpoints = 3,
        .bInterfaceClass = USB_CLASS_VENDOR,
        .bInterfaceSubClass = 0,
        .bInterfaceProtocol = 0,
        .iInterface = 0,
    },
    .intr_ep = {
        .bLength = sizeof(usb_endpoint_descriptor_t),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 0, // set later
        .bmAttributes = USB_ENDPOINT_INTERRUPT,
        .wMaxPacketSize = htole16(INTR_REQ_SIZE),
        .bInterval = 8,
    },
    .bulk_out_ep = {
        .bLength = sizeof(usb_endpoint_descriptor_t),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 0, // set later
        .bmAttributes = USB_ENDPOINT_BULK,
        .wMaxPacketSize = htole16(BULK_MAX_PACKET),
        .bInterval = 0,
    },
    .bulk_in_ep = {
        .bLength = sizeof(usb_endpoint_descriptor_t),
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 0, // set later
        .bmAttributes = USB_ENDPOINT_BULK,
        .wMaxPacketSize = htole16(BULK_MAX_PACKET),
        .bInterval = 0,
    },
};

static void test_bulk_in_complete(void* ctx, usb_request_t* req);

static void test_intr_complete(void* ctx, usb_request_t* req) {
    auto* test = static_cast<usb_test_t*>(ctx);

    zxlogf(LTRACE, "%s %d %ld\n", __func__, req->response.status, req->response.actual);

    fbl::AutoLock lock(&test->lock);
    zx_status_t status = usb_req_list_add_tail(&test->intr_reqs, req, test->parent_req_size);
    ZX_DEBUG_ASSERT(status == ZX_OK);
}

static void test_bulk_out_complete(void* ctx, usb_request_t* req) {
    auto* test = static_cast<usb_test_t*>(ctx);

    zxlogf(LTRACE, "%s %d %ld\n", __func__, req->response.status, req->response.actual);

    if (req->response.status == ZX_ERR_IO_NOT_PRESENT) {
        fbl::AutoLock lock(&test->lock);
        zx_status_t status = usb_req_list_add_head(&test->bulk_out_reqs, req,
                                                   test->parent_req_size);
        ZX_DEBUG_ASSERT(status == ZX_OK);
        return;
    }
    if (req->response.status == ZX_OK) {
        test->lock.Acquire();
        usb_request_t* in_req = usb_req_list_remove_head(&test->bulk_in_reqs,
                                                         test->parent_req_size);
        test->lock.Release();
        if (in_req) {
            // Send data back to host.
            void* buffer;
            usb_request_mmap(req, &buffer);
            usb_request_copy_to(in_req, buffer, req->response.actual, 0);
            req->header.length = req->response.actual;

            usb_request_complete_t complete = {
                .callback = test_bulk_in_complete,
                .ctx = test,
            };
            usb_function_request_queue(&test->function, in_req, &complete);
        } else {
            zxlogf(ERROR, "%s: no bulk in request available\n", __func__);
        }
    } else {
        zxlogf(ERROR, "%s: usb_read_complete called with status %d\n",
                __func__, req->response.status);
    }

    // Requeue read.
    usb_request_complete_t complete = {
        .callback = test_bulk_out_complete,
        .ctx = test,
    };
    usb_function_request_queue(&test->function, req, &complete);
}

static void test_bulk_in_complete(void* ctx, usb_request_t* req) {
    auto* test = static_cast<usb_test_t*>(ctx);

    zxlogf(LTRACE, "%s %d %ld\n", __func__, req->response.status, req->response.actual);

    fbl::AutoLock lock(&test->lock);
    zx_status_t status = usb_req_list_add_tail(&test->bulk_in_reqs, req, test->parent_req_size);
    ZX_DEBUG_ASSERT(status == ZX_OK);
}

static size_t test_get_descriptors_size(void* ctx) {
    return sizeof(descriptors);
}

static void test_get_descriptors(void* ctx, void* buffer, size_t buffer_size, size_t* out_actual) {
    size_t length = sizeof(descriptors);
    if (length > buffer_size) {
        length = buffer_size;
    }
    memcpy(buffer, &descriptors, length);
    *out_actual = length;
}

static zx_status_t test_control(void* ctx, const usb_setup_t* setup, const void* write_buffer,
                                size_t write_size, void* read_buffer, size_t read_size,
                                size_t* out_read_actual) {
    auto* test = static_cast<usb_test_t*>(ctx);
    size_t length = le16toh(setup->wLength);

    zxlogf(TRACE, "%s\n", __func__);
    if (setup->bmRequestType == (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) &&
        setup->bRequest == USB_PERIPHERAL_TEST_SET_DATA) {
        if (length > sizeof(test->test_data)) {
            length = sizeof(test->test_data);
        }
        memcpy(test->test_data, write_buffer, length);
        test->test_data_length = length;
        return ZX_OK;
    } else if (setup->bmRequestType == (USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) &&
        setup->bRequest == USB_PERIPHERAL_TEST_GET_DATA) {
        if (length > test->test_data_length) {
            length = test->test_data_length;
        }
        memcpy(read_buffer, test->test_data, length);
        *out_read_actual = length;
        return ZX_OK;
    } else if (setup->bmRequestType == (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) &&
        setup->bRequest == USB_PERIPHERAL_TEST_SEND_INTERUPT) {
        test->lock.Acquire();
        usb_request_t* req = usb_req_list_remove_head(&test->intr_reqs, test->parent_req_size);
        test->lock.Release();
        if (!req) {
            zxlogf(ERROR, "%s: no interrupt request available\n", __func__);
            // TODO(voydanoff) maybe stall in this case?
            return ZX_OK;
        }

        usb_request_copy_to(req, test->test_data, test->test_data_length, 0);
        req->header.length = test->test_data_length;

        usb_request_complete_t complete = {
            .callback = test_intr_complete,
            .ctx = test,
        };
        usb_function_request_queue(&test->function, req, &complete);
        return ZX_OK;
    } else {
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_status_t test_set_configured(void* ctx, bool configured, usb_speed_t speed) {
    zxlogf(TRACE, "%s: %d %d\n", __func__, configured, speed);
    auto* test = static_cast<usb_test_t*>(ctx);
    zx_status_t status;

    if (configured) {
        if ((status = usb_function_config_ep(&test->function, &descriptors.intr_ep, NULL))
                != ZX_OK ||
            (status = usb_function_config_ep(&test->function, &descriptors.bulk_out_ep, NULL))
                != ZX_OK ||
            (status = usb_function_config_ep(&test->function, &descriptors.bulk_in_ep, NULL))
                != ZX_OK) {
            zxlogf(ERROR, "%s: usb_function_config_ep failed\n", __func__);
            return status;
        }
    } else {
        usb_function_disable_ep(&test->function, test->bulk_out_addr);
        usb_function_disable_ep(&test->function, test->bulk_in_addr);
        usb_function_disable_ep(&test->function, test->intr_addr);
    }
    test->configured = configured;

    if (configured) {
        // Queue our OUT requests.
        usb_request_t* req;
        while ((req = usb_req_list_remove_head(&test->bulk_out_reqs, test->parent_req_size)) != NULL) {
            usb_request_complete_t complete = {
                .callback = test_bulk_out_complete,
                .ctx = test,
            };
            usb_function_request_queue(&test->function, req, &complete);
        }
    }

    return ZX_OK;
}

static zx_status_t test_set_interface(void* ctx, uint8_t interface, uint8_t alt_setting) {
    return ZX_ERR_NOT_SUPPORTED;
}

static usb_function_interface_protocol_ops_t device_ops = {
    .get_descriptors_size = test_get_descriptors_size,
    .get_descriptors = test_get_descriptors,
    .control = test_control,
    .set_configured = test_set_configured,
    .set_interface = test_set_interface,
};

static void usb_test_unbind(void* ctx) {
    zxlogf(TRACE, "%s\n", __func__);
    auto* test = static_cast<usb_test_t*>(ctx);

    device_remove(test->zxdev);
}

static void usb_test_release(void* ctx) {
    zxlogf(TRACE, "%s\n", __func__);
    auto* test = static_cast<usb_test_t*>(ctx);
    usb_request_t* req;

    while ((req = usb_req_list_remove_head(&test->bulk_out_reqs, test->parent_req_size)) != NULL) {
        usb_request_release(req);
    }
    while ((req = usb_req_list_remove_head(&test->bulk_in_reqs, test->parent_req_size)) != NULL) {
        usb_request_release(req);
    }
    while ((req = usb_req_list_remove_head(&test->intr_reqs, test->parent_req_size)) != NULL) {
        usb_request_release(req);
    }
    free(test);
}

zx_protocol_device_t usb_test_proto = [](){
    zx_protocol_device_t dev;
    dev.version = DEVICE_OPS_VERSION;
    dev.unbind = usb_test_unbind;
    dev.release = usb_test_release;
    return dev;
}();

} // anonymous namespace

static zx_status_t usb_test_bind(void* ctx, zx_device_t* parent) {
    zxlogf(INFO, "%s\n", __func__);

    auto* test = static_cast<usb_test_t*>(calloc(1, sizeof(usb_test_t)));
    if (!test) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_USB_FUNCTION, &test->function);
    if (status != ZX_OK) {
        free(test);
        return status;
    }

    test->parent_req_size = usb_function_get_request_size(&test->function);
    uint64_t req_size = test->parent_req_size + sizeof(usb_req_internal_t);

    list_initialize(&test->bulk_out_reqs);
    list_initialize(&test->bulk_in_reqs);
    list_initialize(&test->intr_reqs);

    device_add_args_t args = {};

    status = usb_function_alloc_interface(&test->function, &descriptors.intf.bInterfaceNumber);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: usb_function_alloc_interface failed\n", __func__);
        goto fail;
    }

    status = usb_function_alloc_ep(&test->function, USB_DIR_OUT, &test->bulk_out_addr);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: usb_function_alloc_ep failed\n", __func__);
        goto fail;
    }
    status = usb_function_alloc_ep(&test->function, USB_DIR_IN, &test->bulk_in_addr);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: usb_function_alloc_ep failed\n", __func__);
        goto fail;
    }
    status = usb_function_alloc_ep(&test->function, USB_DIR_IN, &test->intr_addr);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: usb_function_alloc_ep failed\n", __func__);
        goto fail;
    }

    descriptors.bulk_out_ep.bEndpointAddress = test->bulk_out_addr;
    descriptors.bulk_in_ep.bEndpointAddress = test->bulk_in_addr;
    descriptors.intr_ep.bEndpointAddress = test->intr_addr;

    // Allocate bulk out usb requests.
    usb_request_t* req;
    for (size_t i = 0; i < BULK_TX_COUNT; i++) {
        status = usb_request_alloc(&req, BULK_REQ_SIZE, test->bulk_out_addr, req_size);
        if (status != ZX_OK) {
            goto fail;
        }
        status = usb_req_list_add_head(&test->bulk_out_reqs, req, test->parent_req_size);
        ZX_DEBUG_ASSERT(status == ZX_OK);
    }
    // Allocate bulk in usb requests.
    for (size_t i = 0; i < BULK_RX_COUNT; i++) {
        status = usb_request_alloc(&req, BULK_REQ_SIZE, test->bulk_in_addr, req_size);
        if (status != ZX_OK) {
            goto fail;
        }

        status = usb_req_list_add_head(&test->bulk_in_reqs, req, test->parent_req_size);
        ZX_DEBUG_ASSERT(status == ZX_OK);
    }

    // Allocate interrupt requests.
    for (size_t i = 0; i < INTR_COUNT; i++) {
        status = usb_request_alloc(&req, INTR_REQ_SIZE, test->intr_addr, req_size);
        if (status != ZX_OK) {
            goto fail;
        }

        status = usb_req_list_add_head(&test->intr_reqs, req, test->parent_req_size);
        ZX_DEBUG_ASSERT(status == ZX_OK);
    }

    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "usb-function-test";
    args.ctx = test;
    args.ops = &usb_test_proto;
    args.flags = DEVICE_ADD_NON_BINDABLE;

    status = device_add(parent, &args, &test->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: add_device failed %d\n", __func__, status);
        goto fail;
    }

    usb_function_set_interface(&test->function, test, &device_ops);

    return ZX_OK;

fail:
    usb_test_release(test);
    return status;
}

zx_driver_ops_t driver_ops = [](){
    zx_driver_ops_t ops;
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = usb_test_bind;
    return ops;
}();

} // namespace usb_function_test

// clang-format off
ZIRCON_DRIVER_BEGIN(usb_function_test, usb_function_test::driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_FUNCTION),
    BI_ABORT_IF(NE, BIND_USB_VID, GOOGLE_USB_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, GOOGLE_USB_FUNCTION_TEST_PID),
    BI_MATCH_IF(EQ, BIND_USB_PID, GOOGLE_USB_CDC_AND_FUNCTION_TEST_PID),
ZIRCON_DRIVER_END(usb_function_test)
