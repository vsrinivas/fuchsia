// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/real_loop_fixture.h>

#include "sync_manager.h"

#define WAIT_FOR_OK(ok) ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&ok]() { return ok; }, zx::sec(2)))
#define WAIT_FOR_OK_AND_RESET(ok) \
  WAIT_FOR_OK(ok);                \
  ok = false

namespace netemul {
namespace testing {

static const char* kMainTestBarrier = "test-barrier";
static const char* kAltTestBarrier = "alt-barrier";

class BarrierTest : public gtest::RealLoopFixture {
 public:
  using SyncManagerPtr = fidl::InterfacePtr<SyncManager::FSyncManager>;

  BarrierTest() : svc_(std::make_unique<SyncManager>()) {}

 protected:
  void GetSyncManager(fidl::InterfaceRequest<SyncManager::FSyncManager> manager) {
    svc_->GetHandler()(std::move(manager));
  }

  std::unique_ptr<SyncManager> svc_;
};

TEST_F(BarrierTest, FulfillBarrier) {
  SyncManagerPtr sm;
  GetSyncManager(sm.NewRequest());
  bool got_callback = false;
  // waiting on a count of 1 will always fullfill immediately:
  sm->WaitForBarrierThreshold(kMainTestBarrier, 1, zx::msec(0).to_nsecs(),
                              [&got_callback](bool result) {
                                EXPECT_TRUE(result);
                                got_callback = true;
                              });
  WAIT_FOR_OK_AND_RESET(got_callback);
}

TEST_F(BarrierTest, TimeoutBarrier) {
  SyncManagerPtr sm;
  GetSyncManager(sm.NewRequest());
  bool got_callback = false;
  // wait and timeout:
  sm->WaitForBarrierThreshold(kMainTestBarrier, 2, zx::msec(15).to_nsecs(),
                              [&got_callback](bool result) {
                                EXPECT_FALSE(result);
                                got_callback = true;
                              });
  WAIT_FOR_OK_AND_RESET(got_callback);
}

TEST_F(BarrierTest, DestroyWithPending) {
  SyncManagerPtr sm;
  GetSyncManager(sm.NewRequest());
  // wait and timeout:
  sm->WaitForBarrierThreshold(kMainTestBarrier, 2, zx::msec(0).to_nsecs(),
                              [](bool result) { FAIL() << "Shouldn't call callback"; });
}

TEST_F(BarrierTest, ManyWaits) {
  SyncManagerPtr sm;
  GetSyncManager(sm.NewRequest());
  bool got_callback1 = false;
  bool got_callback2 = false;
  bool got_callback3 = false;
  bool got_callback4 = false;

  sm->WaitForBarrierThreshold(kMainTestBarrier, 4, zx::msec(0).to_nsecs(),
                              [&got_callback1](bool result) {
                                EXPECT_TRUE(result);
                                got_callback1 = true;
                              });
  sm->WaitForBarrierThreshold(kMainTestBarrier, 4, zx::msec(0).to_nsecs(),
                              [&got_callback2](bool result) {
                                EXPECT_TRUE(result);
                                got_callback2 = true;
                              });
  sm->WaitForBarrierThreshold(kMainTestBarrier, 4, zx::msec(0).to_nsecs(),
                              [&got_callback3](bool result) {
                                EXPECT_TRUE(result);
                                got_callback3 = true;
                              });

  // Wait for 5. Will trigger the others, but will timeout on itself.
  sm->WaitForBarrierThreshold(kMainTestBarrier, 5, zx::msec(15).to_nsecs(),
                              [&got_callback4](bool result) {
                                EXPECT_FALSE(result);
                                got_callback4 = true;
                              });

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&]() { return got_callback1 && got_callback2 && got_callback3 && got_callback4; },
      zx::sec(2)));
}

TEST_F(BarrierTest, DifferentBarriers) {
  SyncManagerPtr sm;
  GetSyncManager(sm.NewRequest());
  bool got_callback1 = false;
  bool got_callback2 = false;
  bool got_callback3 = false;

  sm->WaitForBarrierThreshold(kMainTestBarrier, 2, zx::msec(0).to_nsecs(),
                              [&got_callback1](bool result) {
                                EXPECT_TRUE(result);
                                got_callback1 = true;
                              });
  sm->WaitForBarrierThreshold(kMainTestBarrier, 2, zx::msec(0).to_nsecs(),
                              [&got_callback2](bool result) {
                                EXPECT_TRUE(result);
                                got_callback2 = true;
                              });
  // wait on different barrier. Should timeout.
  sm->WaitForBarrierThreshold(kAltTestBarrier, 2, zx::msec(10).to_nsecs(),
                              [&got_callback3](bool result) {
                                EXPECT_FALSE(result);
                                got_callback3 = true;
                              });

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&]() { return got_callback1 && got_callback2 && got_callback3; }, zx::sec(2)));
}

}  // namespace testing
}  // namespace netemul
