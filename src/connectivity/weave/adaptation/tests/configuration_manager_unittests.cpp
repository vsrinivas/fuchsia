// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/ConfigurationManager.h>
#include <Weave/Core/WeaveKeyIds.h>
#include "configuration_manager_impl.h"
#include "fuchsia_config.h"
#include "group_key_store_impl.h"
#include <Weave/Profiles/security/WeaveApplicationKeys.h>

#include <Weave/Core/WeaveVendorIdentifiers.hpp>
#include <Weave/DeviceLayer/internal/GenericConfigurationManagerImpl.ipp>
// clang-format on

#include <lib/gtest/test_loop_fixture.h>
#include "gtest/gtest.h"

namespace adaptation {
namespace testing {
namespace {
using nl::Weave::DeviceLayer::ConfigurationMgr;
}  // namespace

class ConfigurationManagerTest : public ::gtest::TestLoopFixture {
 public:
  ConfigurationManagerTest() {}
  void SetUp() override { TestLoopFixture::SetUp(); }
  void TearDown() override {
    ConfigurationMgr().InitiateFactoryReset();
    TestLoopFixture::TearDown();
  }
};

TEST_F(ConfigurationManagerTest, SetAndGetFabricId) {
  const uint64_t fabric_id = 123456789U;
  uint64_t stored_fabric_id = 0U;
  EXPECT_EQ(ConfigurationMgr().StoreFabricId(fabric_id), WEAVE_NO_ERROR);
  EXPECT_EQ(ConfigurationMgr().GetFabricId(stored_fabric_id), WEAVE_NO_ERROR);
  EXPECT_EQ(stored_fabric_id, fabric_id);
}

}  // namespace testing
}  // namespace adaptation
