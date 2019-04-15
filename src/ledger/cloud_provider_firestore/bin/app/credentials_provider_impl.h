// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_APP_CREDENTIALS_PROVIDER_IMPL_H_
#define SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_APP_CREDENTIALS_PROVIDER_IMPL_H_

#include <grpc++/grpc++.h>
#include <lib/callback/cancellable.h>
#include <lib/fit/function.h>

#include <functional>
#include <memory>

#include "src/ledger/cloud_provider_firestore/bin/app/credentials_provider.h"
#include "src/ledger/lib/firebase_auth/firebase_auth.h"

namespace cloud_provider_firestore {

class CredentialsProviderImpl : public CredentialsProvider {
 public:
  explicit CredentialsProviderImpl(
      std::unique_ptr<firebase_auth::FirebaseAuth> firebase_auth);
  ~CredentialsProviderImpl() override;

  void GetCredentials(
      fit::function<void(std::shared_ptr<grpc::CallCredentials>)> callback)
      override;

 private:
  std::unique_ptr<firebase_auth::FirebaseAuth> firebase_auth_;

  // Pending auth token requests to be cancelled when this class goes away.
  callback::CancellableContainer auth_token_requests_;
};

}  // namespace cloud_provider_firestore

#endif  // SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_APP_CREDENTIALS_PROVIDER_IMPL_H_
