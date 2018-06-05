// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "echo2_server_app.h"
#include "lib/gtest/test_with_loop.h"

namespace echo2 {
namespace testing {

class EchoServerAppTest : public ::gtest::TestWithLoop {
 protected:
  EchoServerApp echoServerApp_;
};

TEST_F(EchoServerAppTest, HelloWorld) {
  ::fidl::StringPtr message = "bogus";
  echoServerApp_.EchoString(
      "Hello World!", [&](::fidl::StringPtr retval) { message = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ("Hello World!", message);
}

TEST_F(EchoServerAppTest, Empty) {
  ::fidl::StringPtr message = "bogus";
  echoServerApp_.EchoString(
      "", [&](::fidl::StringPtr retval) { message = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ("", message);
}

}  // namespace testing
}  // namespace echo2
