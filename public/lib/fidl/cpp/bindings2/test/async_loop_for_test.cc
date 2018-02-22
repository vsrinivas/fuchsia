// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings2/test/async_loop_for_test.h"

#include <async/cpp/loop.h>
#include "lib/fidl/cpp/test/loop_config.h"

namespace fidl {
namespace test {

class AsyncLoopForTestImpl {
 public:
  AsyncLoopForTestImpl() : loop_(&fidl::kTestLoopConfig) {}
  ~AsyncLoopForTestImpl() = default;

  async::Loop* loop() { return &loop_; }

 private:
  async::Loop loop_;
};

AsyncLoopForTest::AsyncLoopForTest()
    : impl_(std::make_unique<AsyncLoopForTestImpl>()) {}

AsyncLoopForTest::~AsyncLoopForTest() = default;

zx_status_t AsyncLoopForTest::RunUntilIdle() {
  return impl_->loop()->RunUntilIdle();
}

async_t* AsyncLoopForTest::async() {
  return impl_->loop()->async();
}

}  // namespace test
}  // namespace fidl
