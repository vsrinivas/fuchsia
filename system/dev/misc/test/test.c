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
#include <zircon/listnode.h>

typedef struct test_device {
    zx_device_t* zxdev;
    zx_handle_t output;
    zx_handle_t control;
    test_func_t test_func;
    void* cookie;
} test_device_t;

typedef struct test_root {
    zx_device_t* zxdev;
} test_root_t;

static void test_device_set_output_socket(void* ctx, zx_handle_t handle) {
    test_device_t* device = ctx;
    if (device->output != ZX_HANDLE_INVALID) {
        zx_handle_close(device->output);
    }
    device->output = handle;
}

static zx_handle_t test_device_get_output_socket(void* ctx) {
    test_device_t* device = ctx;
    return device->output;
}

static void test_device_set_control_channel(void* ctx, zx_handle_t handle) {
    test_device_t* device = ctx;
    if (device->control != ZX_HANDLE_INVALID) {
        zx_handle_close(device->control);
    }
    device->control = handle;
}

static zx_handle_t test_device_get_control_channel(void* ctx) {
    test_device_t* device = ctx;
    return device->control;
}

static void test_device_set_test_func(void* ctx, test_func_t func, void* cookie) {
    test_device_t* device = ctx;
    device->test_func = func;
    device->cookie = cookie;
}

static zx_status_t test_device_run_tests(void *ctx, test_report_t* report, const void* arg, size_t arglen) {
    test_device_t* device = ctx;
    if (device->test_func != NULL) {
        return device->test_func(device->cookie, report, arg, arglen);
    } else {
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void test_device_destroy(void *ctx) {
    test_device_t* device = ctx;
    device_remove(device->zxdev);
}

static test_protocol_ops_t test_test_proto = {
    .set_output_socket = test_device_set_output_socket,
    .get_output_socket = test_device_get_output_socket,
    .set_control_channel = test_device_set_control_channel,
    .get_control_channel = test_device_get_control_channel,
    .set_test_func = test_device_set_test_func,
    .run_tests = test_device_run_tests,
    .destroy = test_device_destroy,
};

static zx_status_t test_device_ioctl(void* ctx, uint32_t op, const void* in, size_t inlen, void* out,
                                    size_t outlen, size_t* out_actual) {
    test_device_t* dev = ctx;
    switch (op) {
    case IOCTL_TEST_SET_OUTPUT_SOCKET:
        if (inlen != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        test_device_set_output_socket(dev, *(zx_handle_t*)in);
        return ZX_OK;

    case IOCTL_TEST_SET_CONTROL_CHANNEL:
        if (inlen != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        test_device_set_control_channel(dev, *(zx_handle_t*)in);
        return ZX_OK;

    case IOCTL_TEST_RUN_TESTS:
        if (outlen != sizeof(test_report_t)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        zx_status_t status = test_device_run_tests(dev, (test_report_t*)out, in, inlen);
        *out_actual = sizeof(test_report_t);
        return status;

    case IOCTL_TEST_DESTROY_DEVICE:
        device_remove(dev->zxdev);
        return 0;

    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void test_device_release(void* ctx) {
    test_device_t* device = ctx;
    if (device->output != ZX_HANDLE_INVALID) {
        zx_handle_close(device->output);
    }
    if (device->control != ZX_HANDLE_INVALID) {
        zx_handle_close(device->control);
    }
    free(device);
}

static zx_protocol_device_t test_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = test_device_ioctl,
    .release = test_device_release,
};


static zx_status_t test_ioctl(void* ctx, uint32_t op, const void* in, size_t inlen,
                              void* out, size_t outlen, size_t* out_actual) {
    test_root_t* root = ctx;

    if (op != IOCTL_TEST_CREATE_DEVICE) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    char devname[ZX_DEVICE_NAME_MAX + 1];
    if (inlen > 0) {
        strncpy(devname, in, sizeof(devname));
    } else {
        strncpy(devname, "testdev", sizeof(devname));
    }

    if (outlen < strlen(devname) + sizeof(TEST_CONTROL_DEVICE) + 1) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    test_device_t* device = calloc(1, sizeof(test_device_t));
    if (device == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = devname,
        .ctx = device,
        .ops = &test_device_proto,
        .proto_id = ZX_PROTOCOL_TEST,
        .proto_ops = &test_test_proto,
    };

    zx_status_t status;
    if ((status = device_add(root->zxdev, &args, &device->zxdev)) != ZX_OK) {
        free(device);
        return status;
    }

    int length = snprintf(out, outlen,"%s/%s", TEST_CONTROL_DEVICE, devname) + 1;
    *out_actual = length;
    return ZX_OK;
}

static zx_protocol_device_t test_root_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = test_ioctl,
};

static zx_status_t test_bind(void* ctx, zx_device_t* dev) {
    test_root_t* root = calloc(1, sizeof(test_root_t));
    if (!root) {
        return ZX_ERR_NO_MEMORY;
    }
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "test",
        .ctx = root,
        .ops = &test_root_proto,
    };

    return device_add(dev, &args, &root->zxdev);
}

static zx_driver_ops_t test_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = test_bind,
};

ZIRCON_DRIVER_BEGIN(test, test_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST_PARENT),
ZIRCON_DRIVER_END(test)
