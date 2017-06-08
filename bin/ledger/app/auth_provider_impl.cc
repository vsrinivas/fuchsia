// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/auth_provider_impl.h"

#include <utility>

#include "apps/ledger/src/callback/cancellable_helper.h"
#include "lib/ftl/functional/make_copyable.h"

namespace ledger {

AuthProviderImpl::AuthProviderImpl(
    ftl::RefPtr<ftl::TaskRunner> task_runner,
    modular::auth::TokenProviderPtr token_provider)
    : task_runner_(task_runner), token_provider_(std::move(token_provider)) {}

ftl::RefPtr<callback::Cancellable> AuthProviderImpl::GetFirebaseToken(
    std::function<void(std::string)> callback) {
  auto cancellable = callback::CancellableImpl::Create([] {});
  // TODO(ppi): Request the token from |token_provider_| when support for
  // Firebase tokens is there.
  task_runner_->PostTask(
      [callback = cancellable->WrapCallback(callback)] { callback(""); });
  return cancellable;
}

}  // namespace ledger
