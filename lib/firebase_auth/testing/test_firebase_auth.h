// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIREBASE_AUTH_TESTING_TEST_FIREBASE_AUTH_H_
#define PERIDOT_LIB_FIREBASE_AUTH_TESTING_TEST_FIREBASE_AUTH_H_

#include <lib/async/dispatcher.h>

#include "peridot/lib/firebase_auth/firebase_auth.h"

namespace firebase_auth {

class TestFirebaseAuth : public FirebaseAuth {
 public:
  explicit TestFirebaseAuth(async_t* async);

  // FirebaseAuth:
  void set_error_handler(fxl::Closure on_error) override;

  fxl::RefPtr<callback::Cancellable> GetFirebaseToken(
      std::function<void(AuthStatus, std::string)> callback) override;

  fxl::RefPtr<callback::Cancellable> GetFirebaseUserId(
      std::function<void(AuthStatus, std::string)> callback) override;

  void TriggerConnectionErrorHandler();

  std::string token_to_return;

  AuthStatus status_to_return = AuthStatus::OK;

  std::string user_id_to_return;

 private:
  async_t* const async_;

  fxl::Closure error_handler_;
};

}  // namespace firebase_auth

#endif  // PERIDOT_LIB_FIREBASE_AUTH_TESTING_TEST_FIREBASE_AUTH_H_
