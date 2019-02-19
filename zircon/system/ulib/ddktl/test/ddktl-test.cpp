// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/test.h>
#include <lib/zx/socket.h>

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unittest/unittest.h>

extern test_case_element* test_case_ddktl_device;
extern test_case_element* test_case_ddktl_ethernet_device;

namespace {

void ddktl_test_output_func(const char* line, int len, void* arg) {
    zx_handle_t h = *static_cast<zx_handle_t*>(arg);
    zx::socket s(h);
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

zx_status_t ddktl_test_func(void* cookie, test_report_t* report) {
    auto dev = static_cast<zx_device_t*>(cookie);

    test_protocol_t proto;
    auto status =
        device_get_protocol(dev, ZX_PROTOCOL_TEST, reinterpret_cast<void*>(&proto));
    if (status != ZX_OK) {
        return status;
    }

    zx_handle_t output;
    proto.ops->get_output_socket(proto.ctx, &output);
    if (output != ZX_HANDLE_INVALID) {
        unittest_set_output_function(ddktl_test_output_func, &output);
    }

    memset(report, 0, sizeof(*report));
    update_test_report(unittest_run_one_test(test_case_ddktl_device, TEST_ALL), report);
    update_test_report(unittest_run_one_test(test_case_ddktl_ethernet_device, TEST_ALL), report);
    unittest_restore_output_function();
    zx_handle_close(output);
    return report->n_failed == 0 ? ZX_OK : ZX_ERR_INTERNAL;
}

} // namespace

extern "C" zx_status_t ddktl_test_bind(void* ctx, zx_device_t* parent) {
    test_protocol_t proto;
    auto status =
        device_get_protocol(parent, ZX_PROTOCOL_TEST, reinterpret_cast<void*>(&proto));
    if (status != ZX_OK) {
        return status;
    }

    const test_func_t test = {ddktl_test_func, parent};
    proto.ops->set_test_func(proto.ctx, &test);

    return ZX_OK;
}
