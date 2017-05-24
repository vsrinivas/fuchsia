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

static bool test_phys_iter(void) {
    BEGIN_TEST;
    iotxn_phys_iter_t iter;
    iotxn_t* txn;
    mx_paddr_t paddr;
    size_t length;
    size_t max_length;

    // create 4 page contiguous iotxn
    ASSERT_EQ(iotxn_alloc(&txn, IOTXN_ALLOC_CONTIGUOUS, PAGE_SIZE * 4), NO_ERROR, "");
    txn->length = PAGE_SIZE * 4;
    ASSERT_EQ(iotxn_physmap(txn), NO_ERROR, "");
    ASSERT_EQ(txn->phys_count, 1u, "");

    // simple contiguous case
    max_length = txn->length + PAGE_SIZE;
    iotxn_phys_iter_init(&iter, txn, max_length);
    length = iotxn_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(paddr, txn->phys[0], "iotxn_iter_next returned wrong paddr");
    ASSERT_EQ(length, txn->length, "iotxn_iter_next returned wrong length");
    ASSERT_EQ(iotxn_phys_iter_next(&iter, &paddr), 0u, "");

    // contiguous case with max_length < txn->length
    max_length = PAGE_SIZE;
    iotxn_phys_iter_init(&iter, txn, max_length);
    for (int i = 0; i < 4; i++) {
        length = iotxn_phys_iter_next(&iter, &paddr);
        ASSERT_EQ(paddr, txn->phys[0] + (i * max_length), "iotxn_iter_next returned wrong paddr");
        ASSERT_EQ(length, max_length, "iotxn_iter_next returned wrong length");
    }
    ASSERT_EQ(iotxn_phys_iter_next(&iter, &paddr), 0u, "");

    // contiguous case with unaligned vmo_offset and txn->length
    txn->vmo_offset = 100;
    max_length = txn->length + PAGE_SIZE;
    txn->length -= 1000;
    iotxn_phys_iter_init(&iter, txn, max_length);
    length = iotxn_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(paddr, txn->phys[0] + txn->vmo_offset, "");
    ASSERT_EQ(length, txn->length, "");
    ASSERT_EQ(iotxn_phys_iter_next(&iter, &paddr), 0u, "");

    iotxn_release(txn);

    // create discontiguous iotxn
    ASSERT_EQ(iotxn_alloc(&txn, 0, PAGE_SIZE * 4), NO_ERROR, "");
    txn->length = PAGE_SIZE * 4;
    ASSERT_EQ(iotxn_physmap(txn), NO_ERROR, "");
    ASSERT_EQ(txn->phys_count, 4u, "");
    // pretend that first two pages are contiguous and second two are not
    txn->phys[1] = txn->phys[0] + PAGE_SIZE;
    txn->phys[2] = txn->phys[0] + (PAGE_SIZE * 10);
    txn->phys[3] = txn->phys[0] + (PAGE_SIZE * 20);

    // simple discontiguous case
    max_length = txn->length + PAGE_SIZE;
    iotxn_phys_iter_init(&iter, txn, max_length);
    length = iotxn_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(paddr, txn->phys[0], "iotxn_iter_next returned wrong paddr");
    ASSERT_EQ(length, (size_t)(PAGE_SIZE * 2), "iotxn_iter_next returned wrong length");
    length = iotxn_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(paddr, txn->phys[2], "iotxn_iter_next returned wrong paddr");
    ASSERT_EQ(length, (size_t)PAGE_SIZE, "iotxn_iter_next returned wrong length");
    length = iotxn_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(paddr, txn->phys[3], "iotxn_iter_next returned wrong paddr");
    ASSERT_EQ(length, (size_t)PAGE_SIZE, "iotxn_iter_next returned wrong length");
    ASSERT_EQ(iotxn_phys_iter_next(&iter, &paddr), 0u, "");

    // discontiguous case with max_length < txn->length
    max_length = PAGE_SIZE;
    iotxn_phys_iter_init(&iter, txn, max_length);
    for (int i = 0; i < 4; i++) {
        length = iotxn_phys_iter_next(&iter, &paddr);
        ASSERT_EQ(paddr, txn->phys[i], "iotxn_iter_next returned wrong paddr");
        ASSERT_EQ(length, max_length, "iotxn_iter_next returned wrong length");
    }
    ASSERT_EQ(iotxn_phys_iter_next(&iter, &paddr), 0u, "");

    // discontiguous case with unaligned vmo_offset and txn->length
    txn->vmo_offset = 100;
    max_length = txn->length + PAGE_SIZE;
    txn->length -= 1000;
    iotxn_phys_iter_init(&iter, txn, max_length);
    size_t total_length = 0;
    length = iotxn_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(paddr, txn->phys[0] + txn->vmo_offset, "");
    ASSERT_EQ(length, (size_t)(PAGE_SIZE * 2) - txn->vmo_offset, "iotxn_iter_next returned wrong length");
    total_length += length;
    length = iotxn_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(paddr, txn->phys[2], "");
    ASSERT_EQ(length, (size_t)PAGE_SIZE, "");
    total_length += length;
    length = iotxn_phys_iter_next(&iter, &paddr);
    ASSERT_EQ(paddr, txn->phys[3], "");
    total_length += length;
    ASSERT_EQ(total_length, txn->length, "");
    ASSERT_EQ(iotxn_phys_iter_next(&iter, &paddr), 0u, "");

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
RUN_TEST(test_phys_iter)
END_TEST_CASE(iotxn_tests)

static void iotxn_test_output_func(const char* line, int len, void* arg) {
    mx_handle_t h = *(mx_handle_t*)arg;
    // len is not actually the number of bytes to output
    mx_socket_write(h, 0u, line, strlen(line), NULL);
}

static mx_status_t iotxn_test_func(void* cookie, test_report_t* report, const void* arg, size_t arglen) {
    mx_device_t* dev = (mx_device_t*)cookie;

    test_protocol_t* protocol;
    mx_status_t status = device_op_get_protocol(dev, MX_PROTOCOL_TEST, (void**)&protocol);
    if (status != NO_ERROR) {
        return status;
    }

    mx_handle_t output = protocol->get_output_socket(dev);
    if (output != MX_HANDLE_INVALID) {
        unittest_set_output_function(iotxn_test_output_func, &output);
    }

    bool success = unittest_run_one_test(TEST_CASE_ELEMENT(iotxn_tests), TEST_ALL);
    report->n_tests = 1;
    report->n_success = success ? 1 : 0;
    report->n_failed = success ? 0 : 1;
    return success ? NO_ERROR : ERR_INTERNAL;
}

static mx_status_t iotxn_test_bind(void* ctx, mx_device_t* dev, void** cookie) {
    test_protocol_t* protocol;
    mx_status_t status = device_op_get_protocol(dev, MX_PROTOCOL_TEST, (void**)&protocol);
    if (status != NO_ERROR) {
        return status;
    }

    protocol->set_test_func(dev, iotxn_test_func, dev);
    return NO_ERROR;
}

static mx_driver_ops_t iotxn_test_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = iotxn_test_bind,
};

MAGENTA_DRIVER_BEGIN(iotxn_test, iotxn_test_driver_ops, "magenta", "0.1", 2)
    BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_TEST),
MAGENTA_DRIVER_END(iotxn_test)
