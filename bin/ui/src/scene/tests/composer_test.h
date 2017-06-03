// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/lib/tests/test_with_message_loop.h"
#include "apps/mozart/src/scene/composer_impl.h"

#include "gtest/gtest.h"

namespace mozart {
namespace composer {
namespace test {

// Subclass SessionHandler to make testing easier.
class SessionHandlerForTest : public SessionHandler {
 public:
  SessionHandlerForTest(
      ComposerImpl* composer,
      SessionId session_id,
      ::fidl::InterfaceRequest<mozart2::Session> request,
      ::fidl::InterfaceHandle<mozart2::SessionListener> listener);

  // mozart2::Session interface methods.
  void Enqueue(::fidl::Array<mozart2::OpPtr> ops) override;
  void Present(::fidl::Array<mx::event> wait_events,
               ::fidl::Array<mx::event> signal_events) override;
  void Connect(
      ::fidl::InterfaceRequest<mozart2::Session> session,
      ::fidl::InterfaceHandle<mozart2::SessionListener> listener) override;

  // Return the number of Enqueue()/Present()/Connect() messages that have
  // been processed.
  uint32_t enqueue_count() const { return enqueue_count_; }
  uint32_t present_count() const { return present_count_; }
  uint32_t connect_count() const { return connect_count_; }

 private:
  std::atomic<uint32_t> enqueue_count_;
  std::atomic<uint32_t> present_count_;
  std::atomic<uint32_t> connect_count_;
};

// Subclass ComposerImpl to make testing easier.
class ComposerImplForTest : public ComposerImpl {
 public:
  ComposerImplForTest() = default;

  using ComposerImpl::FindSession;

 private:
  std::unique_ptr<SessionHandler> CreateSessionHandler(
      SessionId id,
      ::fidl::InterfaceRequest<mozart2::Session> request,
      ::fidl::InterfaceHandle<mozart2::SessionListener> listener) override;
};

class ComposerTest : public mozart::test::TestWithMessageLoop {
 public:
  // ::testing::Test virtual method.
  void SetUp() override;

  // ::testing::Test virtual method.
  void TearDown() override;

  SessionPtr NewSession();

 protected:
  mozart2::ComposerPtr composer_;
  std::unique_ptr<fidl::Binding<mozart2::Composer>> composer_binding_;
  std::unique_ptr<ComposerImplForTest> composer_impl_;
  std::unique_ptr<mtl::Thread> thread_;
};

}  // namespace test
}  // namespace composer
}  // namespace mozart
