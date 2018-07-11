// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ldsvc/c/fidl.h>
#include <string.h>
#include <threads.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

#include <unittest/unittest.h>

static bool g_server_done = false;

static zx_status_t ldsvc_Done(void* ctx) {
    g_server_done = true;
    return ZX_OK;
}

static zx_status_t ldsvc_LoadObject(void* ctx, const char* object_name_data, size_t object_name_size, fidl_txn_t* txn) {
    size_t len = strlen("object name");
    ASSERT_EQ(len, object_name_size, "");
    EXPECT_EQ(0, memcmp(object_name_data, "object name", len), "");
    zx_handle_t event = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_OK, zx_event_create(0, &event), "");
    return fuchsia_ldsvc_LoaderLoadObject_reply(txn, 42, event);
}

static zx_status_t ldsvc_LoadScriptInterpreter(void* ctx, const char* interpreter_name_data, size_t interpreter_name_size, fidl_txn_t* txn) {
    size_t len = strlen("script interpreter");
    ASSERT_EQ(len, interpreter_name_size, "");
    EXPECT_EQ(0, memcmp(interpreter_name_data, "script interpreter", len), "");
    zx_handle_t event = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_OK, zx_event_create(0, &event), "");
    return fuchsia_ldsvc_LoaderLoadScriptInterpreter_reply(txn, 43, event);
}

static zx_status_t ldsvc_Config(void* ctx, const char* config_data, size_t config_size, fidl_txn_t* txn) {
    size_t len = strlen("my config");
    ASSERT_EQ(len, config_size, "");
    EXPECT_EQ(0, memcmp(config_data, "my config", len), "");
    return fuchsia_ldsvc_LoaderConfig_reply(txn, 44);
}

static zx_status_t ldsvc_Clone(void* ctx, zx_handle_t loader, fidl_txn_t* txn) {
    EXPECT_EQ(ZX_OK, zx_handle_close(loader), "");
    return fuchsia_ldsvc_LoaderClone_reply(txn, 45);
}

static zx_status_t ldsvc_DebugPublishDataSink(void* ctx, const char* data_sink_data, size_t data_sink_size, zx_handle_t data, fidl_txn_t* txn) {
    size_t len = strlen("my data sink");
    ASSERT_EQ(len, data_sink_size, "");
    EXPECT_EQ(0, memcmp(data_sink_data, "my data sink", len), "");
    EXPECT_EQ(ZX_OK, zx_handle_close(data), "");
    return fuchsia_ldsvc_LoaderDebugPublishDataSink_reply(txn, 46);
}

static zx_status_t ldsvc_DebugLoadConfig(void* ctx, const char* config_name_data, size_t config_name_size, fidl_txn_t* txn) {
    size_t len = strlen("my debug config");
    ASSERT_EQ(len, config_name_size, "");
    EXPECT_EQ(0, memcmp(config_name_data, "my debug config", len), "");
    zx_handle_t event = ZX_HANDLE_INVALID;
    EXPECT_EQ(ZX_OK, zx_event_create(0, &event), "");
    return fuchsia_ldsvc_LoaderDebugLoadConfig_reply(txn, 47, event);
}

static const fuchsia_ldsvc_Loader_ops_t kOps = {
    .Done = ldsvc_Done,
    .LoadObject = ldsvc_LoadObject,
    .LoadScriptInterpreter = ldsvc_LoadScriptInterpreter,
    .Config = ldsvc_Config,
    .Clone = ldsvc_Clone,
    .DebugPublishDataSink = ldsvc_DebugPublishDataSink,
    .DebugLoadConfig = ldsvc_DebugLoadConfig,
};

typedef struct ldsvc_connection {
    fidl_txn_t txn;
    zx_handle_t channel;
    zx_txid_t txid;
    uint32_t reply_count;
} ldsvc_connection_t;

static zx_status_t ldsvc_server_reply(fidl_txn_t* txn, const fidl_msg_t* msg) {
    ldsvc_connection_t* conn = (ldsvc_connection_t*)txn;
    if (msg->num_bytes < sizeof(fidl_message_header_t))
        return ZX_ERR_INVALID_ARGS;
    fidl_message_header_t* hdr = (fidl_message_header_t*)msg->bytes;
    hdr->txid = conn->txid;
    conn->txid = 0;
    ++conn->reply_count;
    return zx_channel_write(conn->channel, 0, msg->bytes, msg->num_bytes,
                            msg->handles, msg->num_handles);
}

static int ldsvc_server(void* ctx) {
    ldsvc_connection_t conn = {
        .txn.reply = ldsvc_server_reply,
        .channel = *(zx_handle_t*)ctx,
        .reply_count = 0u,
    };
    zx_status_t status = ZX_OK;
    g_server_done = false;

    while (status == ZX_OK && !g_server_done) {
        zx_signals_t observed;
        status = zx_object_wait_one(
            conn.channel, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
            ZX_TIME_INFINITE, &observed);
        if ((observed & ZX_CHANNEL_READABLE) != 0) {
            ASSERT_EQ(ZX_OK, status, "");
            char bytes[ZX_CHANNEL_MAX_MSG_BYTES];
            zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
            fidl_msg_t msg = {
                .bytes = bytes,
                .handles = handles,
                .num_bytes = 0u,
                .num_handles = 0u,
            };
            status = zx_channel_read(conn.channel, 0, bytes, handles,
                                     ZX_CHANNEL_MAX_MSG_BYTES,
                                     ZX_CHANNEL_MAX_MSG_HANDLES,
                                     &msg.num_bytes, &msg.num_handles);
            ASSERT_EQ(ZX_OK, status, "");
            ASSERT_GE(msg.num_bytes, sizeof(fidl_message_header_t), "");
            fidl_message_header_t* hdr = (fidl_message_header_t*)msg.bytes;
            conn.txid = hdr->txid;
            conn.reply_count = 0u;
            status = fuchsia_ldsvc_Loader_dispatch(NULL, &conn.txn, &msg, &kOps);
            ASSERT_EQ(ZX_OK, status, "");
            if (!g_server_done)
                ASSERT_EQ(1u, conn.reply_count, "");
        } else {
            break;
        }
    }

    zx_handle_close(conn.channel);
    return 0;
}

static bool loader_test(void) {
    BEGIN_TEST;

    zx_handle_t client, server;
    zx_status_t status = zx_channel_create(0, &client, &server);
    ASSERT_EQ(ZX_OK, status, "");

    thrd_t thread;
    int rv = thrd_create(&thread, ldsvc_server, &server);
    ASSERT_EQ(thrd_success, rv, "");

    {
        const char* object_name = "object name";
        zx_status_t rv = ZX_OK;
        zx_handle_t object = ZX_HANDLE_INVALID;
        ASSERT_EQ(ZX_OK, fuchsia_ldsvc_LoaderLoadObject(client, object_name, strlen(object_name), &rv, &object), "");
        ASSERT_EQ(42, rv, "");
        ASSERT_EQ(ZX_OK, zx_handle_close(object), "");
    }

    {
        const char* interpreter_name = "script interpreter";
        zx_status_t rv = ZX_OK;
        zx_handle_t object = ZX_HANDLE_INVALID;
        ASSERT_EQ(ZX_OK, fuchsia_ldsvc_LoaderLoadScriptInterpreter(client, interpreter_name, strlen(interpreter_name), &rv, &object), "");
        ASSERT_EQ(43, rv, "");
        ASSERT_EQ(ZX_OK, zx_handle_close(object), "");
    }

    {
        const char* config = "my config";
        zx_status_t rv = ZX_OK;
        ASSERT_EQ(ZX_OK, fuchsia_ldsvc_LoaderConfig(client, config, strlen(config), &rv), "");
        ASSERT_EQ(44, rv, "");
    }

    {
        zx_status_t rv = ZX_OK;
        zx_handle_t h1, h2;
        ASSERT_EQ(ZX_OK, zx_eventpair_create(0, &h1, &h2), "");
        ASSERT_EQ(ZX_OK, fuchsia_ldsvc_LoaderClone(client, h1, &rv), "");
        ASSERT_EQ(45, rv, "");
        ASSERT_EQ(ZX_ERR_PEER_CLOSED, zx_object_signal_peer(h2, 0, 0), "");
        ASSERT_EQ(ZX_OK, zx_handle_close(h2), "");
    }

    {
        const char* sink = "my data sink";
        zx_status_t rv = ZX_OK;
        zx_handle_t h1, h2;
        ASSERT_EQ(ZX_OK, zx_eventpair_create(0, &h1, &h2), "");
        ASSERT_EQ(ZX_OK, fuchsia_ldsvc_LoaderDebugPublishDataSink(client, sink, strlen(sink), h1, &rv), "");
        ASSERT_EQ(46, rv, "");
        ASSERT_EQ(ZX_ERR_PEER_CLOSED, zx_object_signal_peer(h2, 0, 0), "");
        ASSERT_EQ(ZX_OK, zx_handle_close(h2), "");
    }

    {
        const char* config_name = "my debug config";
        zx_status_t rv = ZX_OK;
        zx_handle_t object = ZX_HANDLE_INVALID;
        ASSERT_EQ(ZX_OK, fuchsia_ldsvc_LoaderDebugLoadConfig(client, config_name, strlen(config_name), &rv, &object), "");
        ASSERT_EQ(47, rv, "");
        ASSERT_EQ(ZX_OK, zx_handle_close(object), "");
    }

    ASSERT_EQ(ZX_OK, fuchsia_ldsvc_LoaderDone(client), "");

    int result = 0;
    rv = thrd_join(thread, &result);
    ASSERT_EQ(thrd_success, rv, "");

    END_TEST;
}

BEGIN_TEST_CASE(ldsvc_tests)
RUN_NAMED_TEST("fuchsia.ldsvc.Loader test", loader_test)
END_TEST_CASE(ldsvc_tests);
