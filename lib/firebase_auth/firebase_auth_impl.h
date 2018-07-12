// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIREBASE_AUTH_FIREBASE_AUTH_IMPL_H_
#define PERIDOT_LIB_FIREBASE_AUTH_FIREBASE_AUTH_IMPL_H_

#include <functional>
#include <memory>
#include <string>

#include <fuchsia/modular/auth/cpp/fidl.h>
#include <lib/app/cpp/startup_context.h>
#include <lib/async/dispatcher.h>
#include <lib/backoff/backoff.h>
#include <lib/callback/cancellable.h>
#include <lib/callback/scoped_task_runner.h>
#include <lib/fit/function.h>

#include "peridot/lib/cobalt/cobalt.h"
#include "peridot/lib/firebase_auth/firebase_auth.h"

namespace firebase_auth {

// Source of the auth information for cloud sync to use, implemented using the
// system token provider.
//
// If configured with an empty |api_key|, doesn't attempt to use
// |token_provider| and yields empty Firebase tokens and user ids. This allows
// the code to work without auth against public instances (e.g. for running
// benchmarks).
//
// If configured with an empty |cobalt_client_name| or null |startup_context|,
// disables statistics collection about failures.
//
// *Warning*: if |token_provider| disconnects, all requests in progress are
// dropped on the floor. TODO(ppi): keep track of pending requests and call the
// callbacks with status TOKEN_PROVIDER_DISCONNECTED when this happens.
class FirebaseAuthImpl : public FirebaseAuth {
 public:
  struct Config {
    std::string api_key;
    // Name of the client to record during Cobalt error reporting.
    std::string cobalt_client_name;
    // Maximum number of retries on non-fatal errors.
    int max_retries = 5;
  };

  FirebaseAuthImpl(Config config, async_dispatcher_t* dispatcher,
                   fuchsia::modular::auth::TokenProviderPtr token_provider,
                   fuchsia::sys::StartupContext* startup_context);
  // For tests.
  FirebaseAuthImpl(Config config, async_dispatcher_t* dispatcher,
                   fuchsia::modular::auth::TokenProviderPtr token_provider,
                   std::unique_ptr<backoff::Backoff> backoff,
                   std::unique_ptr<cobalt::CobaltContext> cobalt_context);

  // FirebaseAuth:
  void set_error_handler(fit::closure on_error) override;

  fxl::RefPtr<callback::Cancellable> GetFirebaseToken(
      fit::function<void(firebase_auth::AuthStatus, std::string)> callback)
      override;

  fxl::RefPtr<callback::Cancellable> GetFirebaseUserId(
      fit::function<void(firebase_auth::AuthStatus, std::string)> callback)
      override;

 private:
  // Retrieves the Firebase token from the token provider, transparently
  // retrying the request up to |max_retries| times in case of non-fatal
  // errors.
  void GetToken(int max_retries,
                fit::function<void(firebase_auth::AuthStatus,
                                   fuchsia::modular::auth::FirebaseTokenPtr)>
                    callback);

  // Sends a Cobalt event counting the error, unless |cobalt_client_name_| is
  // empty.
  void ReportError(fuchsia::modular::auth::Status status);

  const std::string api_key_;
  fuchsia::modular::auth::TokenProviderPtr token_provider_;
  const std::unique_ptr<backoff::Backoff> backoff_;
  const int max_retries_;

  // Members for Cobalt reporting.
  const std::string cobalt_client_name_;
  std::unique_ptr<cobalt::CobaltContext> cobalt_context_;

  // Must be the last member field.
  callback::ScopedTaskRunner task_runner_;
};

}  // namespace firebase_auth

#endif  // PERIDOT_LIB_FIREBASE_AUTH_FIREBASE_AUTH_IMPL_H_
