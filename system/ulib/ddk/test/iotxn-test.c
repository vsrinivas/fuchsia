// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/iotxn.h>
#include <ddk/protocol/test.h>

#include <unittest/unittest.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static bool test_physmap_simple(void) {
    BEGIN_TEST;
    iotxn_t* txn;
    ASSERT_EQ(iotxn_alloc(&txn, 0, PAGE_SIZE * 3), NO_ERROR, "");
    ASSERT_NONNULL(txn, "");
    ASSERT_EQ(iotxn_physmap(txn), NO_ERROR, "");
    ASSERT_NONNULL(txn->phys, "expected phys to be set");
    ASSERT_EQ(txn->phys_count, 3u, "unexpected phys_count");
    iotxn_release(txn);
    END_TEST;
}

static bool test_physmap_contiguous(void) {
    BEGIN_TEST;
    iotxn_t* txn;
    ASSERT_EQ(iotxn_alloc(&txn, IOTXN_ALLOC_CONTIGUOUS, PAGE_SIZE * 3), NO_ERROR, "");
    ASSERT_NONNULL(txn, "");
    ASSERT_EQ(iotxn_physmap(txn), NO_ERROR, "");
    ASSERT_NONNULL(txn->phys, "expected phys to be set");
    ASSERT_EQ(txn->phys_count, 1u, "unexpected phys_length");
    iotxn_release(txn);
    END_TEST;
}

static bool test_physmap_clone(void) {
    BEGIN_TEST;
    iotxn_t* txn;
    ASSERT_EQ(iotxn_alloc(&txn, 0, PAGE_SIZE * 3), NO_ERROR, "");
    ASSERT_NONNULL(txn, "");
    ASSERT_EQ(iotxn_physmap(txn), NO_ERROR, "");
    ASSERT_NONNULL(txn->phys, "expected phys to be set");
    ASSERT_EQ(txn->phys_count, 3u, "unexpected phys_count");

    iotxn_t* clone = NULL;
    ASSERT_EQ(iotxn_clone(txn, &clone), NO_ERROR, "");
    ASSERT_EQ(txn->phys, clone->phys, "expected clone to point to the same phys");
    ASSERT_EQ(txn->phys_count, clone->phys_count, "unexpected clone phys_count");
    iotxn_release(txn);
    iotxn_release(clone);
    END_TEST;
}

static bool test_physmap_aligned_offset(void) {
    BEGIN_TEST;
    iotxn_t* txn;
    ASSERT_EQ(iotxn_alloc(&txn, 0, PAGE_SIZE * 3), NO_ERROR, "");
    ASSERT_NONNULL(txn, "");
    txn->vmo_offset = PAGE_SIZE;
    txn->vmo_length = PAGE_SIZE * 2;
    ASSERT_EQ(iotxn_physmap(txn), NO_ERROR, "");
    ASSERT_NONNULL(txn->phys, "expected phys to be set");
    ASSERT_EQ(txn->phys_count, 2u, "unexpected phys_count");
    iotxn_release(txn);
    END_TEST;
}

static bool test_physmap_unaligned_offset(void) {
    BEGIN_TEST;
    iotxn_t* txn;
    ASSERT_EQ(iotxn_alloc(&txn, 0, PAGE_SIZE * 3), NO_ERROR, "");
    ASSERT_NONNULL(txn, "");
    txn->vmo_offset = PAGE_SIZE / 2;
    txn->vmo_length = PAGE_SIZE * 2;
    ASSERT_EQ(iotxn_physmap(txn), NO_ERROR, "");
    ASSERT_NONNULL(txn->phys, "expected phys to be set");
    ASSERT_EQ(txn->phys_count, 3u, "unexpected phys_count");
    iotxn_release(txn);
    END_TEST;
}

static bool test_physmap_unaligned_offset2(void) {
    BEGIN_TEST;
    iotxn_t* txn;
    ASSERT_EQ(iotxn_alloc(&txn, 0, PAGE_SIZE * 4), NO_ERROR, "");
    ASSERT_NONNULL(txn, "");
    txn->vmo_offset = PAGE_SIZE - (PAGE_SIZE / 4);
    txn->vmo_length = (PAGE_SIZE * 2) + (PAGE_SIZE / 2);
    ASSERT_EQ(iotxn_physmap(txn), NO_ERROR, "");
    ASSERT_NONNULL(txn->phys, "expected phys to be set");
    ASSERT_EQ(txn->phys_count, 4u, "unexpected phys_count");
    iotxn_release(txn);
    END_TEST;
}

BEGIN_TEST_CASE(iotxn_tests)
RUN_TEST(test_physmap_simple)
RUN_TEST(test_physmap_contiguous)
RUN_TEST(test_physmap_clone)
RUN_TEST(test_physmap_aligned_offset)
RUN_TEST(test_physmap_unaligned_offset)
RUN_TEST(test_physmap_unaligned_offset2)
END_TEST_CASE(iotxn_tests)

static void iotxn_test_output_func(const char* line, int len, void* arg) {
    mx_handle_t h = *(mx_handle_t*)arg;
    // len is not actually the number of bytes to output
    mx_socket_write(h, 0u, line, strlen(line), NULL);
}

static mx_status_t iotxn_test_func(void* cookie, test_report_t* report, const void* arg, size_t arglen) {
    mx_device_t* dev = (mx_device_t*)cookie;

    test_protocol_t* protocol;
    mx_status_t status = device_get_protocol(dev, MX_PROTOCOL_TEST, (void**)&protocol);
    if (status != NO_ERROR) {
        return status;
    }

    mx_handle_t output = protocol->get_output_socket(dev);
    if (output != MX_HANDLE_INVALID) {
        unittest_set_output_function(iotxn_test_output_func, &output);
    }

    struct test_result result;
    bool all_success = unittest_run_all_tests_etc(TEST_ALL, &result);
    report->n_tests = result.n_tests;
    report->n_success = result.n_success;
    report->n_failed = result.n_failed;
    return all_success;
}

static mx_status_t iotxn_test_bind(mx_driver_t* drv, mx_device_t* dev, void** cookie) {
    test_protocol_t* protocol;
    mx_status_t status = device_get_protocol(dev, MX_PROTOCOL_TEST, (void**)&protocol);
    if (status != NO_ERROR) {
        return status;
    }

    protocol->set_test_func(dev, iotxn_test_func, dev);
    return NO_ERROR;
}

mx_driver_t _driver_iotxn_test = {
    .ops = {
        .bind = iotxn_test_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_iotxn_test, "iotxn-test", "magenta", "0.1", 2)
    BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_TEST),
MAGENTA_DRIVER_END(_driver_iotxn_test)
