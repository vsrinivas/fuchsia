// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/crash/c/fidl.h>
#include <string.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

#include <unittest/unittest.h>

static int kContext = 42;
static size_t g_analyze_call_count = 0u;

static zx_status_t analyze(void* ctx, zx_handle_t process, zx_handle_t thread, fidl_txn_t* txn) {
    ++g_analyze_call_count;
    EXPECT_EQ(&kContext, ctx, "");
    EXPECT_NE(ZX_HANDLE_INVALID, process, "");
    EXPECT_NE(ZX_HANDLE_INVALID, thread, "");
    EXPECT_NE(NULL, txn, "");
    zx_handle_close(process);
    zx_handle_close(thread);
    return ZX_OK;
}

static bool dispatch_test(void) {
    BEGIN_TEST;

    fuchsia_crash_Analyzer_ops_t ops = {
        .Analyze = analyze,
    };

    fuchsia_crash_AnalyzerAnalyzeRequest request;
    memset(&request, 0, sizeof(request));
    request.hdr.txid = 42;
    request.hdr.ordinal = fuchsia_crash_AnalyzerAnalyzeOrdinal;
    request.process = FIDL_HANDLE_PRESENT;
    request.thread = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[2];
    fidl_msg_t msg = {
        .bytes = &request,
        .handles = handles,
        .num_bytes = sizeof(request),
        .num_handles = 2,
    };

    fidl_txn_t txn;
    memset(&txn, 0, sizeof(txn));

    // Success

    zx_status_t status = zx_eventpair_create(0, &handles[0], &handles[1]);
    ASSERT_EQ(ZX_OK, status, "");
    EXPECT_EQ(0u, g_analyze_call_count, "");
    status = fuchsia_crash_Analyzer_dispatch(&kContext, &txn, &msg, &ops);
    ASSERT_EQ(ZX_OK, status, "");
    EXPECT_EQ(1u, g_analyze_call_count, "");
    g_analyze_call_count = 0u;

    // Bad ordinal (dispatch)

    request.hdr.ordinal = 8949;
    zx_handle_t canary0 = ZX_HANDLE_INVALID;
    status = zx_eventpair_create(0, &handles[0], &canary0);

    zx_handle_t canary1 = ZX_HANDLE_INVALID;
    status = zx_eventpair_create(0, &handles[1], &canary1);

    ASSERT_EQ(ZX_OK, status, "");
    EXPECT_EQ(0u, g_analyze_call_count, "");
    status = fuchsia_crash_Analyzer_dispatch(&kContext, &txn, &msg, &ops);
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, status, "");
    EXPECT_EQ(0u, g_analyze_call_count, "");
    g_analyze_call_count = 0u;
    status = zx_object_signal_peer(canary0, 0, ZX_USER_SIGNAL_0);
    ASSERT_EQ(ZX_ERR_PEER_CLOSED, status, "");
    status = zx_object_signal_peer(canary1, 0, ZX_USER_SIGNAL_0);
    ASSERT_EQ(ZX_ERR_PEER_CLOSED, status, "");
    zx_handle_close(canary0);
    zx_handle_close(canary1);

    // Bad ordinal (try_dispatch)

    request.hdr.ordinal = 8949;
    canary0 = ZX_HANDLE_INVALID;
    status = zx_eventpair_create(0, &handles[0], &canary0);

    canary1 = ZX_HANDLE_INVALID;
    status = zx_eventpair_create(0, &handles[1], &canary1);

    ASSERT_EQ(ZX_OK, status, "");
    EXPECT_EQ(0u, g_analyze_call_count, "");
    status = fuchsia_crash_Analyzer_try_dispatch(&kContext, &txn, &msg, &ops);
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, status, "");
    EXPECT_EQ(0u, g_analyze_call_count, "");
    g_analyze_call_count = 0u;
    status = zx_object_signal_peer(canary0, 0, ZX_USER_SIGNAL_0);
    ASSERT_EQ(ZX_OK, status, "");
    status = zx_object_signal_peer(canary1, 0, ZX_USER_SIGNAL_0);
    ASSERT_EQ(ZX_OK, status, "");
    zx_handle_close_many(handles, 2);
    zx_handle_close(canary0);
    zx_handle_close(canary1);

    END_TEST;
}

typedef struct my_connection {
    fidl_txn_t txn;
    size_t count;
} my_connection_t;

static zx_status_t reply_handler(fidl_txn_t* txn, const fidl_msg_t* msg) {
    my_connection_t* my_txn = (my_connection_t*)txn;
    EXPECT_EQ(sizeof(fidl_message_header_t), msg->num_bytes, "");
    EXPECT_EQ(0u, msg->num_handles, "");
    ++my_txn->count;
    return ZX_OK;
}

static bool reply_test(void) {
    BEGIN_TEST;

    my_connection_t conn;
    conn.txn.reply = reply_handler;
    conn.count = 0u;

    zx_status_t status = fuchsia_crash_AnalyzerAnalyze_reply(&conn.txn);
    ASSERT_EQ(ZX_OK, status, "");
    EXPECT_EQ(1u, conn.count, "");

    END_TEST;
}

static zx_status_t return_async(void* ctx, zx_handle_t process, zx_handle_t thread, fidl_txn_t* txn) {
    zx_handle_close(process);
    zx_handle_close(thread);
    return ZX_ERR_ASYNC;
}

static bool error_test(void) {
    BEGIN_TEST;

    fuchsia_crash_Analyzer_ops_t ops = {
        .Analyze = return_async,
    };

    fuchsia_crash_AnalyzerAnalyzeRequest request;
    memset(&request, 0, sizeof(request));
    request.hdr.txid = 42;
    request.hdr.ordinal = fuchsia_crash_AnalyzerAnalyzeOrdinal;
    request.process = FIDL_HANDLE_PRESENT;
    request.thread = FIDL_HANDLE_PRESENT;

    zx_handle_t handles[2];
    fidl_msg_t msg = {
        .bytes = &request,
        .handles = handles,
        .num_bytes = sizeof(request),
        .num_handles = 2,
    };

    fidl_txn_t txn;
    memset(&txn, 0, sizeof(txn));

    zx_status_t status = zx_eventpair_create(0, &handles[0], &handles[1]);
    ASSERT_EQ(ZX_OK, status, "");
    status = fuchsia_crash_Analyzer_try_dispatch(NULL, &txn, &msg, &ops);
    ASSERT_EQ(ZX_ERR_ASYNC, status, "");

    END_TEST;
}

BEGIN_TEST_CASE(server_tests)
RUN_NAMED_TEST("fuchsia.crash.Analyzer dispatch test", dispatch_test)
RUN_NAMED_TEST("fuchsia.crash.Analyzer reply test", reply_test)
RUN_NAMED_TEST("fuchsia.crash.Analyzer error test", error_test)
END_TEST_CASE(server_tests);
