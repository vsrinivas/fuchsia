// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase_auth/testing/test_firebase_auth.h"

#include <utility>

#include <lib/async/cpp/task.h>

#include "lib/callback/cancellable_helper.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/functional/make_copyable.h"

namespace firebase_auth {

TestFirebaseAuth::TestFirebaseAuth(async_t* async) : async_(async) {}

void TestFirebaseAuth::set_error_handler(fxl::Closure on_error) {
  error_handler_ = on_error;
}

fxl::RefPtr<callback::Cancellable> TestFirebaseAuth::GetFirebaseToken(
    std::function<void(AuthStatus, std::string)> callback) {
  auto cancellable = callback::CancellableImpl::Create([] {});

  async::PostTask(async_,
                  [this, callback = cancellable->WrapCallback(callback)] {
                    callback(status_to_return, token_to_return);
                  });
  return cancellable;
}

fxl::RefPtr<callback::Cancellable> TestFirebaseAuth::GetFirebaseUserId(
    std::function<void(AuthStatus, std::string)> callback) {
  auto cancellable = callback::CancellableImpl::Create([] {});

  async::PostTask(async_,
                  [this, callback = cancellable->WrapCallback(callback)] {
                    callback(status_to_return, user_id_to_return);
                  });
  return cancellable;
}

void TestFirebaseAuth::TriggerConnectionErrorHandler() {
  error_handler_();
}

}  // namespace firebase_auth
