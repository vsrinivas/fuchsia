// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <magenta/device/test.h>

typedef test_ioctl_test_report_t test_report_t;

typedef mx_status_t (*test_func_t)(void* cookie, test_report_t* report, const void* arg, size_t arglen);

typedef struct test_protocol {
    // sets test output channel
    void (*set_output_channel)(mx_device_t* dev, mx_handle_t handle);

    // gets test output channel
    mx_handle_t (*get_output_channel)(mx_device_t* dev);

    // sets control channel
    void (*set_control_channel)(mx_device_t* dev, mx_handle_t handle);

    // gets control channel
    mx_handle_t (*get_control_channel)(mx_device_t* dev);

    // sets test function
    void (*set_test_func)(mx_device_t* dev, test_func_t func, void* cookie);

    // run tests, calls the function set in set_test_func
    mx_status_t (*run_tests)(mx_device_t* dev, test_report_t* report, const void* arg, size_t arglen);

    // calls device_remove()
    void (*destroy)(mx_device_t* dev);
} test_protocol_t;
