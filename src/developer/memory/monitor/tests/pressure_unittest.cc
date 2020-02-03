// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/pressure.h"

#include <lib/gtest/test_loop_fixture.h>
#include <zircon/types.h>

#include <array>

#include <gtest/gtest.h>

namespace monitor {
namespace test {

class PressureUnitTest : public gtest::TestLoopFixture {
 public:
  PressureUnitTest() : pressure_(false) {}

 protected:
  void RetrieveAndVerifyEvents() {
    ASSERT_EQ(pressure_.InitMemPressureEvents(), ZX_OK);

    std::array<zx_koid_t, Level::kNumLevels> koids;
    for (size_t i = 0; i < Level::kNumLevels; i++) {
      // Events are valid.
      ASSERT_TRUE(pressure_.events_[i].is_valid());

      zx_info_handle_basic_t info;
      ASSERT_EQ(zx_object_get_info(pressure_.events_[i].get(), ZX_INFO_HANDLE_BASIC, &info,
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
    ASSERT_EQ(pressure_.InitMemPressureEvents(), ZX_OK);
    // The first wait returns immediately, signaling the current pressure level.
    pressure_.WaitOnLevelChange();

    // Memory pressure level is valid.
    ASSERT_LT(pressure_.level_, Level::kNumLevels);
  }

  void VerifyEventsWaitedOn() {
    ASSERT_EQ(pressure_.InitMemPressureEvents(), ZX_OK);
    pressure_.WaitOnLevelChange();

    // Events we will wait on exclude the one currently asserted.
    for (size_t i = 0; i < Level::kNumLevels - 1; i++) {
      ASSERT_NE(pressure_.wait_items_[i].handle, pressure_.events_[pressure_.level_].get());
    }
  }

 private:
  Pressure pressure_;
};

TEST_F(PressureUnitTest, Events) { RetrieveAndVerifyEvents(); }

TEST_F(PressureUnitTest, InitialLevel) { VerifyInitialLevel(); }

TEST_F(PressureUnitTest, WaitOnEvents) { VerifyEventsWaitedOn(); }

}  // namespace test
}  // namespace monitor
