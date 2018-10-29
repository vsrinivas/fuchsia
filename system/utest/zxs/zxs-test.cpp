// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/type_support.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/socket.h>
#include <lib/zxs/inception.h>
#include <lib/zxs/protocol.h>
#include <lib/zxs/zxs.h>
#include <string.h>
#include <sys/socket.h>

#include <unittest/unittest.h>

static void destroy_wait(async::Wait* wait) {
    zx_handle_close(wait->object());
    delete wait;
}

static zx_status_t handle_message(async::Wait* wait, zxsio_msg_t* msg) {
    zxsio_msg_t reply;
    memset(&reply, 0, sizeof(reply));
    reply.txid = msg->txid;
    reply.op = msg->op;

    switch (msg->op) {
    case ZXSIO_GETSOCKNAME: {
        struct sockaddr addr;
        memset(&addr, 0, sizeof(addr));
        addr.sa_family = AF_IPX;
        addr.sa_data[0] = 'i';
        addr.sa_data[1] = 'p';
        addr.sa_data[2] = 'x';
        addr.sa_data[3] = ' ';
        addr.sa_data[4] = 's';
        addr.sa_data[5] = 'o';
        addr.sa_data[6] = 'c';
        addr.sa_data[7] = 'k';

        zxrio_sockaddr_reply_t payload;
        memset(&payload, 0, sizeof(payload));
        memcpy(&payload.addr, &addr, sizeof(addr));
        payload.len = sizeof(addr);

        reply.datalen = sizeof(payload);
        memcpy(reply.data, &payload, sizeof(payload));
        break;
    }
    case ZXSIO_GETPEERNAME: {
        struct sockaddr addr;
        memset(&addr, 0, sizeof(addr));
        addr.sa_family = AF_IPX;
        addr.sa_data[0] = 'i';
        addr.sa_data[1] = 'p';
        addr.sa_data[2] = 'x';
        addr.sa_data[3] = ' ';
        addr.sa_data[4] = 'p';
        addr.sa_data[5] = 'e';
        addr.sa_data[6] = 'e';
        addr.sa_data[7] = 'r';

        zxrio_sockaddr_reply_t payload;
        memset(&payload, 0, sizeof(payload));
        memcpy(&payload.addr, &addr, sizeof(addr));
        payload.len = sizeof(addr);

        reply.datalen = sizeof(payload);
        memcpy(reply.data, &payload, sizeof(payload));
        break;
    }
    case ZXSIO_SETSOCKOPT: {
        zxrio_sockopt_req_reply_t payload;
        memset(&payload, 0, sizeof(payload));
        memcpy(&payload, msg->data, sizeof(payload));
        if (payload.level != IPPROTO_IP || payload.optname != IP_TTL) {
            return ZX_ERR_STOP;
        }
        break;
    }
    case ZXSIO_GETSOCKOPT: {
        zxrio_sockopt_req_reply_t payload;
        memset(&payload, 0, sizeof(payload));
        memcpy(&payload, msg->data, sizeof(payload));
        if (payload.level != IPPROTO_IP || payload.optname != IP_TTL) {
            return ZX_ERR_STOP;
        }
        int result = 128;
        memset(payload.optval, 0, sizeof(payload.optval));
        memcpy(payload.optval, &result, sizeof(result));
        payload.optlen = sizeof(int);

        reply.datalen = sizeof(payload);
        memcpy(reply.data, &payload, sizeof(payload));
        break;
    }
    case ZXSIO_CLOSE:
    case ZXSIO_OPEN:
    case ZXSIO_IOCTL:
    case ZXSIO_CONNECT:
    case ZXSIO_BIND:
    case ZXSIO_LISTEN:
    default:
        return ZX_ERR_STOP;
    }

    size_t actual = 0u;
    zx_status_t status = zx_socket_write(
        wait->object(), ZX_SOCKET_CONTROL, &reply, ZXSIO_HDR_SZ + reply.datalen,
        &actual);
    EXPECT_EQ(ZX_OK, status);
    EXPECT_EQ(ZXSIO_HDR_SZ + reply.datalen, actual);
    return ZX_OK;
}

static zx_status_t start_socket_server(async_dispatcher_t* dispatcher,
                                       zx::socket remote) {
    auto wait = new async::Wait(
        remote.release(), ZX_SOCKET_CONTROL_READABLE | ZX_SOCKET_PEER_CLOSED);

    wait->set_handler([](async_dispatcher_t* dispatcher,
                         async::Wait* wait,
                         zx_status_t status,
                         const zx_packet_signal_t* signal) {
        if (status != ZX_OK) {
            destroy_wait(wait);
        } else if (signal->observed & ZX_SOCKET_CONTROL_READABLE) {
            zxsio_msg_t msg;
            memset(&msg, 0, sizeof(msg));
            size_t actual = 0u;
            status = zx_socket_read(wait->object(), ZX_SOCKET_CONTROL, &msg,
                                    sizeof(msg), &actual);
            if (status != ZX_OK) {
                destroy_wait(wait);
                return;
            }

            zx_status_t status = handle_message(wait, &msg);
            if (status != ZX_OK) {
                destroy_wait(wait);
                return;
            }

            status = wait->Begin(dispatcher);
            if (status != ZX_OK) {
                destroy_wait(wait);
                return;
            }
        } else if (signal->observed & ZX_SOCKET_PEER_CLOSED) {
            destroy_wait(wait);
        }
    });
    zx_status_t status = wait->Begin(dispatcher);
    if (status != ZX_OK) {
        destroy_wait(wait);
    }
    return status;
}

bool basic_test(void) {
    BEGIN_TEST;
    zx_status_t status = ZX_OK;

    async_loop_t* loop = nullptr;
    ASSERT_EQ(ZX_OK, async_loop_create(&kAsyncLoopConfigNoAttachToThread, &loop), "");
    ASSERT_EQ(ZX_OK, async_loop_start_thread(loop, "fake-netstack", nullptr), "");

    async_dispatcher_t* dispatcher = async_loop_get_dispatcher(loop);

    zx::socket local, remote;
    status = zx::socket::create(ZX_SOCKET_HAS_CONTROL, &local, &remote);
    ASSERT_EQ(ZX_OK, status);

    status = start_socket_server(dispatcher, fbl::move(remote));
    ASSERT_EQ(ZX_OK, status);

    zxs_socket_t socket = {
        .socket = local.get(),
        .flags = 0u,
    };

    struct sockaddr addr;
    memset(&addr, 0, sizeof(addr));
    status = zxs_connect(&socket, &addr, sizeof(addr));
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, status);

    memset(&addr, 0, sizeof(addr));
    size_t actual = 0u;
    status = zxs_getsockname(&socket, &addr, sizeof(addr), &actual);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(sizeof(addr), actual);
    ASSERT_EQ('s', addr.sa_data[4]);

    memset(&addr, 0, sizeof(addr));
    actual = 0u;
    status = zxs_getpeername(&socket, &addr, sizeof(addr), &actual);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(sizeof(addr), actual);
    ASSERT_EQ('p', addr.sa_data[4]);

    int ttl = 255;
    zxs_option_t option = {
        .level = IPPROTO_IP,
        .name = IP_TTL,
        .value = &ttl,
        .length = sizeof(ttl),
    };

    status = zxs_setsockopts(&socket, &option, 1u);
    ASSERT_EQ(ZX_OK, status);

    ttl = 0;
    actual = 0u;
    status = zxs_getsockopt(&socket, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl),
                            &actual);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(sizeof(int), actual);
    ASSERT_EQ(128, ttl);

    local.reset();

    async_loop_destroy(loop);

    END_TEST;
}

BEGIN_TEST_CASE(zxs_test)
RUN_TEST(basic_test);
END_TEST_CASE(zxs_test)
