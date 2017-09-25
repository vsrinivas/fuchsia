// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_AUTH_PROVIDER_TEST_TEST_AUTH_PROVIDER_H_
#define PERIDOT_BIN_LEDGER_AUTH_PROVIDER_TEST_TEST_AUTH_PROVIDER_H_

#include "peridot/bin/ledger/auth_provider/auth_provider.h"

#include "lib/fxl/tasks/task_runner.h"

namespace auth_provider {
namespace test {

class TestAuthProvider : public AuthProvider {
 public:
  explicit TestAuthProvider(fxl::RefPtr<fxl::TaskRunner> task_runner);

  // AuthProvider:
  fxl::RefPtr<callback::Cancellable> GetFirebaseToken(
      std::function<void(AuthStatus, std::string)> callback) override;

  fxl::RefPtr<callback::Cancellable> GetFirebaseUserId(
      std::function<void(AuthStatus, std::string)> callback) override;

  std::string token_to_return;

  AuthStatus status_to_return = AuthStatus::OK;

  std::string user_id_to_return;

 private:
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
};

}  // namespace test
}  // namespace auth_provider

#endif  // PERIDOT_BIN_LEDGER_AUTH_PROVIDER_TEST_TEST_AUTH_PROVIDER_H_
