// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_AUTH_PROVIDER_TEST_TEST_AUTH_PROVIDER_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_AUTH_PROVIDER_TEST_TEST_AUTH_PROVIDER_H_

#include "peridot/bin/cloud_provider_firebase/auth_provider/auth_provider.h"

#include "lib/fxl/tasks/task_runner.h"

namespace auth_provider {
namespace test {

class TestAuthProvider : public AuthProvider {
 public:
  explicit TestAuthProvider(fxl::RefPtr<fxl::TaskRunner> task_runner);

  // AuthProvider:
  void set_connection_error_handler(fxl::Closure on_error) override;

  fxl::RefPtr<callback::Cancellable> GetFirebaseToken(
      std::function<void(AuthStatus, std::string)> callback) override;

  fxl::RefPtr<callback::Cancellable> GetFirebaseUserId(
      std::function<void(AuthStatus, std::string)> callback) override;

  void TriggerConnectionErrorHandler();

  std::string token_to_return;

  AuthStatus status_to_return = AuthStatus::OK;

  std::string user_id_to_return;

 private:
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  fxl::Closure error_handler_;
};

}  // namespace test
}  // namespace auth_provider

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIREBASE_AUTH_PROVIDER_TEST_TEST_AUTH_PROVIDER_H_
