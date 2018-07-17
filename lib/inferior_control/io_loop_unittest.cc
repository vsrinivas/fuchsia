// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/inferior_control/io_loop.h"
#include "gtest/gtest.h"

namespace inferior_control {
namespace {

class TestDelegate : public IOLoop::Delegate {
 public:
  void OnBytesRead(const fxl::StringView& bytes_read) override {}
  void OnDisconnected() override {}
  void OnIOError() override {}
};

class IOLoopTest : public IOLoop, public ::testing::Test {
 public:
  static constexpr int kInitialFd = -1;

  IOLoopTest()
      : IOLoop(kInitialFd, &delegate_, &loop_),
        loop_(&kAsyncLoopConfigNoAttachToThread) {}

  void SetUp() override {}
  void TearDown() override {}

 private:
  void OnReadTask() override {}

  TestDelegate delegate_;
  async::Loop loop_;
};

TEST_F(IOLoopTest, Quit) {
  // Prefix Run() with IOLoop:: since testing::Test has one too.
  IOLoop::Run();
  Quit();
  EXPECT_TRUE(quit_called());

  // The real test here is that this doesn't hang.
}

}  // namespace
}  // namespace inferior_control
