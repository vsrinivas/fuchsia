// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/firebase_auth/testing/credentials.h"

#include "gtest/gtest.h"
#include "src/ledger/lib/firebase_auth/testing/service_account_test_constants.h"

namespace service_account {
namespace {

TEST(CredentialsTest, CorrectConfig) {
  auto config = Credentials::Parse(kTestServiceAccountConfig);
  EXPECT_TRUE(config);

  EXPECT_EQ(config->project_id(), kTestServiceAccountProjectId);
  EXPECT_EQ(config->client_email(), kTestServiceAccountClientEmail);
  EXPECT_EQ(config->client_id(), kTestServiceAccountClientId);
}

TEST(CredentialsTest, IncorrectConfig) {
  EXPECT_FALSE(Credentials::Parse(""));
  EXPECT_FALSE(Credentials::Parse("{}"));
  EXPECT_FALSE(Credentials::Parse(kWrongKeyTestServiceAccountConfig));
}

TEST(CredentialsTest, Clone) {
  auto config = Credentials::Parse(kTestServiceAccountConfig);
  ASSERT_TRUE(config);

  auto cloned_config = config->Clone();
  EXPECT_EQ(cloned_config->project_id(), config->project_id());
  EXPECT_EQ(cloned_config->client_email(), config->client_email());
  EXPECT_EQ(cloned_config->client_id(), config->client_id());

  EXPECT_EQ(EVP_PKEY_cmp(config->private_key().get(), cloned_config->private_key().get()), 1);
}

}  // namespace
}  // namespace service_account
