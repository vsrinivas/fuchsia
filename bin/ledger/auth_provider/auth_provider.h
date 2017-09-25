// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_AUTH_PROVIDER_AUTH_PROVIDER_H_
#define PERIDOT_BIN_LEDGER_AUTH_PROVIDER_AUTH_PROVIDER_H_

#include <functional>
#include <string>

#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/callback/cancellable.h"

namespace auth_provider {

enum class AuthStatus {
  OK,
  // Failed to retrieve the auth token.
  ERROR
};

// Source of tokens that are used to authenticate with cloud services.
//
// Each instance of this class is tied to exactly one user.
class AuthProvider {
 public:
  AuthProvider() {}
  virtual ~AuthProvider() {}

  // Retrieves the Firebase ID token suitable to use with Firebase Real-time
  // Database and Firebase Storage.
  virtual fxl::RefPtr<callback::Cancellable> GetFirebaseToken(
      std::function<void(AuthStatus, std::string)> callback) = 0;

  // Retrieves the Firebase user ID of the user.
  virtual fxl::RefPtr<callback::Cancellable> GetFirebaseUserId(
      std::function<void(AuthStatus, std::string)> callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(AuthProvider);
};

}  // namespace auth_provider

#endif  // PERIDOT_BIN_LEDGER_AUTH_PROVIDER_AUTH_PROVIDER_H_
