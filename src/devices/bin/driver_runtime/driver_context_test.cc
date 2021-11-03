// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_runtime/driver_context.h"

#include <thread>

#include <zxtest/zxtest.h>

namespace driver_context {

class DriverContextTest : public zxtest::Test {
 protected:
  // Returns a fake driver pointer that can be used with driver_context APIs.
  // Do not try to access the internals of the pointer.
  const void* CreateFakeDriver() {
    // We don't actually need a real pointer.
    int driver = next_driver_;
    next_driver_++;
    return reinterpret_cast<const void*>(driver);
  }

  std::vector<const void*> CreateFakeDrivers(size_t num_drivers) {
    std::vector<const void*> drivers;
    for (size_t i = 0; i < num_drivers; i++) {
      drivers.push_back(CreateFakeDriver());
    }
    return drivers;
  }

 private:
  int next_driver_ = 0xDEADBEEF;
};

TEST_F(DriverContextTest, PushPopStack) {
  constexpr size_t kNumDrivers = 100;
  std::vector<const void*> drivers = CreateFakeDrivers(kNumDrivers);

  for (const auto& driver : drivers) {
    PushDriver(driver);
    EXPECT_EQ(GetCurrentDriver(), driver);
  }

  for (size_t i = 0; i < kNumDrivers; i++) {
    size_t pop_driver_idx = kNumDrivers - i - 1;
    PopDriver();
    const void* expect_driver = pop_driver_idx == 0 ? nullptr : drivers[pop_driver_idx - 1];
    EXPECT_EQ(GetCurrentDriver(), expect_driver);
    EXPECT_FALSE(IsDriverInCallStack(drivers[pop_driver_idx]));

    for (size_t j = 0; j < pop_driver_idx; j++) {
      EXPECT_TRUE(IsDriverInCallStack(drivers[j]));
    }
  }
}

TEST_F(DriverContextTest, PopEmptyStack) {
  ASSERT_DEATH([] { PopDriver(); });
}

TEST_F(DriverContextTest, CallStackPerThread) {
  const void* driverA = CreateFakeDriver();
  const void* driverB = CreateFakeDriver();

  PushDriver(driverA);

  std::thread t = std::thread([&] {
    EXPECT_NULL(GetCurrentDriver());

    PushDriver(driverB);
    EXPECT_EQ(GetCurrentDriver(), driverB);
    EXPECT_TRUE(IsDriverInCallStack(driverB));
    EXPECT_FALSE(IsDriverInCallStack(driverA));
  });

  t.join();

  EXPECT_EQ(GetCurrentDriver(), driverA);
  EXPECT_TRUE(IsDriverInCallStack(driverA));
  EXPECT_FALSE(IsDriverInCallStack(driverB));

  PopDriver();
}

}  // namespace driver_context
