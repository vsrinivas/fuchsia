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
#include <zircon/status.h>

#include <unittest/unittest.h>

static void destroy_wait(async::Wait* wait) {
    zx_handle_close(wait->object());
    delete wait;
}

static zx_status_t start_socket_server(async_dispatcher_t* dispatcher,
                                       zx::socket remote);

static zx_status_t handle_message(async_dispatcher_t* dispatcher,
                                  async::Wait* wait, zxsio_msg_t* msg) {
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
        if (payload.level != IPPROTO_IP || payload.optname != IP_TTL
            || payload.optlen != 0) {
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
    case ZXSIO_CONNECT: {
        // No reply needed.
        break;
    }
    case ZXSIO_BIND: {
        // No reply needed.
        break;
    }
    case ZXSIO_LISTEN: {
        int backlog = -1;
        memcpy(&backlog, msg->data, sizeof(backlog));
        if (backlog != 5) {
            printf("ZXSIO_LISTEN received backlog=%d.\n", backlog);
            return ZX_ERR_STOP;
        }

        zx::socket local, remote;
        zx_status_t status = zx::socket::create(ZX_SOCKET_HAS_CONTROL,
                                                &local, &remote);
        if (status != ZX_OK) {
            printf("ZXSIO_LISTEN failed to create sockets: %d (%s)\n",
                   status, zx_status_get_string(status));
            return ZX_ERR_STOP;
        }

        status = zx_socket_share(wait->object(), remote.release());
        if (status != ZX_OK) {
            printf("ZXSIO_LISTEN failed to share socket: %d (%s)\n",
                   status, zx_status_get_string(status));
            return ZX_ERR_STOP;
        }

        status = start_socket_server(dispatcher, fbl::move(local));
        if (status != ZX_OK) {
            printf("ZXSIO_LISTEN failed to start socket server: %d (%s)\n",
                   status, zx_status_get_string(status));
            return ZX_ERR_STOP;
        }
        break;
    }
    case ZXSIO_CLOSE:
    case ZXSIO_OPEN:
    case ZXSIO_IOCTL:
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

            zx_status_t status = handle_message(dispatcher, wait, &msg);
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

struct FakeNetstack {
    async_loop_t* loop;
    zxs_socket_t socket;
};

static bool SetUp(FakeNetstack* fake) {
    zx_status_t status = ZX_OK;

    ASSERT_EQ(ZX_OK, async_loop_create(&kAsyncLoopConfigNoAttachToThread, &fake->loop), "");
    ASSERT_EQ(ZX_OK, async_loop_start_thread(fake->loop, "fake-netstack", nullptr), "");

    async_dispatcher_t* dispatcher = async_loop_get_dispatcher(fake->loop);

    zx::socket local, remote;
    status = zx::socket::create(ZX_SOCKET_HAS_CONTROL | ZX_SOCKET_HAS_ACCEPT,
                                &local, &remote);
    ASSERT_EQ(ZX_OK, status);

    status = start_socket_server(dispatcher, fbl::move(remote));
    ASSERT_EQ(ZX_OK, status);

    fake->socket = {
        .socket = local.release(),
        .flags = 0u,
    };

    return true;
}

static void TearDown(FakeNetstack* fake) {
    zx_handle_close(fake->socket.socket);
    async_loop_destroy(fake->loop);
}

static bool connect_test(void) {
    BEGIN_TEST;

    FakeNetstack fake;
    if (!SetUp(&fake))
        return false;
    zxs_socket_t* socket = &fake.socket;

    struct sockaddr addr;
    memset(&addr, 0, sizeof(addr));
    ASSERT_EQ(ZX_OK, zxs_connect(socket, &addr, sizeof(addr)));

    TearDown(&fake);

    END_TEST;
}

static bool bind_test(void) {
    BEGIN_TEST;

    FakeNetstack fake;
    if (!SetUp(&fake))
        return false;
    zxs_socket_t* socket = &fake.socket;

    struct sockaddr addr;
    memset(&addr, 0, sizeof(addr));
    ASSERT_EQ(ZX_OK, zxs_bind(socket, &addr, sizeof(addr)));

    TearDown(&fake);

    END_TEST;
}

static bool getsockname_test(void) {
    BEGIN_TEST;

    FakeNetstack fake;
    if (!SetUp(&fake))
        return false;
    zxs_socket_t* socket = &fake.socket;

    struct sockaddr addr;
    memset(&addr, 0, sizeof(addr));
    size_t actual = 0u;
    ASSERT_EQ(ZX_OK, zxs_getsockname(socket, &addr, sizeof(addr), &actual));
    ASSERT_EQ(sizeof(addr), actual);
    ASSERT_EQ('s', addr.sa_data[4]);

    TearDown(&fake);

    END_TEST;
}

static bool getpeername_test(void) {
    BEGIN_TEST;

    FakeNetstack fake;
    if (!SetUp(&fake))
        return false;
    zxs_socket_t* socket = &fake.socket;

    struct sockaddr addr;
    memset(&addr, 0, sizeof(addr));
    size_t actual = 0u;
    ASSERT_EQ(ZX_OK, zxs_getpeername(socket, &addr, sizeof(addr), &actual));
    ASSERT_EQ(sizeof(addr), actual);
    ASSERT_EQ('p', addr.sa_data[4]);

    TearDown(&fake);

    END_TEST;
}

static bool sockopts_test(void) {
    BEGIN_TEST;

    FakeNetstack fake;
    if (!SetUp(&fake))
        return false;
    zxs_socket_t* socket = &fake.socket;

    int ttl = 255;
    zxs_option_t option = {
        .level = IPPROTO_IP,
        .name = IP_TTL,
        .value = &ttl,
        .length = sizeof(ttl),
    };

    ASSERT_EQ(ZX_OK, zxs_setsockopts(socket, &option, 1u));

    ttl = 0;
    size_t actual = 0u;
    ASSERT_EQ(ZX_OK, zxs_getsockopt(socket, IPPROTO_IP, IP_TTL, &ttl,
                                    sizeof(ttl), &actual));
    ASSERT_EQ(sizeof(int), actual);
    ASSERT_EQ(128, ttl);

    TearDown(&fake);

    END_TEST;
}

static bool listen_accept_test(void) {
    BEGIN_TEST;

    FakeNetstack fake;
    if (!SetUp(&fake))
        return false;
    zxs_socket_t* socket = &fake.socket;
    socket->flags = ZXS_FLAG_BLOCKING;

    ASSERT_EQ(ZX_OK, zxs_listen(socket, 5));

    struct sockaddr addr;
    memset(&addr, 0, sizeof(addr));
    size_t actual = 0u;
    zxs_socket_t accepted;
    memset(&accepted, 0, sizeof(accepted));

    ASSERT_EQ(ZX_OK, zxs_accept(socket, &addr, sizeof(addr), &actual, &accepted));
    ASSERT_EQ(sizeof(addr), actual);
    ASSERT_EQ('p', addr.sa_data[4]);
    ASSERT_NE(ZX_HANDLE_INVALID, accepted.socket);
    ASSERT_EQ(ZX_OK, zx_handle_close(accepted.socket));

    TearDown(&fake);

    END_TEST;
}

BEGIN_TEST_CASE(zxs_test)
RUN_TEST(connect_test);
RUN_TEST(bind_test);
RUN_TEST(getsockname_test);
RUN_TEST(getpeername_test);
RUN_TEST(sockopts_test);
RUN_TEST(listen_accept_test);
END_TEST_CASE(zxs_test)
