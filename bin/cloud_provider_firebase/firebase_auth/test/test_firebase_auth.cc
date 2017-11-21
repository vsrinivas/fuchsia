// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/firebase_auth/test/test_firebase_auth.h"

#include <utility>

#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/ledger/callback/cancellable_helper.h"

namespace firebase_auth {
namespace test {

TestFirebaseAuth::TestFirebaseAuth(fxl::RefPtr<fxl::TaskRunner> task_runner)
    : task_runner_(std::move(task_runner)) {}

void TestFirebaseAuth::set_connection_error_handler(fxl::Closure on_error) {
  error_handler_ = on_error;
}

fxl::RefPtr<callback::Cancellable> TestFirebaseAuth::GetFirebaseToken(
    std::function<void(AuthStatus, std::string)> callback) {
  auto cancellable = callback::CancellableImpl::Create([] {});

  task_runner_->PostTask(
      [this, callback = cancellable->WrapCallback(callback)] {
        callback(status_to_return, token_to_return);
      });
  return cancellable;
}

fxl::RefPtr<callback::Cancellable> TestFirebaseAuth::GetFirebaseUserId(
    std::function<void(AuthStatus, std::string)> callback) {
  auto cancellable = callback::CancellableImpl::Create([] {});

  task_runner_->PostTask(
      [this, callback = cancellable->WrapCallback(callback)] {
        callback(status_to_return, user_id_to_return);
      });
  return cancellable;
}

void TestFirebaseAuth::TriggerConnectionErrorHandler() {
  error_handler_();
}

}  // namespace test
}  // namespace firebase_auth
