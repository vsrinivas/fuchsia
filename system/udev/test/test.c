// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/test.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <magenta/listnode.h>

typedef struct test_device {
    mx_device_t* mxdev;
    mx_handle_t output;
    mx_handle_t control;
    test_func_t test_func;
    void* cookie;
} test_device_t;

typedef struct test_root {
    mx_device_t* mxdev;
} test_root_t;

static void test_device_set_output_socket(mx_device_t* dev, mx_handle_t handle) {
    test_device_t* device = dev->ctx;
    if (device->output != MX_HANDLE_INVALID) {
        mx_handle_close(device->output);
    }
    device->output = handle;
}

static mx_handle_t test_device_get_output_socket(mx_device_t* dev) {
    test_device_t* device = dev->ctx;
    return device->output;
}

static void test_device_set_control_channel(mx_device_t* dev, mx_handle_t handle) {
    test_device_t* device = dev->ctx;
    if (device->control != MX_HANDLE_INVALID) {
        mx_handle_close(device->control);
    }
    device->control = handle;
}

static mx_handle_t test_device_get_control_channel(mx_device_t* dev) {
    test_device_t* device = dev->ctx;
    return device->control;
}

static void test_device_set_test_func(mx_device_t* dev, test_func_t func, void* cookie) {
    test_device_t* device = dev->ctx;
    device->test_func = func;
    device->cookie = cookie;
}

static mx_status_t test_device_run_tests(mx_device_t* dev, test_report_t* report, const void* arg, size_t arglen) {
    test_device_t* device = dev->ctx;
    if (device->test_func != NULL) {
        return device->test_func(device->cookie, report, arg, arglen);
    } else {
        return ERR_NOT_SUPPORTED;
    }
}

static void test_device_destroy(mx_device_t* dev) {
    device_remove(dev);
}

static test_protocol_t test_test_proto = {
    .set_output_socket = test_device_set_output_socket,
    .get_output_socket = test_device_get_output_socket,
    .set_control_channel = test_device_set_control_channel,
    .get_control_channel = test_device_get_control_channel,
    .set_test_func = test_device_set_test_func,
    .run_tests = test_device_run_tests,
    .destroy = test_device_destroy,
};

static mx_status_t test_device_ioctl(void* ctx, uint32_t op, const void* in, size_t inlen, void* out,
                                    size_t outlen, size_t* out_actual) {
    test_device_t* dev = ctx;
    switch (op) {
    case IOCTL_TEST_SET_OUTPUT_SOCKET:
        if (inlen != sizeof(mx_handle_t)) {
            return ERR_INVALID_ARGS;
        }
        test_device_set_output_socket(dev->mxdev, *(mx_handle_t*)in);
        return NO_ERROR;

    case IOCTL_TEST_SET_CONTROL_CHANNEL:
        if (inlen != sizeof(mx_handle_t)) {
            return ERR_INVALID_ARGS;
        }
        test_device_set_control_channel(dev->mxdev, *(mx_handle_t*)in);
        return NO_ERROR;

    case IOCTL_TEST_RUN_TESTS:
        if (outlen != sizeof(test_report_t)) {
            return ERR_BUFFER_TOO_SMALL;
        }
        mx_status_t status = test_device_run_tests(dev->mxdev, (test_report_t*)out, in, inlen);
        *out_actual = sizeof(test_report_t);
        return status;

    case IOCTL_TEST_DESTROY_DEVICE:
        device_remove(dev->mxdev);
        return 0;

    default:
        return ERR_NOT_SUPPORTED;
    }
}

static void test_device_release(void* ctx) {
    test_device_t* device = ctx;
    if (device->output != MX_HANDLE_INVALID) {
        mx_handle_close(device->output);
    }
    if (device->control != MX_HANDLE_INVALID) {
        mx_handle_close(device->control);
    }
    free(device);
}

static mx_protocol_device_t test_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = test_device_ioctl,
    .release = test_device_release,
};


#define DEV_TEST "/dev/misc/test"

static mx_status_t test_ioctl(void* ctx, uint32_t op, const void* in, size_t inlen,
                              void* out, size_t outlen, size_t* out_actual) {
    test_root_t* root = ctx;

    if (op != IOCTL_TEST_CREATE_DEVICE) {
        return ERR_NOT_SUPPORTED;
    }

    char devname[MX_DEVICE_NAME_MAX + 1];
    if (inlen > 0) {
        strncpy(devname, in, sizeof(devname));
    } else {
        strncpy(devname, "testdev", sizeof(devname));
    }

    if (outlen < strlen(devname) + sizeof(DEV_TEST) + 1) {
        return ERR_BUFFER_TOO_SMALL;
    }

    test_device_t* device = calloc(1, sizeof(test_device_t));
    if (device == NULL) {
        return ERR_NO_MEMORY;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = devname,
        .ctx = device,
        .ops = &test_device_proto,
        .proto_id = MX_PROTOCOL_TEST,
        .proto_ops = &test_test_proto,
    };

    mx_status_t status;
    if ((status = device_add(root->mxdev, &args, &device->mxdev)) != NO_ERROR) {
        free(device);
        return status;
    }

    int length = snprintf(out, outlen,"%s/%s", DEV_TEST, devname) + 1;
    *out_actual = length;
    return NO_ERROR;
}

static mx_protocol_device_t test_root_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = test_ioctl,
};

static mx_status_t test_bind(void* ctx, mx_device_t* dev, void** cookie) {
    test_root_t* root = calloc(1, sizeof(test_root_t));
    if (!root) {
        return ERR_NO_MEMORY;
    }
    device_add_args_t args = {
       .version = DEVICE_ADD_ARGS_VERSION,
        .name = "test",
        .ctx = root,
        .ops = &test_root_proto,
    };

    return device_add(dev, &args, &root->mxdev);
}

static mx_driver_ops_t test_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = test_bind,
};

MAGENTA_DRIVER_BEGIN(test, test_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_MISC_PARENT),
MAGENTA_DRIVER_END(test)
