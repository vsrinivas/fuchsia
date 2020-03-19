// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/fidl-async/bind.h>
#include <string.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

#include <fidl/test/spaceship/c/fidl.h>
#include <unittest/unittest.h>

static zx_status_t SpaceShip_AdjustHeading(void* ctx, const uint32_t* stars_data,
                                           size_t stars_count, fidl_txn_t* txn) {
  EXPECT_EQ(3u, stars_count, "");
  EXPECT_EQ(11u, stars_data[0], "");
  EXPECT_EQ(0u, stars_data[1], "");
  EXPECT_EQ(UINT32_MAX, stars_data[2], "");
  return fidl_test_spaceship_SpaceShipAdjustHeading_reply(txn, -12);
}

static zx_status_t SpaceShip_ScanForLifeforms(void* ctx, fidl_txn_t* txn) {
  const uint32_t lifesigns[5] = {42u, 43u, UINT32_MAX, 0u, 9u};
  return fidl_test_spaceship_SpaceShipScanForLifeforms_reply(txn, lifesigns, 5);
}

static zx_status_t SpaceShip_ScanForTensorLifeforms(void* ctx, fidl_txn_t* txn) {
  uint32_t lifesigns[8][5][3] = {};
  // fill the array with increasing counter
  uint32_t counter = 0;
  for (size_t i = 0; i < 8; i++) {
    for (size_t j = 0; j < 5; j++) {
      for (size_t k = 0; k < 3; k++) {
        lifesigns[i][j][k] = counter;
        counter += 1;
      }
    }
  }
  return fidl_test_spaceship_SpaceShipScanForTensorLifeforms_reply(txn, lifesigns);
}

static zx_status_t SpaceShip_SetAstrometricsListener(void* ctx, zx_handle_t listener) {
  EXPECT_EQ(ZX_OK, fidl_test_spaceship_AstrometricsListenerOnNova(listener), "");
  EXPECT_EQ(ZX_OK, zx_handle_close(listener), "");
  return ZX_OK;
}

static zx_status_t SpaceShip_SetDefenseCondition(void* ctx, fidl_test_spaceship_Alert alert) {
  EXPECT_EQ(fidl_test_spaceship_Alert_RED, alert, "");
  return ZX_OK;
}

static zx_status_t SpaceShip_GetFuelRemaining(void* ctx, zx_handle_t cancel, fidl_txn_t* txn) {
  EXPECT_EQ(ZX_HANDLE_INVALID, cancel, "");
  const fidl_test_spaceship_FuelLevel level = {
      .reaction_mass = 1641u,
  };
  return fidl_test_spaceship_SpaceShipGetFuelRemaining_reply(txn, ZX_OK, &level);
}

static zx_status_t SpaceShip_AddFuelTank(void* ctx, const fidl_test_spaceship_FuelLevel* level,
                                         fidl_txn_t* txn) {
  return fidl_test_spaceship_SpaceShipAddFuelTank_reply(txn, level->reaction_mass / 2);
}

static const fidl_test_spaceship_SpaceShip_ops_t kOps = {
    .AdjustHeading = SpaceShip_AdjustHeading,
    .ScanForLifeforms = SpaceShip_ScanForLifeforms,
    .SetAstrometricsListener = SpaceShip_SetAstrometricsListener,
    .SetDefenseCondition = SpaceShip_SetDefenseCondition,
    .GetFuelRemaining = SpaceShip_GetFuelRemaining,
    .AddFuelTank = SpaceShip_AddFuelTank,
    .ScanForTensorLifeforms = SpaceShip_ScanForTensorLifeforms,
};

static bool spaceship_test(void) {
  BEGIN_TEST;

  zx_handle_t client, server;
  zx_status_t status = zx_channel_create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "");

  async_loop_t* loop = NULL;
  ASSERT_EQ(ZX_OK, async_loop_create(&kAsyncLoopConfigNoAttachToCurrentThread, &loop), "");
  ASSERT_EQ(ZX_OK, async_loop_start_thread(loop, "spaceship-dispatcher", NULL), "");

  async_dispatcher_t* dispatcher = async_loop_get_dispatcher(loop);
  fidl_bind(dispatcher, server, (fidl_dispatch_t*)fidl_test_spaceship_SpaceShip_dispatch, NULL,
            &kOps);

  {
    const uint32_t stars[3] = {11u, 0u, UINT32_MAX};
    int8_t result = 0;
    ASSERT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipAdjustHeading(client, stars, 3, &result), "");
    ASSERT_EQ(-12, result, "");
  }

  {
    const uint32_t num_stars_overflow = fidl_test_spaceship_MaxStarsAdjustHeading * 2;
    const uint32_t stars[num_stars_overflow];
    int8_t result = 0;
    ASSERT_EQ(
        ZX_ERR_INVALID_ARGS,
        fidl_test_spaceship_SpaceShipAdjustHeading(client, stars, num_stars_overflow, &result), "");
  }

  {
    int8_t result = 0;
    ASSERT_EQ(ZX_ERR_INVALID_ARGS,
              fidl_test_spaceship_SpaceShipAdjustHeading(client, NULL, 1u << 31, &result), "");
  }

  {
    uint32_t lifesigns[64];
    size_t actual = 0;
    ASSERT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipScanForLifeforms(client, lifesigns, 64, &actual),
              "");
    ASSERT_EQ(5u, actual, "");
    ASSERT_EQ(42u, lifesigns[0], "");
    ASSERT_EQ(43u, lifesigns[1], "");
    ASSERT_EQ(UINT32_MAX, lifesigns[2], "");
    ASSERT_EQ(0u, lifesigns[3], "");
    ASSERT_EQ(9u, lifesigns[4], "");
  }

  {
    uint32_t lifesigns[8][5][3];
    ASSERT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipScanForTensorLifeforms(client, lifesigns), "");
    uint32_t counter = 0;
    for (size_t i = 0; i < 8; i++) {
      for (size_t j = 0; j < 5; j++) {
        for (size_t k = 0; k < 3; k++) {
          ASSERT_EQ(counter, lifesigns[i][j][k], "");
          counter += 1;
        }
      }
    }
  }

  {
    zx_handle_t listener_client, listener_server;
    status = zx_channel_create(0, &listener_client, &listener_server);
    ASSERT_EQ(ZX_OK, status, "");
    ASSERT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipSetAstrometricsListener(client, listener_client),
              "");
    ASSERT_EQ(ZX_OK,
              zx_object_wait_one(listener_server, ZX_CHANNEL_READABLE, ZX_TIME_INFINITE, NULL), "");
    ASSERT_EQ(ZX_OK, zx_handle_close(listener_server), "");
  }

  {
    ASSERT_EQ(
        ZX_OK,
        fidl_test_spaceship_SpaceShipSetDefenseCondition(client, fidl_test_spaceship_Alert_RED),
        "");
  }

  {
    fidl_test_spaceship_FuelLevel level;
    ASSERT_EQ(
        ZX_OK,
        fidl_test_spaceship_SpaceShipGetFuelRemaining(client, ZX_HANDLE_INVALID, &status, &level),
        "");
    ASSERT_EQ(ZX_OK, status, "");
    ASSERT_EQ(1641u, level.reaction_mass, "");
  }

  {
    fidl_test_spaceship_FuelLevel level = {
        .reaction_mass = 9482,
    };
    uint32_t out_consumed = 0u;
    ASSERT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipAddFuelTank(client, &level, &out_consumed), "");
    ASSERT_EQ(4741u, out_consumed, "");
  }

  ASSERT_EQ(ZX_OK, zx_handle_close(client), "");

  async_loop_destroy(loop);

  END_TEST;
}

BEGIN_TEST_CASE(spaceship_tests)
RUN_NAMED_TEST("fidl.test.spaceship.SpaceShip test", spaceship_test)
END_TEST_CASE(spaceship_tests);
