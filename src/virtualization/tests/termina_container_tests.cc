// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <string.h>

#include "src/virtualization/tests/lib/guest_test.h"

constexpr auto kExpectedContainerName = "penguin";

template <class T>
class TerminaIntegrationTest : public GuestTest<T> {};

using GuestTypes = ::testing::Types<TerminaContainerEnclosedGuest>;

TYPED_TEST_SUITE(TerminaIntegrationTest, GuestTypes, GuestTestNameGenerator);

TYPED_TEST(TerminaIntegrationTest, ContainerStartup) {
  std::string result;
  int32_t return_code;
  this->Execute({"uname", "-a"}, &result, &return_code);

  // uname should contain the container name within the container.
  EXPECT_EQ(0, return_code);
  EXPECT_NE(result.find(kExpectedContainerName), std::string::npos);
}
