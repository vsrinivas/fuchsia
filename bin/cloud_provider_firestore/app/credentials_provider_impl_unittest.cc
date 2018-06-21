// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/credentials_provider_impl.h"

#include <gtest/gtest.h>

#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/gtest/test_with_loop.h"
#include "peridot/lib/firebase_auth/testing/test_firebase_auth.h"

namespace cloud_provider_firestore {

namespace {

class CredentialsProviderImplTest : public gtest::TestWithLoop {
 public:
  CredentialsProviderImplTest() {
    auto firebase_auth =
        std::make_unique<firebase_auth::TestFirebaseAuth>(dispatcher());
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
  bool called;
  credentials_provider_->GetCredentials(
      callback::Capture(callback::SetWhenCalled(&called), &call_credentials));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_TRUE(call_credentials);
}

TEST_F(CredentialsProviderImplTest, Error) {
  firebase_auth_->status_to_return = firebase_auth::AuthStatus::ERROR;
  bool called;
  std::shared_ptr<grpc::CallCredentials> call_credentials;
  credentials_provider_->GetCredentials(
      callback::Capture(callback::SetWhenCalled(&called), &call_credentials));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_FALSE(call_credentials);
}

}  // namespace

}  // namespace cloud_provider_firestore
