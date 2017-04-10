// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/types.h>

// Create a test device, only supported by /dev/misc/test
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
