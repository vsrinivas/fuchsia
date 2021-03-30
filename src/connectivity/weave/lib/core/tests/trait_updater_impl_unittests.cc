// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/weave/lib/core/trait_updater.h"
#include "src/connectivity/weave/lib/core/trait_updater_delegate_impl.h"

#include "src/connectivity/weave/adaptation/configuration_manager_delegate_impl.h"
#include "src/connectivity/weave/adaptation/tests/weave_test_fixture.h"

namespace nl::Weave::DeviceLayer::Internal::testing {
using nl::Weave::DeviceLayer::TraitUpdaterImpl;
using nl::Weave::DeviceLayer::WeaveDeviceEvent;
using weavestack::applets::Applet;

std::vector<std::string> test_list = {"test_applets.so"};
std::vector<std::string> empty_list;

class ConfigurationManagerTestDelegateImpl : public ConfigurationManagerDelegateImpl {
 public:
    bool IsMemberOfFabric() override {
      return member_of_fabric;
    }
    void SetMemberOfFabric(bool value) {
        member_of_fabric = value;
    }
 protected:
    bool member_of_fabric = false;
};

class TraitUpdaterImplTest : public WeaveTestFixture<> {
 public:
  void SetUp() {
    WeaveTestFixture<>::SetUp();
    WeaveTestFixture<>::RunFixtureLoop();
    ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerTestDelegateImpl>());
    TraitUpdater().SetDelegate(std::make_unique<TraitUpdaterDelegateImpl>());
  }

  void TearDown() {
    WeaveTestFixture<>::StopFixtureLoop();
    WeaveTestFixture<>::TearDown();
    ConfigurationMgrImpl().SetDelegate(nullptr);
    TraitUpdater().SetDelegate(nullptr);
  }
};

TEST_F(TraitUpdaterImplTest, InitNoApplets) {
  auto delegate = (TraitUpdaterDelegateImpl*)TraitUpdater().GetDelegate();
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, delegate->InitApplets(empty_list));
}

TEST_F(TraitUpdaterImplTest, Init) {
  auto delegate = (TraitUpdaterDelegateImpl*)TraitUpdater().GetDelegate();
  EXPECT_EQ(ZX_OK, delegate->InitApplets(test_list));
}

TEST_F(TraitUpdaterImplTest, OnPlatformEvent) {
  constexpr char kTestString[] = "DEADBEEF";
  auto trait_updater_delegate = (TraitUpdaterDelegateImpl*)TraitUpdater().GetDelegate();
  EXPECT_EQ(ZX_OK, trait_updater_delegate->InitApplets(test_list));
  WeaveDeviceEvent fabric_event{.Type = DeviceEventType::kFabricMembershipChange};
  auto delegate = ConfigurationMgrImpl().GetDelegate();
  reinterpret_cast<ConfigurationManagerTestDelegateImpl*>(delegate)->SetMemberOfFabric(true);
  trait_updater_delegate->HandleWeaveDeviceEvent(&fabric_event);
  EXPECT_STREQ((char*)fabric_event.Platform.arg, kTestString);
}

}  // nl::Weave::DeviceLayer
