// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/type_support.h>
#include <fidl/test/spaceship/c/fidl.h>
#include <lib/async-loop/loop.h>
#include <lib/fidl/cpp/bind.h>
#include <lib/zx/channel.h>
#include <string.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

#include <unittest/unittest.h>

class SpaceShip {
public:
    zx_status_t AdjustHeading(const uint32_t* stars_data, size_t stars_count, fidl_txn_t* txn) {
        EXPECT_EQ(3u, stars_count, "");
        EXPECT_EQ(11u, stars_data[0], "");
        EXPECT_EQ(0u, stars_data[1], "");
        EXPECT_EQ(UINT32_MAX, stars_data[2], "");
        return fidl_test_spaceship_SpaceShipAdjustHeading_reply(txn, -12);
    }

    zx_status_t ScanForLifeforms(fidl_txn_t* txn) {
        const uint32_t lifesigns[5] = {42u, 43u, UINT32_MAX, 0u, 9u};
        return fidl_test_spaceship_SpaceShipScanForLifeforms_reply(txn, lifesigns, 5);
    }

    zx_status_t SetAstrometricsListener(zx_handle_t listener) {
        EXPECT_EQ(ZX_OK, fidl_test_spaceship_AstrometricsListenerOnNova(listener), "");
        EXPECT_EQ(ZX_OK, zx_handle_close(listener), "");
        return ZX_OK;
    }

    zx_status_t SetDefenseCondition(fidl_test_spaceship_Alert alert) {
        EXPECT_EQ(fidl_test_spaceship_Alert_RED, alert, "");
        return ZX_OK;
    }

    zx_status_t GetFuelRemaining(zx_handle_t cancel, fidl_txn_t* txn) {
        EXPECT_EQ(ZX_HANDLE_INVALID, cancel, "");
        const fidl_test_spaceship_FuelLevel level = {
            .reaction_mass = 1641u,
        };
        return fidl_test_spaceship_SpaceShipGetFuelRemaining_reply(txn, ZX_OK, &level);
    }

    zx_status_t AddFuelTank(const fidl_test_spaceship_FuelLevel* level, fidl_txn_t* txn) {
        return fidl_test_spaceship_SpaceShipAddFuelTank_reply(txn, level->reaction_mass / 2);
    }


    zx_status_t Bind(async_dispatcher_t* dispatcher, zx::channel channel) {
        static constexpr fidl_test_spaceship_SpaceShip_ops_t kOps = {
            .AdjustHeading = fidl::BindMember<&SpaceShip::AdjustHeading>,
            .ScanForLifeforms = fidl::BindMember<&SpaceShip::ScanForLifeforms>,
            .SetAstrometricsListener = fidl::BindMember<&SpaceShip::SetAstrometricsListener>,
            .SetDefenseCondition = fidl::BindMember<&SpaceShip::SetDefenseCondition>,
            .GetFuelRemaining = fidl::BindMember<&SpaceShip::GetFuelRemaining>,
            .AddFuelTank = fidl::BindMember<&SpaceShip::AddFuelTank>,
        };

        return fidl::BindOps<fidl_test_spaceship_SpaceShip_dispatch>(
            dispatcher, fbl::move(channel), this, &kOps);
    }
};

static bool spaceship_test(void) {
    BEGIN_TEST;

    zx::channel client, server;
    zx_status_t status = zx::channel::create(0, &client, &server);
    ASSERT_EQ(ZX_OK, status, "");

    async_loop_t* loop = NULL;
    ASSERT_EQ(ZX_OK, async_loop_create(&kAsyncLoopConfigNoAttachToThread, &loop), "");
    ASSERT_EQ(ZX_OK, async_loop_start_thread(loop, "spaceship-dispatcher", NULL), "");

    async_dispatcher_t* dispatcher = async_loop_get_dispatcher(loop);
    SpaceShip ship;
    ASSERT_EQ(ZX_OK, ship.Bind(dispatcher, fbl::move(server)));

    {
        const uint32_t stars[3] = {11u, 0u, UINT32_MAX};
        int8_t result = 0;
        ASSERT_EQ(ZX_OK,
                  fidl_test_spaceship_SpaceShipAdjustHeading(client.get(), stars, 3, &result));
        ASSERT_EQ(-12, result, "");
    }

    {
        uint32_t lifesigns[64];
        size_t actual = 0;
        ASSERT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipScanForLifeforms(client.get(), lifesigns,
                                                                       64, &actual));
        ASSERT_EQ(5u, actual, "");
        ASSERT_EQ(42u, lifesigns[0], "");
        ASSERT_EQ(43u, lifesigns[1], "");
        ASSERT_EQ(UINT32_MAX, lifesigns[2], "");
        ASSERT_EQ(0u, lifesigns[3], "");
        ASSERT_EQ(9u, lifesigns[4], "");
    }

    {
        zx::channel listener_client, listener_server;
        status = zx::channel::create(0, &listener_client, &listener_server);
        ASSERT_EQ(ZX_OK, status, "");
        ASSERT_EQ(ZX_OK,
                  fidl_test_spaceship_SpaceShipSetAstrometricsListener(client.get(),
                                                                       listener_client.release()));
        ASSERT_EQ(ZX_OK, listener_server.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), NULL));
        ASSERT_EQ(ZX_OK, zx_handle_close(listener_server.release()));
    }

    {
        ASSERT_EQ(ZX_OK,
                  fidl_test_spaceship_SpaceShipSetDefenseCondition(client.get(),
                                                                   fidl_test_spaceship_Alert_RED));
    }

    {
        fidl_test_spaceship_FuelLevel level;
        ASSERT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipGetFuelRemaining(client.get(),
                                                                       ZX_HANDLE_INVALID, &status,
                                                                       &level));
        ASSERT_EQ(ZX_OK, status, "");
        ASSERT_EQ(1641u, level.reaction_mass, "");
    }

    {
        fidl_test_spaceship_FuelLevel level = {
            .reaction_mass = 9482,
        };
        uint32_t out_consumed = 0u;
        ASSERT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipAddFuelTank(client.get(), &level,
                                                                  &out_consumed));
        ASSERT_EQ(4741u, out_consumed, "");
    }

    ASSERT_EQ(ZX_OK, zx_handle_close(client.release()));

    async_loop_destroy(loop);

    END_TEST;
}

BEGIN_TEST_CASE(spaceship_tests_cpp)
RUN_NAMED_TEST("fidl.test.spaceship.SpaceShip test", spaceship_test)
END_TEST_CASE(spaceship_tests_cpp);
