// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/session_handler_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

void SessionHandlerTest::SetUp() { SessionTest::SetUp(); }

void SessionHandlerTest::TearDown() {
  SessionTest::TearDown();
  session_handler_.reset();
  scenic_.reset();
  app_context_.reset();
}

void SessionHandlerTest::InitializeScenic() {
  // TODO(SCN-720): Wrap CreateFromStartupInfo using ::gtest::Environment
  // instead of this hack.  This code has the chance to break non-ScenicTests.
  app_context_ = component::StartupContext::CreateFromStartupInfo();
  scenic_ = std::make_unique<Scenic>(app_context_.get(), [] {});
}

void SessionHandlerTest::InitializeSessionHandler() {
  if (!scenic_) {
    InitializeScenic();
  }

  auto session_context = CreateBarebonesSessionContext();
  session_handler_ = std::make_unique<SessionHandlerForTest>(
      CommandDispatcherContext(scenic_.get(), nullptr, session_->id()),
      session_manager_.get(), std::move(session_context));
  session_manager_->InsertSessionHandler(session_->id(),
                                         session_handler_.get());
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
