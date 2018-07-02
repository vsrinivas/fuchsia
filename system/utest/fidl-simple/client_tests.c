// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/crash/c/fidl.h>
#include <string.h>
#include <threads.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

#include <unittest/unittest.h>

static int crash_server(void* ctx) {
    zx_handle_t server = *(zx_handle_t*)ctx;
    zx_status_t status = ZX_OK;

    while (status == ZX_OK) {
        zx_signals_t observed;
        status = zx_object_wait_one(
            server, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
            ZX_TIME_INFINITE, &observed);
        if ((observed & ZX_CHANNEL_READABLE) != 0) {
            ASSERT_EQ(ZX_OK, status, "");
            char msg[1024];
            zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
            uint32_t actual_bytes = 0u;
            uint32_t actual_handles = 0u;
            status = zx_channel_read(server, 0, msg, handles,
                                     sizeof(msg), ZX_CHANNEL_MAX_MSG_HANDLES,
                                     &actual_bytes, &actual_handles);
            ASSERT_EQ(ZX_OK, status, "");
            ASSERT_GE(actual_bytes, sizeof(fidl_message_header_t), "");
            ASSERT_EQ(actual_handles, 2u, "");
            zx_handle_close_many(handles, actual_handles);
            fidl_message_header_t* req = (fidl_message_header_t*)msg;
            fidl_message_header_t hdr;
            memset(&hdr, 0, sizeof(hdr));
            hdr.txid = req->txid;
            hdr.ordinal = req->ordinal;
            status = zx_channel_write(server, 0, &hdr, sizeof(hdr), NULL, 0);
            ASSERT_EQ(ZX_OK, status, "");
        } else {
            break;
        }
    }

    zx_handle_close(server);
    return 0;
}

static bool crash_analyzer_test(void) {
    BEGIN_TEST;

    zx_handle_t client, server;
    zx_status_t status = zx_channel_create(0, &client, &server);
    ASSERT_EQ(ZX_OK, status, "");

    thrd_t thread;
    int rv = thrd_create(&thread, crash_server, &server);
    ASSERT_EQ(thrd_success, rv, "");

    zx_handle_t h0, h1;
    status = zx_eventpair_create(0, &h0, &h1);
    ASSERT_EQ(ZX_OK, status, "");

    status = fuchsia_crash_AnalyzerAnalyze(client, h0, h1);
    ASSERT_EQ(ZX_OK, status, "");

    status = zx_handle_close(client);
    ASSERT_EQ(ZX_OK, status, "");

    int result = 0;
    rv = thrd_join(thread, &result);
    ASSERT_EQ(thrd_success, rv, "");

    END_TEST;
}

BEGIN_TEST_CASE(client_tests)
RUN_NAMED_TEST("fuchsia.crash.Analyzer test", crash_analyzer_test)
END_TEST_CASE(client_tests);
