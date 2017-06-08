// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_TEST_TEST_AUTH_PROVIDER_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_TEST_TEST_AUTH_PROVIDER_H_

#include "apps/ledger/src/cloud_sync/public/auth_provider.h"

#include "lib/ftl/tasks/task_runner.h"

namespace cloud_sync {
namespace test {

class TestAuthProvider : public AuthProvider {
 public:
  TestAuthProvider(ftl::RefPtr<ftl::TaskRunner> task_runner);

  // AuthProvider:
  ftl::RefPtr<callback::Cancellable> GetFirebaseToken(
      std::function<void(std::string)> callback) override;

  std::string token_to_return;

 private:
  ftl::RefPtr<ftl::TaskRunner> task_runner_;
};

}  // namespace test
}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_TEST_TEST_AUTH_PROVIDER_H_
