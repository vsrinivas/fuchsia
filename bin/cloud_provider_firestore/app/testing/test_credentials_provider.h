// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_TESTING_TEST_CREDENTIALS_PROVIDER_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_TESTING_TEST_CREDENTIALS_PROVIDER_H_

#include <functional>
#include <memory>

#include <grpc++/grpc++.h>

#include "peridot/bin/cloud_provider_firestore/app/credentials_provider.h"
#include "peridot/lib/callback/scoped_task_runner.h"

namespace cloud_provider_firestore {

class TestCredentialsProvider : public CredentialsProvider {
 public:
  explicit TestCredentialsProvider(fxl::RefPtr<fxl::TaskRunner> task_runner);
  ~TestCredentialsProvider() override;

  void GetCredentials(
      std::function<void(std::shared_ptr<grpc::CallCredentials>)> callback)
      override;

 private:
  // Must be the last member.
  callback::ScopedTaskRunner task_runner_;
};

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_TESTING_TEST_CREDENTIALS_PROVIDER_H_
