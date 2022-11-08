// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/connectivity/weave/weavestack/app.h"

#include <lib/async/cpp/time.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

// clang-format off
#pragma GCC diagnostic push
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/ConnectivityManager.h>
#include <Weave/DeviceLayer/PlatformManager.h>
#include <Weave/DeviceLayer/ThreadStackManager.h>
#pragma GCC diagnostic pop

#include "src/connectivity/weave/adaptation/generic_platform_manager_impl_fuchsia.ipp"
#include "src/connectivity/weave/adaptation/configuration_manager_delegate_impl.h"
#include "src/connectivity/weave/adaptation/connectivity_manager_delegate_impl.h"
#include "src/connectivity/weave/adaptation/network_provisioning_server_delegate_impl.h"
#include "src/connectivity/weave/adaptation/thread_stack_manager_stub_impl.h"
#include "src/connectivity/weave/lib/core/trait_updater_delegate_impl.h"
// clang-format on

#include <gtest/gtest.h>

namespace weavestack {
namespace testing {
namespace {
using nl::Weave::DeviceLayer::ConfigurationManagerDelegateImpl;
using nl::Weave::DeviceLayer::ConfigurationMgrImpl;
using nl::Weave::DeviceLayer::ConnectivityManager;
using nl::Weave::DeviceLayer::ConnectivityManagerImpl;
using nl::Weave::DeviceLayer::ConnectivityMgrImpl;
using nl::Weave::DeviceLayer::PlatformMgr;
using nl::Weave::DeviceLayer::PlatformMgrImpl;
using nl::Weave::DeviceLayer::ThreadStackManagerStubImpl;
using nl::Weave::DeviceLayer::ThreadStackMgrImpl;
using nl::Weave::DeviceLayer::TraitUpdater;
using nl::Weave::DeviceLayer::TraitUpdaterDelegateImpl;
using nl::Weave::DeviceLayer::WeaveDeviceEvent;
using nl::Weave::DeviceLayer::WeaveDevicePlatformEventType;
using nl::Weave::DeviceLayer::Internal::NetworkProvisioningServerDelegateImpl;
using nl::Weave::DeviceLayer::Internal::NetworkProvisioningServerImpl;
using nl::Weave::DeviceLayer::Internal::NetworkProvisioningSvrImpl;

class ConnectivityManagerTestDelegate : public ConnectivityManagerImpl::Delegate {
 public:
  ~ConnectivityManagerTestDelegate() {}

  WEAVE_ERROR Init() { return WEAVE_NO_ERROR; }
  bool IsServiceTunnelConnected() { return false; }
  bool IsServiceTunnelRestricted() { return false; }
  void OnPlatformEvent(const WeaveDeviceEvent* event) {}
  std::optional<std::string> GetWiFiInterfaceName() { return std::nullopt; }
  ConnectivityManager::ThreadMode GetThreadMode() {
    return ConnectivityManager::kThreadMode_NotSupported;
  }
  fuchsia::net::interfaces::admin::ControlSyncPtr* GetTunInterfaceControlSyncPtr() {
    return nullptr;
  }
};

class TestTraitUpdaterDelegate : public TraitUpdaterDelegateImpl {
 public:
  WEAVE_ERROR Init() override { return WEAVE_NO_ERROR; }
  void HandleWeaveDeviceEvent(const WeaveDeviceEvent* event) override {}
};

void SetDefaultDelegates() {
  ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerDelegateImpl>());
  NetworkProvisioningSvrImpl().SetDelegate(
      std::make_unique<NetworkProvisioningServerDelegateImpl>());
  // The default delegate for the ConnectivityManager is replaced with a test
  // delegate that does not initialize. This is to prevent the failure of
  // binding to the net interface from immediately shutting down the stack.
  ConnectivityMgrImpl().SetDelegate(std::make_unique<ConnectivityManagerTestDelegate>());
  // Similarly, the ThreadStackManager delegate is replaced with a delegate that
  // does not initialize.
  ThreadStackMgrImpl().SetDelegate(std::make_unique<ThreadStackManagerStubImpl>());
  // TraitUpdaterImpl delegate is replaced with a dummy delegate.
  TraitUpdater().SetDelegate(std::make_unique<TestTraitUpdaterDelegate>());
}

void ClearDelegates() {
  ConfigurationMgrImpl().SetDelegate(nullptr);
  ConnectivityMgrImpl().SetDelegate(nullptr);
  NetworkProvisioningSvrImpl().SetDelegate(nullptr);
  ThreadStackMgrImpl().SetDelegate(nullptr);
  TraitUpdater().SetDelegate(nullptr);
}

}  // namespace

class AppTest : public ::gtest::RealLoopFixture {
 public:
  void SetUp() {
    RealLoopFixture::SetUp();
    SetDefaultDelegates();
    PlatformMgrImpl().SetDispatcher(dispatcher());
    PlatformMgr().InitWeaveStack();
  }

  void TearDown() {
    PlatformMgrImpl().ShutdownWeaveStack();
    ClearDelegates();
    RealLoopFixture::TearDown();
  }

  void PrepareSelect() {
    struct timeval t;
    memset(&t, 0, sizeof(t));
    ClearFds();
    PlatformMgrImpl().GetSystemLayer().PrepareSelect(fds.num_fds, &fds.read_fds, &fds.write_fds,
                                                     &fds.except_fds, t);
  }

  int GetSelectResult() {
    struct timeval t;
    memset(&t, 0, sizeof(t));
    int ret = select(fds.num_fds, &fds.read_fds, &fds.write_fds, &fds.except_fds, &t);
    return ret;
  }

  void ClearFds() {
    FD_ZERO(&fds.read_fds);
    FD_ZERO(&fds.write_fds);
    FD_ZERO(&fds.except_fds);
    fds.num_fds = 0;
  }
  static void EmptyWorkFunc(intptr_t arg) {}

 protected:
  struct {
    fd_set read_fds;
    fd_set write_fds;
    fd_set except_fds;
    int num_fds;
  } fds;
};

TEST(App, CanRunApp) {
  auto app = App();
  auto defer_shutdown = fit::defer([&] {
    app.Quit();
    ClearDelegates();
  });

  SetDefaultDelegates();
  EXPECT_EQ(ZX_OK, app.Init());
  EXPECT_EQ(ZX_ERR_TIMED_OUT,
            app.Run(async::Now(app.loop()->dispatcher()) + zx::duration(ZX_SEC(1)), false));
}

TEST(App, CallInitTwice) {
  auto app = App();
  auto defer_shutdown = fit::defer([&] {
    app.Quit();
    ClearDelegates();
  });

  SetDefaultDelegates();
  EXPECT_EQ(ZX_OK, app.Init());
  EXPECT_EQ(ZX_ERR_BAD_STATE, app.Init());
}

TEST(App, CallQuitTwice) {
  auto app = App();
  auto defer_shutdown = fit::defer([&] {
    app.Quit();
    ClearDelegates();
  });

  SetDefaultDelegates();
  EXPECT_EQ(ZX_OK, app.Init());
  app.Quit();
  app.Quit();
}

TEST(App, RequestShutdown) {
  auto app = App();
  auto defer_shutdown = fit::defer([&] {
    app.Quit();
    ClearDelegates();
  });

  SetDefaultDelegates();
  EXPECT_EQ(ZX_OK, app.Init());
  EXPECT_EQ(ASYNC_LOOP_RUNNABLE, app.loop()->GetState());

  const WeaveDeviceEvent shutdown_request = {
      .Type = WeaveDevicePlatformEventType::kShutdownRequest,
  };
  PlatformMgrImpl().PostEvent(&shutdown_request);
  EXPECT_EQ(ZX_ERR_CANCELED, app.Run());

  EXPECT_EQ(ASYNC_LOOP_QUIT, app.loop()->GetState());
}

TEST_F(AppTest, WakeSelectTest) {
  // Get and clear existing fds;
  PrepareSelect();
  int ret = GetSelectResult();
  PlatformMgrImpl().GetSystemLayer().HandleSelectResult(ret, &fds.read_fds, &fds.write_fds,
                                                        &fds.except_fds);
  PrepareSelect();
  // select will timeout when no fds are set and returns 0.
  EXPECT_EQ(0, GetSelectResult());

  PrepareSelect();
  PlatformMgr().ScheduleWork(EmptyWorkFunc, (intptr_t)0);
  // GetSelectResult should return 1 as only the WakeSelect fd should be set.
  EXPECT_EQ(1, GetSelectResult());
}

}  // namespace testing
}  // namespace weavestack
