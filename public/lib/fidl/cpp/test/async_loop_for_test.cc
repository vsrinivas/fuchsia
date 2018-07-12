// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/test/async_loop_for_test.h"

#include <lib/async-loop/cpp/loop.h>

namespace fidl {
namespace test {

class AsyncLoopForTestImpl {
 public:
  AsyncLoopForTestImpl() : loop_(&kAsyncLoopConfigMakeDefault) {}
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

zx_status_t AsyncLoopForTest::Run() { return impl_->loop()->Run(); }

async_dispatcher_t* AsyncLoopForTest::dispatcher() { return impl_->loop()->dispatcher(); }

}  // namespace test
}  // namespace fidl
