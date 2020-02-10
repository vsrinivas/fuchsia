// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/weave/weavestack/app.h"

#include <gtest/gtest.h>

namespace weavestack {

TEST(App, CanInstantiateApp) { EXPECT_EQ(ZX_OK, App().loop()->RunUntilIdle()); }

}  // namespace weavestack
