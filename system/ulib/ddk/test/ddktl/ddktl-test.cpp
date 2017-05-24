// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/test.h>
#include <mx/socket.h>

#include <unittest/unittest.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

extern test_case_element* test_case_ddktl_device;
extern test_case_element* test_case_ddktl_ethernet_device;
extern test_case_element* test_case_ddktl_wlan_device;

namespace {

void ddktl_test_output_func(const char* line, int len, void* arg) {
    mx_handle_t h = *static_cast<mx_handle_t*>(arg);
    mx::socket s(h);
    // len is not actually the number of bytes to output
    s.write(0u, line, strlen(line), nullptr);
    // we don't on the socket so release it before it goes out of scope
    h = s.release();
}

static void inline update_test_report(bool success, test_report_t* report) {
    report->n_tests++;
    if (success) {
        report->n_success++;
    } else {
        report->n_failed++;
    }
}

mx_status_t ddktl_test_func(void* cookie, test_report_t* report, const void* arg, size_t arglen) {
    auto dev = static_cast<mx_device_t*>(cookie);

    test_protocol_t* protocol;
    auto status =
        device_op_get_protocol(dev, MX_PROTOCOL_TEST, reinterpret_cast<void**>(&protocol));
    if (status != NO_ERROR) {
        return status;
    }

    mx_handle_t output = protocol->get_output_socket(dev);
    if (output != MX_HANDLE_INVALID) {
        unittest_set_output_function(ddktl_test_output_func, &output);
    }

    memset(report, 0, sizeof(*report));
    update_test_report(unittest_run_one_test(test_case_ddktl_device, TEST_ALL), report);
    update_test_report(unittest_run_one_test(test_case_ddktl_ethernet_device, TEST_ALL), report);
    update_test_report(unittest_run_one_test(test_case_ddktl_wlan_device, TEST_ALL), report);
    return report->n_failed == 0 ? NO_ERROR : ERR_INTERNAL;
}

}  // namespace

extern "C" mx_status_t ddktl_test_bind(void* ctx, mx_device_t* dev, void** cookie) {
    test_protocol_t* protocol;
    auto status =
        device_op_get_protocol(dev, MX_PROTOCOL_TEST, reinterpret_cast<void**>(&protocol));
    if (status != NO_ERROR) {
        return status;
    }

    protocol->set_test_func(dev, ddktl_test_func, dev);

    return NO_ERROR;
}
