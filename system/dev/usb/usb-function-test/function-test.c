// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb-function.h>
#include <magenta/device/usb-device.h>

static struct {
    usb_interface_descriptor_t intf;
    usb_endpoint_descriptor_t out_ep;
    usb_endpoint_descriptor_t in_ep;
} descriptors = {
    .intf = {
        .bLength = sizeof(usb_interface_descriptor_t),
        .bDescriptorType = USB_DT_INTERFACE,
//      .bInterfaceNumber set later
        .bAlternateSetting = 0,
        .bNumEndpoints = 2,
        .bInterfaceClass = USB_CLASS_VENDOR,
        .bInterfaceSubClass = 1,
        .bInterfaceProtocol = 0,
        .iInterface = 0,
    },
    .out_ep = {
        .bLength = sizeof(usb_endpoint_descriptor_t),
        .bDescriptorType = USB_DT_ENDPOINT,
//      .bEndpointAddress set later
        .bmAttributes = USB_ENDPOINT_BULK,
        .wMaxPacketSize = htole16(512),
        .bInterval = 0,
    },
    .in_ep = {
        .bLength = sizeof(usb_endpoint_descriptor_t),
        .bDescriptorType = USB_DT_ENDPOINT,
//      .bEndpointAddress set later
        .bmAttributes = USB_ENDPOINT_BULK,
        .wMaxPacketSize = htole16(512),
        .bInterval = 0,
    },
};

typedef struct {
    mx_device_t* mxdev;
    usb_function_protocol_t function;
} usb_function_test_t;

static const usb_descriptor_header_t* function_test_get_descriptors(void* ctx, size_t* out_length) {
    *out_length = sizeof(descriptors);
    return (const usb_descriptor_header_t *)&descriptors;
}

static mx_status_t function_test_control(void* ctx, const usb_setup_t* setup, void* buffer,
                                         size_t length, size_t* out_actual) {
    return MX_ERR_NOT_SUPPORTED;
}

usb_function_interface_ops_t device_ops = {
    .get_descriptors = function_test_get_descriptors,
    .control = function_test_control,
};

static void usb_function_test_unbind(void* ctx) {
printf("usb_function_test_unbind\n");
    usb_function_test_t* test = ctx;
    device_remove(test->mxdev);
}

static void usb_function_test_release(void* ctx) {
printf("usb_function_test_release\n");
    usb_function_test_t* test = ctx;
    free(test);
}

static mx_protocol_device_t usb_function_test_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = usb_function_test_unbind,
    .release = usb_function_test_release,
};

mx_status_t usb_function_test_bind(void* ctx, mx_device_t* parent, void** cookie) {
    printf("usb_function_test_bind\n");

    usb_function_test_t* test = calloc(1, sizeof(usb_function_test_t));
    if (!test) {
        return MX_ERR_NO_MEMORY;
    }

    if (device_get_protocol(parent, MX_PROTOCOL_USB_FUNCTION, &test->function)) {
        free(test);
        return MX_ERR_NOT_SUPPORTED;
    }

    descriptors.intf.bInterfaceNumber = usb_function_get_interface_number(&test->function);

    mx_status_t status = usb_function_alloc_endpoint(&test->function, USB_DIR_OUT,
                                                     &descriptors.out_ep.bEndpointAddress);
    if (status != MX_OK) {
        printf("usb_function_test_bind: usb_function_alloc_endpoint failed\n");
        free(test);
        return status;
    }
    status = usb_function_alloc_endpoint(&test->function, USB_DIR_IN,
                                                     &descriptors.in_ep.bEndpointAddress);
    if (status != MX_OK) {
        printf("usb_function_test_bind: usb_function_alloc_endpoint failed\n");
        free(test);
        return status;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "usb-device-test",
        .ctx = test,
        .ops = &usb_function_test_proto,
    };

    status = device_add(parent, &args, &test->mxdev);
    if (status != MX_OK) {
        printf("usb_device_bind add_device failed %d\n", status);
        free(test);
        return status;
    }

    usb_function_interface_t intf = {
        .ops = &device_ops,
        .ctx = test,
    };
    usb_function_register(&test->function, &intf);

    return MX_OK;
}

static mx_driver_ops_t usb_function_test_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_function_test_bind,
};

// clang-format off
MAGENTA_DRIVER_BEGIN(usb_function_test, usb_function_test_ops, "magenta", "0.1", 6)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB_FUNCTION),
    BI_ABORT_IF(NE, BIND_USB_VID, 0x18D1),
    BI_ABORT_IF(NE, BIND_USB_PID, 0x1234),
    BI_MATCH_IF(EQ, BIND_USB_CLASS, USB_CLASS_VENDOR),
    BI_MATCH_IF(EQ, BIND_USB_SUBCLASS, 1),
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, 0),
MAGENTA_DRIVER_END(usb_function_test)
