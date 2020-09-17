// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/connectivity/weave/weavestack/app.h"

#include <lib/async/cpp/time.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/syslog/cpp/macros.h>

#include <Weave/DeviceLayer/PlatformManager.h>
#include <gtest/gtest.h>

namespace weavestack {
namespace {
using nl::Weave::DeviceLayer::PlatformMgr;
using nl::Weave::DeviceLayer::PlatformMgrImpl;
}  // namespace

class AppTest : public ::gtest::RealLoopFixture {
 public:
  void SetUp() {
    RealLoopFixture::SetUp();
    PlatformMgrImpl().SetDispatcher(dispatcher());
    PlatformMgr().InitWeaveStack();
  }

  void TearDown() {
    PlatformMgrImpl().ShutdownWeaveStack();
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
  EXPECT_EQ(ZX_OK, app.Init());
  EXPECT_EQ(ZX_ERR_TIMED_OUT,
            app.Run(async::Now(app.loop()->dispatcher()) + zx::duration(ZX_SEC(1)), false));
  app.Quit();
}

TEST(App, CallInitAgain) {
  auto app = App();
  EXPECT_EQ(ZX_OK, app.Init());
  EXPECT_EQ(ZX_ERR_BAD_STATE, app.Init());
  app.Quit();
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

}  // namespace weavestack
