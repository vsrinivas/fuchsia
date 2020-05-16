// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/pressure_observer.h"

#include <lib/gtest/test_loop_fixture.h>
#include <zircon/types.h>

#include <array>

#include <gtest/gtest.h>

namespace monitor {
namespace test {

class PressureObserverUnitTest : public gtest::TestLoopFixture {
 public:
  PressureObserverUnitTest() : observer_(false) {}

 protected:
  void RetrieveAndVerifyEvents() {
    std::array<zx_koid_t, Level::kNumLevels> koids;
    for (size_t i = 0; i < Level::kNumLevels; i++) {
      // Events are valid.
      ASSERT_TRUE(observer_.events_[i].is_valid());

      zx_info_handle_basic_t info;
      ASSERT_EQ(zx_object_get_info(observer_.events_[i].get(), ZX_INFO_HANDLE_BASIC, &info,
                                   sizeof(info), nullptr, nullptr),
                ZX_OK);
      koids[i] = info.koid;
      EXPECT_EQ(info.type, ZX_OBJ_TYPE_EVENT);
      EXPECT_EQ(info.rights, ZX_DEFAULT_SYSTEM_EVENT_LOW_MEMORY_RIGHTS);
    }

    // Events are distinct.
    for (size_t i = 0; i < Level::kNumLevels; i++) {
      for (size_t j = 0; j < i; j++) {
        ASSERT_NE(koids[i], koids[j]);
      }
    }
  }

  void VerifyInitialLevel() {
    // The first wait returns immediately, signaling the current pressure level.
    observer_.WaitOnLevelChange();

    // Memory pressure level is valid.
    ASSERT_LT(observer_.level_, Level::kNumLevels);
  }

  void VerifyEventsWaitedOn() {
    observer_.WaitOnLevelChange();

    // Events we will wait on exclude the one currently asserted.
    for (size_t i = 0; i < Level::kNumLevels - 1; i++) {
      ASSERT_NE(observer_.wait_items_[i].handle, observer_.events_[observer_.level_].get());
    }
  }

 private:
  PressureObserver observer_;
};

TEST_F(PressureObserverUnitTest, Events) { RetrieveAndVerifyEvents(); }

TEST_F(PressureObserverUnitTest, InitialLevel) { VerifyInitialLevel(); }

TEST_F(PressureObserverUnitTest, WaitOnEvents) { VerifyEventsWaitedOn(); }

}  // namespace test
}  // namespace monitor
