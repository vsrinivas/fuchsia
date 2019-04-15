// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_APP_TESTING_TEST_CREDENTIALS_PROVIDER_H_
#define SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_APP_TESTING_TEST_CREDENTIALS_PROVIDER_H_

#include <grpc++/grpc++.h>
#include <lib/callback/scoped_task_runner.h>
#include <lib/fit/function.h>

#include <functional>
#include <memory>

#include "src/ledger/cloud_provider_firestore/bin/app/credentials_provider.h"

namespace cloud_provider_firestore {

class TestCredentialsProvider : public CredentialsProvider {
 public:
  explicit TestCredentialsProvider(async_dispatcher_t* dispatcher);
  ~TestCredentialsProvider() override;

  void GetCredentials(
      fit::function<void(std::shared_ptr<grpc::CallCredentials>)> callback)
      override;

 private:
  // Must be the last member.
  callback::ScopedTaskRunner task_runner_;
};

}  // namespace cloud_provider_firestore

#endif  // SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_APP_TESTING_TEST_CREDENTIALS_PROVIDER_H_
