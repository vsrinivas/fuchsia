// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/spaceship/c/fidl.h>
#include <lib/async-loop/loop.h>
#include <lib/fidl/bind.h>
#include <string.h>
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

static bool spaceship_test(void) {
    BEGIN_TEST;

    zx_handle_t client, server;
    zx_status_t status = zx_channel_create(0, &client, &server);
    ASSERT_EQ(ZX_OK, status, "");

    async_loop_t* loop = NULL;
    ASSERT_EQ(ZX_OK, async_loop_create(NULL, &loop), "");
    ASSERT_EQ(ZX_OK, async_loop_start_thread(loop, "spaceship-dispatcher", NULL), "");

    async_dispatcher_t* dispacher = async_loop_get_dispatcher(loop);
    fidl_bind(dispacher, server, (fidl_dispatch_t*)fidl_test_spaceship_SpaceShip_dispatch, NULL, &kOps);

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

    async_loop_destroy(loop);

    END_TEST;
}

BEGIN_TEST_CASE(spaceship_tests)
RUN_NAMED_TEST("fidl.test.spaceship.SpaceShip test", spaceship_test)
END_TEST_CASE(spaceship_tests);
