// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <zxtest/zxtest.h>

#include "hello_world_util.h"

namespace {

class HelloWorldCTS : public zxtest::Test {
 public:
  ~HelloWorldCTS() override = default;
  void SetUp() override {}
};

TEST_F(HelloWorldCTS, HelloCTS) {
  ASSERT_EQ(HelloWorldUtil::get_hello_world(), "Hello, World!");
}

}  // namespace
