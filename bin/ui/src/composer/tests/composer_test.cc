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

  composer_impl_ = std::make_unique<ComposerImpl>();
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

}  // namespace test
}  // namespace composer
}  // namespace mozart
