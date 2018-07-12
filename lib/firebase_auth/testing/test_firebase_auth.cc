// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase_auth/testing/test_firebase_auth.h"

#include <utility>

#include <lib/async/cpp/task.h>
#include <lib/callback/cancellable_helper.h>
#include <lib/fit/function.h>
#include <lib/fxl/functional/closure.h>
#include <lib/fxl/functional/make_copyable.h>

namespace firebase_auth {

TestFirebaseAuth::TestFirebaseAuth(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

void TestFirebaseAuth::set_error_handler(fit::closure on_error) {
  error_handler_ = std::move(on_error);
}

fxl::RefPtr<callback::Cancellable> TestFirebaseAuth::GetFirebaseToken(
    fit::function<void(AuthStatus, std::string)> callback) {
  auto cancellable = callback::CancellableImpl::Create([] {});

  async::PostTask(dispatcher_, [this, callback = cancellable->WrapCallback(
                                     std::move(callback))]() mutable {
    callback(status_to_return, token_to_return);
  });
  return cancellable;
}

fxl::RefPtr<callback::Cancellable> TestFirebaseAuth::GetFirebaseUserId(
    fit::function<void(AuthStatus, std::string)> callback) {
  auto cancellable = callback::CancellableImpl::Create([] {});

  async::PostTask(dispatcher_, [this, callback = cancellable->WrapCallback(
                                     std::move(callback))]() mutable {
    callback(status_to_return, user_id_to_return);
  });
  return cancellable;
}

void TestFirebaseAuth::TriggerConnectionErrorHandler() { error_handler_(); }

}  // namespace firebase_auth
