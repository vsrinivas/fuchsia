// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/context_test.h"

namespace zxdb {

constexpr uint64_t DebugAdapterContextTest::kProcessKoid;
constexpr uint64_t DebugAdapterContextTest::kThreadKoid;

void DebugAdapterContextTest::SetUp() {
  RemoteAPITest::SetUp();
  context_ = std::make_unique<DebugAdapterContext>(&session(), pipe_.end1());
  client_ = dap::Session::create();

  client_->connect(std::make_shared<DebugAdapterReader>(pipe_.end2()),
                   std::make_shared<DebugAdapterWriter>(pipe_.end2()));
  // Eat the output from process attaching (this is asynchronously appended).
  loop().RunUntilNoTasks();
}

void DebugAdapterContextTest::TearDown() {
  context_.reset();
  client_.reset();
  RemoteAPITest::TearDown();
}

void DebugAdapterContextTest::InitializeDebugging() {
  // Send initialize request from the client.
  auto response = client().send(dap::InitializeRequest{});
  // Run server to process request
  context().OnStreamReadable();
  // Run client twice to receive initialize response and event.
  RunClient();
  RunClient();
  response.get();
}

}  // namespace zxdb
