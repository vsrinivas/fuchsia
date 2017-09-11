// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/auth_provider/test/test_auth_provider.h"

#include <utility>

#include "apps/ledger/src/callback/cancellable_helper.h"
#include "lib/fxl/functional/make_copyable.h"

namespace auth_provider {
namespace test {

TestAuthProvider::TestAuthProvider(fxl::RefPtr<fxl::TaskRunner> task_runner)
    : task_runner_(std::move(task_runner)) {}

fxl::RefPtr<callback::Cancellable> TestAuthProvider::GetFirebaseToken(
    std::function<void(AuthStatus, std::string)> callback) {
  auto cancellable = callback::CancellableImpl::Create([] {});

  task_runner_->PostTask([
    this, callback = cancellable->WrapCallback(callback)
  ] { callback(status_to_return, token_to_return); });
  return cancellable;
}

fxl::RefPtr<callback::Cancellable> TestAuthProvider::GetFirebaseUserId(
    std::function<void(AuthStatus, std::string)> callback) {
  auto cancellable = callback::CancellableImpl::Create([] {});

  task_runner_->PostTask([
    this, callback = cancellable->WrapCallback(callback)
  ] { callback(status_to_return, user_id_to_return); });
  return cancellable;
}

}  // namespace test
}  // namespace auth_provider
