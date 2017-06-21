// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_AUTH_PROVIDER_IMPL_H_
#define APPS_LEDGER_SRC_APP_AUTH_PROVIDER_IMPL_H_

#include <string>

#include "apps/ledger/src/cloud_sync/public/auth_provider.h"
#include "apps/modular/services/auth/token_provider.fidl.h"
#include "lib/ftl/tasks/task_runner.h"

namespace ledger {

// Source of the auth information for cloud sync to use, implemented using the
// system token provider.h
//
// This is currently a placeholder that always returns an empty token.
class AuthProviderImpl : public cloud_sync::AuthProvider {
 public:
  AuthProviderImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
                   std::string api_key,
                   modular::auth::TokenProviderPtr token_provider);

  // AuthProvider:
  ftl::RefPtr<callback::Cancellable> GetFirebaseToken(
      std::function<void(std::string)> callback) override;

  void GetFirebaseUserId(std::function<void(std::string)> callback) override;

 private:
  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  const std::string api_key_;
  modular::auth::TokenProviderPtr token_provider_;
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_AUTH_PROVIDER_IMPL_H_
