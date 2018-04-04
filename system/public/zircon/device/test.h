// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>


#define TEST_CONTROL_DEVICE "/dev/test/test"

// Create a test device, only supported by TEST_CONTROL_DEVICE
//   in: null-terminated string device name
//   out: null-terminated string path to created device
#define IOCTL_TEST_CREATE_DEVICE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_TEST, 0)

// Destroy a test device
//   in: none
//   out: none
#define IOCTL_TEST_DESTROY_DEVICE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_TEST, 1)

// Run tests on the device
//   in: none
//   out: test_ioctl_test_report_t test results
#define IOCTL_TEST_RUN_TESTS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_TEST, 2)

// Set an output socket
//   in: zx_handle_t*
//   out: none
#define IOCTL_TEST_SET_OUTPUT_SOCKET \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_TEST, 3)

// Set a control channel
//   in: zx_handle_t*
//   out: none
#define IOCTL_TEST_SET_CONTROL_CHANNEL \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_TEST, 4)

typedef struct test_ioctl_test_report {
    unsigned int n_tests;
    unsigned int n_success;
    unsigned int n_failed;
} test_ioctl_test_report_t;

// ssize_t ioctl_test_create_device(int fd, char* in, size_t in_len, char* out, size_t out_len);
IOCTL_WRAPPER_VARIN_VAROUT(ioctl_test_create_device, IOCTL_TEST_CREATE_DEVICE, char, char);

// ssize_t ioctl_test_destroy_device(int fd)
IOCTL_WRAPPER(ioctl_test_destroy_device, IOCTL_TEST_DESTROY_DEVICE);

// ssize_t ioctl_test_run_tests(int fd, void* in, size_t in_len, test_ioctl_test_report_t* out);
IOCTL_WRAPPER_VARIN_OUT(ioctl_test_run_tests, IOCTL_TEST_RUN_TESTS, void*, test_ioctl_test_report_t);

// ssize_t ioctl_test_set_output_socket(int fd, zx_handle_t in)
IOCTL_WRAPPER_IN(ioctl_test_set_output_socket, IOCTL_TEST_SET_OUTPUT_SOCKET, zx_handle_t)

// ssize_t ioctl_test_set_control_channel(int fd, zx_handle_t in)
IOCTL_WRAPPER_IN(ioctl_test_set_control_channel, IOCTL_TEST_SET_CONTROL_CHANNEL, zx_handle_t)
