// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/spaceship/c/fidl.h>
#include <string.h>
#include <threads.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

#include <unittest/unittest.h>

static zx_status_t SpaceShip_AdjustHeading(void* ctx, const uint32_t* stars_data, size_t stars_count, fidl_txn_t* txn) {
    EXPECT_EQ(3u, stars_count, "");
    EXPECT_EQ(11u, stars_data[0], "");
    EXPECT_EQ(0u, stars_data[1], "");
    EXPECT_EQ(UINT32_MAX, stars_data[2], "");
    return fidl_test_spaceship_SpaceShipAdjustHeading_reply(txn, -12);
}

static zx_status_t SpaceShip_ScanForLifeforms(void* ctx, fidl_txn_t* txn) {
    const uint32_t lifesigns[5] = { 42u, 43u, UINT32_MAX, 0u, 9u };
    return fidl_test_spaceship_SpaceShipScanForLifeforms_reply(txn, lifesigns, 5);
}

static const fidl_test_spaceship_SpaceShip_ops_t kOps = {
    .AdjustHeading = SpaceShip_AdjustHeading,
    .ScanForLifeforms = SpaceShip_ScanForLifeforms,
};

typedef struct spaceship_connection {
    fidl_txn_t txn;
    zx_handle_t channel;
    zx_txid_t txid;
} spaceship_connection_t;

static zx_status_t spaceship_reply(fidl_txn_t* txn, const fidl_msg_t* msg) {
    spaceship_connection_t* conn = (spaceship_connection_t*)txn;
    if (msg->num_bytes < sizeof(fidl_message_header_t))
        return ZX_ERR_INVALID_ARGS;
    fidl_message_header_t* hdr = (fidl_message_header_t*)msg->bytes;
    hdr->txid = conn->txid;
    conn->txid = 0;
    return zx_channel_write(conn->channel, 0, msg->bytes, msg->num_bytes,
                            msg->handles, msg->num_handles);
}

static int spaceship_server(void* ctx) {
    spaceship_connection_t conn = {
        .txn.reply = spaceship_reply,
        .channel = *(zx_handle_t*)ctx,
    };
    zx_status_t status = ZX_OK;
    while (status == ZX_OK) {
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
            status = fidl_test_spaceship_SpaceShip_dispatch(NULL, &conn.txn, &msg, &kOps);
            ASSERT_EQ(ZX_OK, status, "");
        } else {
            break;
        }
    }

    zx_handle_close(conn.channel);
    return 0;
}

static bool spaceship_test(void) {
    BEGIN_TEST;

    zx_handle_t client, server;
    zx_status_t status = zx_channel_create(0, &client, &server);
    ASSERT_EQ(ZX_OK, status, "");

    thrd_t thread;
    int rv = thrd_create(&thread, spaceship_server, &server);
    ASSERT_EQ(thrd_success, rv, "");

    {
        const uint32_t stars[3] = { 11u, 0u, UINT32_MAX };
        int8_t result = 0;
        ASSERT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipAdjustHeading(client, stars, 3, &result), "");
        ASSERT_EQ(-12, result, "");
    }

    {
        uint32_t lifesigns[64];
        size_t actual = 0;
        ASSERT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipScanForLifeforms(client, lifesigns, 64, &actual), "");
        ASSERT_EQ(5u, actual, "");
        ASSERT_EQ(42u, lifesigns[0], "");
        ASSERT_EQ(43u, lifesigns[1], "");
        ASSERT_EQ(UINT32_MAX, lifesigns[2], "");
        ASSERT_EQ(0u, lifesigns[3], "");
        ASSERT_EQ(9u, lifesigns[4], "");
    }

    ASSERT_EQ(ZX_OK, zx_handle_close(client), "");

    int result = 0;
    rv = thrd_join(thread, &result);
    ASSERT_EQ(thrd_success, rv, "");

    END_TEST;
}

BEGIN_TEST_CASE(spaceship_tests)
RUN_NAMED_TEST("fidl.test.spaceship.SpaceShip test", spaceship_test)
END_TEST_CASE(spaceship_tests);
