// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/composer/tests/composer_test.h"

#include "gtest/gtest.h"

namespace mozart {
namespace composer {
namespace test {

void ComposerTest::SetUp() {
  mozart::test::TestWithMessageLoop::SetUp();

  composer_impl_ = std::make_unique<ComposerImplForTest>();
  composer_binding_ =
      std::make_unique<fidl::Binding<mozart2::Composer>>(composer_impl_.get());

  thread_ = std::make_unique<mtl::Thread>();
  thread_->Run();

  auto interface_request = composer_.NewRequest();

  ftl::ManualResetWaitableEvent wait;
  thread_->TaskRunner()->PostTask([this, &interface_request, &wait]() {
    this->composer_binding_->Bind(std::move(interface_request));
    this->composer_binding_->set_connection_error_handler(
        [this]() { this->composer_impl_.reset(); });
    wait.Signal();
  });
  wait.Wait();
}

void ComposerTest::TearDown() {
  composer_ = nullptr;
  RUN_MESSAGE_LOOP_WHILE(composer_impl_ != nullptr);
  thread_->TaskRunner()->PostTask(
      []() { mtl::MessageLoop::GetCurrent()->QuitNow(); });
  thread_->Join();
}

SessionHandlerForTest::SessionHandlerForTest(
    ComposerImpl* composer,
    SessionId session_id,
    ::fidl::InterfaceRequest<mozart2::Session> request,
    ::fidl::InterfaceHandle<mozart2::SessionListener> listener)
    : SessionHandler(composer,
                     session_id,
                     std::move(request),
                     std::move(listener)),
      enqueue_count_(0),
      present_count_(0),
      connect_count_(0) {}

void SessionHandlerForTest::Enqueue(::fidl::Array<mozart2::OpPtr> ops) {
  SessionHandler::Enqueue(std::move(ops));
  ++enqueue_count_;
}

void SessionHandlerForTest::Present(::fidl::Array<mx::event> wait_events,
                                    ::fidl::Array<mx::event> signal_events) {
  SessionHandler::Present(std::move(wait_events), std::move(signal_events));
  ++present_count_;
}

void SessionHandlerForTest::Connect(
    ::fidl::InterfaceRequest<mozart2::Session> session,
    ::fidl::InterfaceHandle<mozart2::SessionListener> listener) {
  SessionHandler::Connect(std::move(session), std::move(listener));
  ++connect_count_;
}

std::unique_ptr<SessionHandler> ComposerImplForTest::CreateSessionHandler(
    SessionId session_id,
    ::fidl::InterfaceRequest<mozart2::Session> request,
    ::fidl::InterfaceHandle<mozart2::SessionListener> listener) {
  return std::make_unique<SessionHandlerForTest>(
      this, session_id, std::move(request), std::move(listener));
}

}  // namespace test
}  // namespace composer
}  // namespace mozart
