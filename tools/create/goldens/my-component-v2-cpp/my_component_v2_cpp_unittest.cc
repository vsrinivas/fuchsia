// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/create/goldens/my-component-v2-cpp/my_component_v2_cpp.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <gtest/gtest.h>

TEST(MyComponentV2CppTest, Smoke) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  my_component_v2_cpp::App app(loop.dispatcher());
  loop.RunUntilIdle();
}
