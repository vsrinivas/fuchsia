// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/fidl-utils/bind.h>
#include <lib/zx/channel.h>
#include <string.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

#include <utility>

#include <fidl/test/spaceship/c/fidl.h>
#include <unittest/unittest.h>

namespace {

class SpaceShip {
 public:
  using SpaceShipBinder = fidl::Binder<SpaceShip>;

  virtual ~SpaceShip() {}

  virtual zx_status_t AdjustHeading(const uint32_t* stars_data, size_t stars_count,
                                    fidl_txn_t* txn) {
    EXPECT_EQ(3u, stars_count, "");
    EXPECT_EQ(11u, stars_data[0], "");
    EXPECT_EQ(0u, stars_data[1], "");
    EXPECT_EQ(UINT32_MAX, stars_data[2], "");
    return fidl_test_spaceship_SpaceShipAdjustHeading_reply(txn, -12);
  }

  virtual zx_status_t ScanForLifeforms(fidl_txn_t* txn) {
    const uint32_t lifesigns[5] = {42u, 43u, UINT32_MAX, 0u, 9u};
    return fidl_test_spaceship_SpaceShipScanForLifeforms_reply(txn, lifesigns, 5);
  }

  virtual zx_status_t ScanForTensorLifeforms(fidl_txn_t* txn) {
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

  virtual zx_status_t SetAstrometricsListener(zx_handle_t listener) {
    EXPECT_EQ(ZX_OK, fidl_test_spaceship_AstrometricsListenerOnNova(listener), "");
    EXPECT_EQ(ZX_OK, zx_handle_close(listener), "");
    return ZX_OK;
  }

  virtual zx_status_t SetDefenseCondition(fidl_test_spaceship_Alert alert) {
    EXPECT_EQ(fidl_test_spaceship_Alert_RED, alert, "");
    return ZX_OK;
  }

  virtual zx_status_t GetFuelRemaining(zx_handle_t cancel, fidl_txn_t* txn) {
    EXPECT_EQ(ZX_HANDLE_INVALID, cancel, "");
    const fidl_test_spaceship_FuelLevel level = {
        .reaction_mass = 1641u,
    };
    return fidl_test_spaceship_SpaceShipGetFuelRemaining_reply(txn, ZX_OK, &level);
  }

  virtual zx_status_t AddFuelTank(const fidl_test_spaceship_FuelLevel* level, fidl_txn_t* txn) {
    return fidl_test_spaceship_SpaceShipAddFuelTank_reply(txn, level->reaction_mass / 2);
  }
  virtual zx_status_t ActivateShields(fidl_test_spaceship_Shields shields) { return ZX_OK; }

  virtual zx_status_t Bind(async_dispatcher_t* dispatcher, zx::channel channel) {
    static constexpr fidl_test_spaceship_SpaceShip_ops_t kOps = {
        .AdjustHeading = SpaceShipBinder::BindMember<&SpaceShip::AdjustHeading>,
        .ScanForLifeforms = SpaceShipBinder::BindMember<&SpaceShip::ScanForLifeforms>,
        .SetAstrometricsListener = SpaceShipBinder::BindMember<&SpaceShip::SetAstrometricsListener>,
        .SetDefenseCondition = SpaceShipBinder::BindMember<&SpaceShip::SetDefenseCondition>,
        .GetFuelRemaining = SpaceShipBinder::BindMember<&SpaceShip::GetFuelRemaining>,
        .AddFuelTank = SpaceShipBinder::BindMember<&SpaceShip::AddFuelTank>,
        .ScanForTensorLifeforms = SpaceShipBinder::BindMember<&SpaceShip::ScanForTensorLifeforms>,
        .ActivateShields = SpaceShipBinder::BindMember<&SpaceShip::ActivateShields>,
    };

    return SpaceShipBinder::BindOps<fidl_test_spaceship_SpaceShip_dispatch>(
        dispatcher, std::move(channel), this, &kOps);
  }
};

bool spaceship_test(void) {
  BEGIN_TEST;

  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "");

  async_loop_t* loop = NULL;
  ASSERT_EQ(ZX_OK, async_loop_create(&kAsyncLoopConfigNoAttachToCurrentThread, &loop), "");
  ASSERT_EQ(ZX_OK, async_loop_start_thread(loop, "spaceship-dispatcher", NULL), "");

  async_dispatcher_t* dispatcher = async_loop_get_dispatcher(loop);
  SpaceShip ship;
  ASSERT_EQ(ZX_OK, ship.Bind(dispatcher, std::move(server)));

  {
    const uint32_t stars[3] = {11u, 0u, UINT32_MAX};
    int8_t result = 0;
    ASSERT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipAdjustHeading(client.get(), stars, 3, &result));
    ASSERT_EQ(-12, result, "");
  }

  {
    constexpr uint32_t kNumStarsOverflow = fidl_test_spaceship_MaxStarsAdjustHeading * 2;
    const uint32_t stars[kNumStarsOverflow] = {};
    int8_t result = 0;
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, fidl_test_spaceship_SpaceShipAdjustHeading(
                                       client.get(), stars, kNumStarsOverflow, &result));
  }

  {
    int8_t result = 0;
    ASSERT_EQ(ZX_ERR_INVALID_ARGS,
              fidl_test_spaceship_SpaceShipAdjustHeading(client.get(), nullptr, 1 << 31, &result));
  }

  {
    uint32_t lifesigns[64];
    size_t actual = 0;
    ASSERT_EQ(ZX_OK,
              fidl_test_spaceship_SpaceShipScanForLifeforms(client.get(), lifesigns, 64, &actual));
    ASSERT_EQ(5u, actual, "");
    ASSERT_EQ(42u, lifesigns[0], "");
    ASSERT_EQ(43u, lifesigns[1], "");
    ASSERT_EQ(UINT32_MAX, lifesigns[2], "");
    ASSERT_EQ(0u, lifesigns[3], "");
    ASSERT_EQ(9u, lifesigns[4], "");
  }

  {
    uint32_t lifesigns[8][5][3];
    ASSERT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipScanForTensorLifeforms(client.get(), lifesigns),
              "");
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
    zx::channel listener_client, listener_server;
    status = zx::channel::create(0, &listener_client, &listener_server);
    ASSERT_EQ(ZX_OK, status, "");
    ASSERT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipSetAstrometricsListener(
                         client.get(), listener_client.release()));
    ASSERT_EQ(ZX_OK, listener_server.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), NULL));
    ASSERT_EQ(ZX_OK, zx_handle_close(listener_server.release()));
  }

  {
    ASSERT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipSetDefenseCondition(
                         client.get(), fidl_test_spaceship_Alert_RED));
  }

  {
    fidl_test_spaceship_FuelLevel level;
    ASSERT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipGetFuelRemaining(client.get(), ZX_HANDLE_INVALID,
                                                                   &status, &level));
    ASSERT_EQ(ZX_OK, status, "");
    ASSERT_EQ(1641u, level.reaction_mass, "");
  }

  {
    fidl_test_spaceship_FuelLevel level = {
        .reaction_mass = 9482,
    };
    uint32_t out_consumed = 0u;
    ASSERT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipAddFuelTank(client.get(), &level, &out_consumed));
    ASSERT_EQ(4741u, out_consumed, "");
  }

  ASSERT_EQ(ZX_OK, zx_handle_close(client.release()));

  async_loop_destroy(loop);

  END_TEST;
}

// A variant of spaceship which responds to requests asynchronously.
class AsyncSpaceShip : public SpaceShip {
 public:
  using SpaceShipBinder = fidl::Binder<AsyncSpaceShip>;

  virtual ~AsyncSpaceShip() {}

  // Creates a |fidl_async_txn| using the C++ wrapper, and pushes the
  // computation to a background thread.
  //
  // This background thread responds to the original |txn|, and rebinds the connection
  // to the dispatcher.
  zx_status_t AdjustHeading(const uint32_t* stars_data, size_t stars_count,
                            fidl_txn_t* txn) override {
    EXPECT_EQ(3u, stars_count, "");
    EXPECT_EQ(11u, stars_data[0], "");
    EXPECT_EQ(0u, stars_data[1], "");
    EXPECT_EQ(UINT32_MAX, stars_data[2], "");
    static constexpr auto handler = [](void* arg) {
      auto spaceship = reinterpret_cast<AsyncSpaceShip*>(arg);
      EXPECT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipAdjustHeading_reply(
                           spaceship->async_txn_.Transaction(), -12));
      EXPECT_EQ(ZX_OK, spaceship->async_txn_.Rebind());
      return 0;
    };

    async_txn_.Reset(txn);
    EXPECT_EQ(thrd_success, thrd_create(&thrd_, handler, this));
    return ZX_ERR_ASYNC;
  }

  // Creates a |fidl_async_txn| using the C++ wrapper, and pushes the
  // computation to a background thread.
  //
  // This background thread responds to the original |txn|, but does not rebind
  // the connection to the dispatcher. This completes the asynchronous transaction and destroys
  // the original binding.
  zx_status_t ScanForLifeforms(fidl_txn_t* txn) override {
    static constexpr auto handler = [](void* arg) {
      auto spaceship = reinterpret_cast<AsyncSpaceShip*>(arg);
      const uint32_t lifesigns[2] = {42u, 43u};
      EXPECT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipScanForLifeforms_reply(
                           spaceship->async_txn_.Transaction(), lifesigns, 2));
      spaceship->async_txn_.Reset();
      return 0;
    };

    async_txn_.Reset(txn);
    EXPECT_EQ(thrd_success, thrd_create(&thrd_, handler, this));
    return ZX_ERR_ASYNC;
  }

  void Join() {
    int res;
    EXPECT_EQ(thrd_success, thrd_join(thrd_, &res));
    EXPECT_EQ(0, res);
  }

  zx_status_t Bind(async_dispatcher_t* dispatcher, zx::channel channel) override {
    static constexpr fidl_test_spaceship_SpaceShip_ops_t kOps = {
        .AdjustHeading = SpaceShipBinder::BindMember<&AsyncSpaceShip::AdjustHeading>,
        .ScanForLifeforms = SpaceShipBinder::BindMember<&AsyncSpaceShip::ScanForLifeforms>,
        .SetAstrometricsListener = SpaceShipBinder::BindMember<&SpaceShip::SetAstrometricsListener>,
        .SetDefenseCondition = SpaceShipBinder::BindMember<&SpaceShip::SetDefenseCondition>,
        .GetFuelRemaining = SpaceShipBinder::BindMember<&SpaceShip::GetFuelRemaining>,
        .AddFuelTank = SpaceShipBinder::BindMember<&SpaceShip::AddFuelTank>,
        .ScanForTensorLifeforms = SpaceShipBinder::BindMember<&SpaceShip::ScanForTensorLifeforms>,
        .ActivateShields = SpaceShipBinder::BindMember<&SpaceShip::ActivateShields>,
    };

    return SpaceShipBinder::BindOps<fidl_test_spaceship_SpaceShip_dispatch>(
        dispatcher, std::move(channel), this, &kOps);
  }

 private:
  thrd_t thrd_;
  fidl::AsyncTransaction async_txn_;
};

bool spaceship_async_test(void) {
  BEGIN_TEST;

  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "");

  async_loop_t* loop = NULL;
  ASSERT_EQ(ZX_OK, async_loop_create(&kAsyncLoopConfigNoAttachToCurrentThread, &loop), "");
  ASSERT_EQ(ZX_OK, async_loop_start_thread(loop, "spaceship-dispatcher", NULL), "");

  async_dispatcher_t* dispatcher = async_loop_get_dispatcher(loop);
  AsyncSpaceShip ship;
  ASSERT_EQ(ZX_OK, ship.Bind(dispatcher, std::move(server)));

  // Try invoking a member function which responds asynchronously and rebinds the connection.
  {
    const uint32_t stars[3] = {11u, 0u, UINT32_MAX};
    int8_t result = 0;
    ASSERT_EQ(ZX_OK, fidl_test_spaceship_SpaceShipAdjustHeading(client.get(), stars, 3, &result));
    ASSERT_EQ(-12, result, "");
    ship.Join();
  }

  // Try invoking a member function which responds asynchronously, but does not rebind the
  // connection. We should be able to observe that the server terminates the connection.
  {
    uint32_t lifesigns[64];
    size_t actual = 0;
    ASSERT_EQ(ZX_OK,
              fidl_test_spaceship_SpaceShipScanForLifeforms(client.get(), lifesigns, 64, &actual));
    ASSERT_EQ(2u, actual, "");
    ASSERT_EQ(42u, lifesigns[0], "");
    ASSERT_EQ(43u, lifesigns[1], "");

    zx_signals_t pending;
    zx::time deadline = zx::deadline_after(zx::sec(5));
    ASSERT_EQ(ZX_OK, client.wait_one(ZX_CHANNEL_PEER_CLOSED, deadline, &pending));
    ASSERT_EQ(pending & ZX_CHANNEL_PEER_CLOSED, ZX_CHANNEL_PEER_CLOSED);
    ship.Join();
  }

  ASSERT_EQ(ZX_OK, zx_handle_close(client.release()));

  async_loop_destroy(loop);

  END_TEST;
}

// These classes represents a compile-time check:
//
// We should be able to bind a derived class to its own methods,
// but also to methods of the base class.
//
// However, we should not be able to bind to an unrelated class.
class NotDerived {
 public:
  zx_status_t AdjustHeading(const uint32_t* stars_data, size_t stars_count, fidl_txn_t* txn) {
    return ZX_ERR_NOT_SUPPORTED;
  }
};

class Derived : public SpaceShip {
 public:
  using DerivedBinder = fidl::Binder<Derived>;

  zx_status_t ScanForLifeforms(fidl_txn_t* txn) override { return ZX_OK; }

  zx_status_t Bind(async_dispatcher_t* dispatcher, zx::channel channel) override {
    static constexpr fidl_test_spaceship_SpaceShip_ops_t kOps = {
    // (Under the failure case) Tries to bind to a member, such that the
    // context object passed to BindOps does not match the BindMember
    // callback. This should fail at compile time.
#if TEST_WILL_NOT_COMPILE || 0
      .AdjustHeading = DerivedBinder::BindMember<&NotDerived::AdjustHeading>,
#else
      .AdjustHeading = DerivedBinder::BindMember<&SpaceShip::AdjustHeading>,
#endif
      // Binds a member of the derived class to the derived method:
      // This is the typical use case of the Binder object.
      .ScanForLifeforms = DerivedBinder::BindMember<&Derived::ScanForLifeforms>,

      // Binds a member of the derived class to the base method:
      // The compile time check should allow this, because the "Derived"
      // class should be castable to a "SpaceShip" class.
      .SetAstrometricsListener = DerivedBinder::BindMember<&SpaceShip::SetAstrometricsListener>,

      // The remaining functions cover already tested behavior, but just
      // fill the ops table.
      .SetDefenseCondition = DerivedBinder::BindMember<&SpaceShip::SetDefenseCondition>,
      .GetFuelRemaining = DerivedBinder::BindMember<&SpaceShip::GetFuelRemaining>,
      .AddFuelTank = DerivedBinder::BindMember<&SpaceShip::AddFuelTank>,
      .ScanForTensorLifeforms = DerivedBinder::BindMember<&Derived::ScanForTensorLifeforms>,
      .ActivateShields = SpaceShipBinder::BindMember<&Derived::ActivateShields>,
    };

    return SpaceShipBinder::BindOps<fidl_test_spaceship_SpaceShip_dispatch>(
        dispatcher, std::move(channel), this, &kOps);
  }
};

}  // namespace

BEGIN_TEST_CASE(spaceship_tests_cpp)
RUN_NAMED_TEST("fidl.test.spaceship.SpaceShip test", spaceship_test)
RUN_NAMED_TEST("fidl.test.spaceship.SpaceShip async test", spaceship_async_test)
END_TEST_CASE(spaceship_tests_cpp)
