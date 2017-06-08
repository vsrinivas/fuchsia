// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/test/test_auth_provider.h"

#include <utility>

#include "apps/ledger/src/callback/cancellable_helper.h"
#include "lib/ftl/functional/make_copyable.h"

namespace cloud_sync {
namespace test {

TestAuthProvider::TestAuthProvider(ftl::RefPtr<ftl::TaskRunner> task_runner)
    : task_runner_(task_runner) {}

ftl::RefPtr<callback::Cancellable> TestAuthProvider::GetFirebaseToken(
    std::function<void(std::string)> callback) {
  auto cancellable = callback::CancellableImpl::Create([] {});

  task_runner_->PostTask([
    this, callback = cancellable->WrapCallback(callback)
  ] { callback(token_to_return); });
  return cancellable;
}

}  // namespace test
}  // namespace cloud_sync
