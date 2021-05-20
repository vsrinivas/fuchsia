// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/test_loop_fixture.h>

#include "src/connectivity/weave/lib/core/trait_updater.h"
#include "src/connectivity/weave/lib/core/trait_updater_delegate_impl.h"

namespace nl::Weave::DeviceLayer::Internal::testing {
namespace {
using nl::Weave::DeviceLayer::WeaveDeviceEvent;

std::vector<std::string> test_list = {"test_applets.so"};
std::vector<std::string> empty_list;
}  // namespace

class TraitUpdaterImplTest : public ::gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TraitUpdater().SetDelegate(std::make_unique<TraitUpdaterDelegateImpl>());
  }

  void TearDown() override { TraitUpdater().SetDelegate(nullptr); }
};

TEST_F(TraitUpdaterImplTest, InitNoApplets) {
  auto delegate = reinterpret_cast<TraitUpdaterDelegateImpl*>(TraitUpdater().GetDelegate());
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, delegate->InitApplets(empty_list));
}

TEST_F(TraitUpdaterImplTest, Init) {
  auto delegate = reinterpret_cast<TraitUpdaterDelegateImpl*>(TraitUpdater().GetDelegate());
  EXPECT_EQ(ZX_OK, delegate->InitApplets(test_list));
}

TEST_F(TraitUpdaterImplTest, OnPlatformEvent) {
  constexpr char kTestString[] = "DEADBEEF";
  constexpr WeaveDeviceEvent test_event = {};

  auto delegate = reinterpret_cast<TraitUpdaterDelegateImpl*>(TraitUpdater().GetDelegate());
  EXPECT_EQ(ZX_OK, delegate->InitApplets(test_list));

  delegate->HandleWeaveDeviceEvent(&test_event);
  EXPECT_STREQ((char*)test_event.Platform.arg, kTestString);
}

}  // namespace nl::Weave::DeviceLayer::Internal::testing
