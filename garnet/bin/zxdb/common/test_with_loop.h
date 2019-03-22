// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "gtest/gtest.h"
#include "src/developer/debug/shared/platform_message_loop.h"

namespace zxdb {

// This class is to be used as a base class for tests requiring a MessageLoop
// be set up to run them. Derive from it and then use that:
//
//   namespace {
//   MyTest : public TestWithLoop {};
//   }  // namespace
//
//   TEST_F(MyTest, Foo) {
//     loop().Run();
//     ...
//   }
class TestWithLoop : public testing::Test {
 public:
  TestWithLoop() { loop_.Init(); }
  ~TestWithLoop() { loop_.Cleanup(); }

  debug_ipc::MessageLoop& loop() { return loop_; }

 private:
  debug_ipc::PlatformMessageLoop loop_;
};

}  // namespace zxdb
