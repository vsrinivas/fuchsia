// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIREBASE_AUTH_FIREBASE_AUTH_H_
#define PERIDOT_LIB_FIREBASE_AUTH_FIREBASE_AUTH_H_

#include <functional>
#include <string>

#include "lib/callback/cancellable.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"

namespace firebase_auth {

enum class AuthStatus {
  OK,
  // Failed to retrieve the auth token.
  ERROR
};

// Source of Firebase Auth tokens that can be used to authenticate with Firebase
// services such as Firebase Real-Time Database, Firebase Storage or Firestore.
//
// Each instance of this class is tied to exactly one user.
class FirebaseAuth {
 public:
  FirebaseAuth() {}
  virtual ~FirebaseAuth() {}

  virtual void set_error_handler(fxl::Closure /*on_error*/) {}

  // Retrieves the Firebase ID token suitable to use with Firebase Real-time
  // Database and Firebase Storage.
  virtual fxl::RefPtr<callback::Cancellable> GetFirebaseToken(
      std::function<void(AuthStatus, std::string)> callback) = 0;

  // Retrieves the Firebase user ID of the user.
  virtual fxl::RefPtr<callback::Cancellable> GetFirebaseUserId(
      std::function<void(AuthStatus, std::string)> callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(FirebaseAuth);
};

}  // namespace firebase_auth

#endif  // PERIDOT_LIB_FIREBASE_AUTH_FIREBASE_AUTH_H_
