// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/fakesocket/c/fidl.h>
#include <lib/async-loop/loop.h>
#include <lib/async/wait.h>
#include <lib/fidl-async/bind.h>
#include <lib/fidl/transport.h>
#include <lib/zx/socket.h>
#include <string.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

#include <unittest/unittest.h>

typedef struct fidl_socket_binding {
    async_wait_t wait;
    fidl_dispatch_t* dispatch;
    async_dispatcher_t* dispatcher;
    void* ctx;
    const void* ops;
} fidl_socket_binding_t;

typedef struct fidl_socket_connection {
    fidl_txn_t txn;
    zx_handle_t socket;
} fidl_socket_connection_t;

static zx_status_t fidl_socket_reply(fidl_txn_t* txn, const fidl_msg_t* msg) {
    fidl_socket_connection_t* conn = reinterpret_cast<fidl_socket_connection_t*>(txn);
    if (msg->num_handles > 0u) {
        zx_handle_close_many(msg->handles, msg->num_handles);
    }
    return fidl_socket_write_control(conn->socket, msg->bytes, msg->num_bytes);
}

static void fidl_socket_binding_destroy(fidl_socket_binding_t* binding) {
    zx_handle_close(binding->wait.object);
    free(binding);
}

static void fidl_socket_message_handler(async_dispatcher_t* dispatcher,
                                        async_wait_t* wait,
                                        zx_status_t status,
                                        const zx_packet_signal_t* signal) {
    fidl_socket_binding_t* binding = reinterpret_cast<fidl_socket_binding_t*>(wait);
    if (status != ZX_OK) {
        goto shutdown;
    }

    if (signal->observed & ZX_SOCKET_CONTROL_READABLE) {
        char bytes[1024];
        for (uint64_t i = 0; i < signal->count; i++) {
            fidl_msg_t msg = {
                .bytes = bytes,
                .handles = nullptr,
                .num_bytes = 0u,
                .num_handles = 0u,
            };
            size_t actual = 0u;
            status = fidl_socket_read_control(wait->object, msg.bytes,
                                              sizeof(bytes), &actual);
            msg.num_bytes = static_cast<uint32_t>(actual);
            if (status != ZX_OK) {
                goto shutdown;
            }
            fidl_socket_connection_t conn = {
                .txn = {.reply = fidl_socket_reply},
                .socket = wait->object,
            };
            status = binding->dispatch(binding->ctx, &conn.txn, &msg, binding->ops);
            switch (status) {
            case ZX_OK:
                status = async_begin_wait(dispatcher, wait);
                if (status != ZX_OK) {
                    goto shutdown;
                }
                return;
            default:
                goto shutdown;
            }
        }
    }

shutdown:
    fidl_socket_binding_destroy(binding);
}

zx_status_t fidl_bind_socket(async_dispatcher_t* dispatcher, zx_handle_t socket,
                             fidl_dispatch_t* dispatch, void* ctx, const void* ops) {
    fidl_socket_binding_t* binding = static_cast<fidl_socket_binding_t*>(
        calloc(1, sizeof(fidl_socket_binding_t)));
    binding->wait.handler = fidl_socket_message_handler;
    binding->wait.object = socket;
    binding->wait.trigger = ZX_SOCKET_CONTROL_READABLE | ZX_SOCKET_PEER_CLOSED;
    binding->dispatch = dispatch;
    binding->dispatcher = dispatcher;
    binding->ctx = ctx;
    binding->ops = ops;
    zx_status_t status = async_begin_wait(dispatcher, &binding->wait);
    if (status != ZX_OK) {
        fidl_socket_binding_destroy(binding);
    }
    return status;
}

static zx_status_t Control_Bind(void* ctx, const char* addr_data, size_t addr_size) {
    EXPECT_EQ('x', addr_data[0]);
    EXPECT_EQ(2u, addr_size);
    return ZX_OK;
}

static zx_status_t Control_GetPeerAddr(void* ctx, int32_t index, fidl_txn_t* txn) {
    EXPECT_EQ(5, index);
    return fidl_test_fakesocket_ControlGetPeerAddr_reply(txn, "abc", 3);
}

static const fidl_test_fakesocket_Control_ops_t kOps = {
    .Bind = Control_Bind,
    .GetPeerAddr = Control_GetPeerAddr,
};

static bool basic_test(void) {
    BEGIN_TEST;

    zx::socket client, server;
    ASSERT_EQ(ZX_OK, zx::socket::create(ZX_SOCKET_HAS_CONTROL, &client, &server));

    async_loop_t* loop = nullptr;
    ASSERT_EQ(ZX_OK, async_loop_create(&kAsyncLoopConfigNoAttachToThread, &loop), "");
    ASSERT_EQ(ZX_OK, async_loop_start_thread(loop, "spaceship-dispatcher", nullptr), "");

    async_dispatcher_t* dispatcher = async_loop_get_dispatcher(loop);

    ASSERT_EQ(ZX_OK, fidl_bind_socket(dispatcher, server.release(),
                                      reinterpret_cast<fidl_dispatch_t*>(fidl_test_fakesocket_Control_dispatch),
                                      nullptr, &kOps));

    ASSERT_EQ(ZX_OK, fidl_test_fakesocket_ControlBind(client.get(), "xy", 2u));

    char buffer[64];
    memset(buffer, 0, sizeof(buffer));
    size_t actual = 0u;
    ASSERT_EQ(ZX_OK, fidl_test_fakesocket_ControlGetPeerAddr(client.get(), 5, buffer, sizeof(buffer), &actual));
    ASSERT_EQ(3u, actual);
    buffer[3] = '\0';
    ASSERT_STR_EQ("abc", buffer);

    client.reset();

    async_loop_destroy(loop);

    END_TEST;
}

BEGIN_TEST_CASE(fakesocket_tests)
RUN_NAMED_TEST("basic test", basic_test)
END_TEST_CASE(fakesocket_tests)
