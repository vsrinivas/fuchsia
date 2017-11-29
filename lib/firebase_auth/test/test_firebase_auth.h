// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIREBASE_AUTH_TEST_TEST_FIREBASE_AUTH_H_
#define PERIDOT_LIB_FIREBASE_AUTH_TEST_TEST_FIREBASE_AUTH_H_

#include "peridot/lib/firebase_auth/firebase_auth.h"

#include "lib/fxl/tasks/task_runner.h"

namespace firebase_auth {
namespace test {

class TestFirebaseAuth : public FirebaseAuth {
 public:
  explicit TestFirebaseAuth(fxl::RefPtr<fxl::TaskRunner> task_runner);

  // FirebaseAuth:
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
}  // namespace firebase_auth

#endif  // PERIDOT_LIB_FIREBASE_AUTH_TEST_TEST_FIREBASE_AUTH_H_
