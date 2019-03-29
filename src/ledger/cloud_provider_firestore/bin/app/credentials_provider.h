// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_APP_CREDENTIALS_PROVIDER_H_
#define SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_APP_CREDENTIALS_PROVIDER_H_

#include <functional>
#include <memory>

#include <grpc++/grpc++.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/macros.h>

namespace cloud_provider_firestore {

// Interface for a provider of gRPC call credentials that can be used to make
// Firestore requests.
class CredentialsProvider {
 public:
  CredentialsProvider() {}
  virtual ~CredentialsProvider() {}

  // Retrieves call credentials.
  virtual void GetCredentials(
      fit::function<void(std::shared_ptr<grpc::CallCredentials>)> callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(CredentialsProvider);
};

}  // namespace cloud_provider_firestore

#endif  // SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_APP_CREDENTIALS_PROVIDER_H_
