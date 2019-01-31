// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIREBASE_AUTH_FIREBASE_AUTH_IMPL_H_
#define PERIDOT_LIB_FIREBASE_AUTH_FIREBASE_AUTH_IMPL_H_

#include <functional>
#include <memory>
#include <string>

#include <fuchsia/auth/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/backoff/backoff.h>
#include <lib/callback/cancellable.h>
#include <lib/callback/scoped_task_runner.h>
#include <lib/cobalt/cpp/cobalt_logger.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fit/function.h>

#include "peridot/lib/firebase_auth/firebase_auth.h"
#include "peridot/lib/rng/random.h"

namespace firebase_auth {

// Source of the auth information for cloud sync to use, implemented using the
// system token provider.
//
// If configured with an empty |api_key|, doesn't attempt to use
// |token_manager| and yields empty Firebase tokens and user ids. This allows
// the code to work without auth against public instances (e.g. for running
// benchmarks).
//
// If configured with an empty |cobalt_client_name| or null |startup_context|,
// disables statistics collection about failures.
//
// *Warning*: if |token_manager| disconnects, all requests in progress are
// dropped on the floor. TODO(ppi): keep track of pending requests and call the
// callbacks with status TOKEN_MANAGER_DISCONNECTED when this happens.
class FirebaseAuthImpl : public FirebaseAuth {
 public:
  struct Config {
    // Value of the Firebase api key.
    std::string api_key;
    // Name of the client to record during Cobalt error reporting.
    std::string cobalt_client_name;
    // Maximum number of retries on non-fatal errors.
    int max_retries = 5;
  };

  FirebaseAuthImpl(Config config, async_dispatcher_t* dispatcher,
                   rng::Random* random,
                   fuchsia::auth::TokenManagerPtr token_manager,
                   component::StartupContext* startup_context);
  // For tests.
  FirebaseAuthImpl(Config config, async_dispatcher_t* dispatcher,
                   fuchsia::auth::TokenManagerPtr token_manager,
                   std::unique_ptr<backoff::Backoff> backoff,
                   std::unique_ptr<cobalt::CobaltLogger> cobalt_logger);

  // FirebaseAuth:
  void set_error_handler(fit::closure on_error) override;

  fxl::RefPtr<callback::Cancellable> GetFirebaseToken(
      fit::function<void(firebase_auth::AuthStatus, std::string)> callback)
      override;

  fxl::RefPtr<callback::Cancellable> GetFirebaseUserId(
      fit::function<void(firebase_auth::AuthStatus, std::string)> callback)
      override;

 private:
  // Retrieves the Firebase token from the fuchsia::auth::TokenManager,
  // transparently retrying the request up to |max_retries| times in case of
  // non-fatal errors.
  void GetToken(int max_retries,
                fit::function<void(firebase_auth::AuthStatus,
                                   fuchsia::auth::FirebaseTokenPtr)>
                    callback);

  // Sends a Cobalt event for metric |metric_id| counting the error code
  // |status|, unless |cobalt_client_name_| is empty.
  void ReportError(int32_t metric_id, uint32_t status);

  const Config config_;
  fuchsia::auth::TokenManagerPtr token_manager_;
  const std::unique_ptr<backoff::Backoff> backoff_;
  const int max_retries_;

  // Members for Cobalt reporting.
  const std::string cobalt_client_name_;
  std::unique_ptr<cobalt::CobaltLogger> cobalt_logger_;

  // Must be the last member field.
  callback::ScopedTaskRunner task_runner_;
};

}  // namespace firebase_auth

#endif  // PERIDOT_LIB_FIREBASE_AUTH_FIREBASE_AUTH_IMPL_H_
