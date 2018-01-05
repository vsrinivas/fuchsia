// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/credentials_provider_impl.h"

#include <gtest/gtest.h>

#include "peridot/lib/callback/capture.h"
#include "peridot/lib/firebase_auth/testing/test_firebase_auth.h"
#include "peridot/lib/gtest/test_with_message_loop.h"

namespace cloud_provider_firestore {

namespace {

class CredentialsProviderImplTest : public gtest::TestWithMessageLoop {
 public:
  CredentialsProviderImplTest() {
    auto firebase_auth = std::make_unique<firebase_auth::TestFirebaseAuth>(
        message_loop_.task_runner());
    firebase_auth_ = firebase_auth.get();
    credentials_provider_ =
        std::make_unique<CredentialsProviderImpl>(std::move(firebase_auth));
  }

 protected:
  firebase_auth::TestFirebaseAuth* firebase_auth_ = nullptr;
  std::unique_ptr<CredentialsProviderImpl> credentials_provider_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(CredentialsProviderImplTest);
};

TEST_F(CredentialsProviderImplTest, Ok) {
  std::shared_ptr<grpc::CallCredentials> call_credentials;
  credentials_provider_->GetCredentials(
      callback::Capture(MakeQuitTask(), &call_credentials));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(call_credentials);
}

TEST_F(CredentialsProviderImplTest, Error) {
  firebase_auth_->status_to_return = firebase_auth::AuthStatus::ERROR;
  std::shared_ptr<grpc::CallCredentials> call_credentials;
  credentials_provider_->GetCredentials(
      callback::Capture(MakeQuitTask(), &call_credentials));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(call_credentials);
}

}  // namespace

}  // namespace cloud_provider_firestore
