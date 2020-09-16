// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/ConnectivityManager.h>
#include <Weave/DeviceLayer/internal/ServiceTunnelAgent.h>
#include <Weave/Profiles/WeaveProfiles.h>
// clang-format on

#include "src/connectivity/weave/adaptation/connectivity_manager_impl.h"
#include "src/connectivity/weave/adaptation/connectivity_manager_delegate_impl.h"
#include "weave_test_fixture.h"

namespace nl::Weave::DeviceLayer::Internal {
namespace testing {
namespace {

using nl::Weave::DeviceLayer::ConnectivityManager;
using nl::Weave::DeviceLayer::ConnectivityManagerImpl;
using nl::Weave::Profiles::ServiceDirectory::WeaveServiceManager;
using nl::Weave::Profiles::WeaveTunnel::WeaveTunnelAgent;

}  // namespace

class TestConnectivityManagerDelegateImpl : public ConnectivityManagerDelegateImpl {
 public:
  WEAVE_ERROR InitServiceTunnelAgent() override { return WEAVE_NO_ERROR; }
};

class ConnectivityManagerTest : public WeaveTestFixture {
 public:
  void SetUp() {
    WeaveTestFixture::SetUp();
    WeaveTestFixture::RunFixtureLoop();
    ConnectivityMgrImpl().SetDelegate(std::make_unique<TestConnectivityManagerDelegateImpl>());
    EXPECT_EQ(ConnectivityMgrImpl().GetDelegate()->Init(), WEAVE_NO_ERROR);
  }

  void TearDown() {
    WeaveTestFixture::StopFixtureLoop();
    WeaveTestFixture::TearDown();
  }
};

TEST_F(ConnectivityManagerTest, Init) {
  EXPECT_FALSE(ConnectivityMgr().IsServiceTunnelConnected());
  EXPECT_FALSE(ConnectivityMgr().HaveIPv4InternetConnectivity());
  EXPECT_FALSE(ConnectivityMgr().HaveIPv6InternetConnectivity());
  EXPECT_EQ(ConnectivityMgr().GetServiceTunnelMode(),
            ConnectivityManager::kServiceTunnelMode_Enabled);
}

}  // namespace testing
}  // namespace nl::Weave::DeviceLayer::Internal
