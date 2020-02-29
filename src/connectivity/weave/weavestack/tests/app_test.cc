// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/weave/weavestack/app.h"
#include <lib/async/cpp/time.h>
#include <gtest/gtest.h>

namespace weavestack {

TEST(App, CanInstantiateApp) {
  App app;
  EXPECT_EQ(WEAVE_NO_ERROR, app.Init());
  EXPECT_EQ(WEAVE_NO_ERROR, app.Start());
  app.Quit();
}
}  // namespace weavestack
