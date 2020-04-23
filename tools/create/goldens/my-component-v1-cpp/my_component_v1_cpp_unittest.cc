// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/create/goldens/my-component-v1-cpp/my_component_v1_cpp.h"

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

// |gtest::RealLoopFixture| creates an async loop and provides us with some utilities such as
// RunLoopUntil().
class MyComponentV1CppTest : public gtest::RealLoopFixture {
 public:
  // Set up for each TEST_F() here:
  MyComponentV1CppTest() {}
  // Clean up for each TEST_F() here:
  ~MyComponentV1CppTest() {}
};

TEST_F(MyComponentV1CppTest, SmokeTest) { my_component_v1_cpp::App app(dispatcher()); }
