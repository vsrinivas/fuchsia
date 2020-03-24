// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/connectivity/weave/weavestack/app.h"

#include <lib/async/cpp/time.h>
#include <lib/syslog/cpp/logger.h>

#include <gtest/gtest.h>

namespace weavestack {

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

}  // namespace weavestack
