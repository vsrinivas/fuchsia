// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_TEST_WITH_LOOP_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_TEST_WITH_LOOP_H_

#include "gtest/gtest.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/lib/fxl/logging.h"

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
  TestWithLoop() {
    std::string error_message;
    bool success = loop_.Init(&error_message);
    FXL_CHECK(success) << error_message;
  }
  ~TestWithLoop() { loop_.Cleanup(); }

  debug_ipc::MessageLoop& loop() { return loop_; }

 private:
  debug_ipc::PlatformMessageLoop loop_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_TEST_WITH_LOOP_H_
