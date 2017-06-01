// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/test/test_auth_provider.h"

#include <utility>

namespace cloud_sync {
namespace test {

TestAuthProvider::TestAuthProvider(ftl::RefPtr<ftl::TaskRunner> task_runner)
    : task_runner_(task_runner) {}

void TestAuthProvider::GetFirebaseToken(
    std::function<void(std::string)> callback) {
  task_runner_->PostTask(
      [ this, callback = std::move(callback) ] { callback(token_to_return); });
}

}  // namespace test
}  // namespace cloud_sync
